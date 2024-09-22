import asyncio
from typing import List
import numpy as np
import socket
import threading
import queue

import torch
from transformers import AutoModelForSpeechSeq2Seq, AutoProcessor, pipeline

device = "cuda" if torch.cuda.is_available() else "cpu"
torch.set_default_device(device)
torch_dtype = torch.float16
model_id = "distil-whisper/distil-large-v2"

UDP_IP = "0.0.0.0" 
UDP_PORT = 3000 # Audio stream receive udp port
sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

# STT Class with VAD
# uses display engine for direct response to display when speech is detected
class STT:
	def __init__(self,display, silence_periond = 75, sensitivity = 0.09, speech_length = 41000):
	
		self.sensitivity = sensitivity
		self.loaded = False
		self.speech_detected = False
		self.silence_period = silence_periond
		self.speech_length = speech_length
		
		self.stt_q = queue.Queue(3)
		self.audiochunks_q =  queue.Queue(2)
		
		self.running = False
		self.display = display

	# Model loading is deferred so class can be loaded without loading model, speeding up startup of the server
	def load_models(self):
	
		if not self.loaded:
			print("Loading STT models")
			
			# Forcing the whole VAD model to CPU as ONNX and input are mixed
			self.vad_model, _ = torch.hub.load(repo_or_dir="snakers4/silero-vad", model="silero_vad", force_reload=False,onnx=True,)
			
			self.stt_model = AutoModelForSpeechSeq2Seq.from_pretrained(model_id, torch_dtype=torch_dtype, low_cpu_mem_usage=True, use_safetensors=True).to(device)
			self.stt_model.to(device)
			self.processor = AutoProcessor.from_pretrained(model_id)
	
			self.transcriber = pipeline(
				"automatic-speech-recognition",
				device=device,
				model=self.stt_model,
				tokenizer=self.processor.tokenizer,
				feature_extractor=self.processor.feature_extractor,
				max_new_tokens=128,
				torch_dtype=torch_dtype,
			)

		print("STT Models loaded")
		self.loaded = True
			
	# async audio receiver
	def receive_audio(self):
		
		np_audio_chunk = np.empty(0)
		nonsilence_counter = 0 # Counter for silence period
		
		self.vad_model.reset_states() 

		print("Ready to transcribe...")
		
		while self.running:
		
			speech_prob = 0
			
			try:
				# Receive data from the socket (with a buffer size of 1024 bytes)
				# Need a 2048 Sample for VAD so will add two together, as audio packets are 1024 size
				data1, addr = sock.recvfrom(1024)  # Buffer size is 1024 bytes
				data2, addr = sock.recvfrom(1024)  # Buffer size is 1024 bytes
				raw_data = data1 + data2
				
				np_data = np.frombuffer(raw_data, dtype=np.float32)
				
				# VAD
				speech_prob = self.vad_model(torch.tensor(np_data).cpu(), 16000).cpu().item()
				
			except:
				print(f"Audio receive/detection fail")

			if speech_prob > self.sensitivity:
			
				nonsilence_counter = self.silence_period
				if not self.speech_detected:
					self.speech_detected = True
					print("STT Detection started")
					#Send display action
					self.display.action(19)

			# Create Audio transcription sample until silence is detected via nonsilence_counter. 
			# Currently no max sample size. 
			if nonsilence_counter > 0:	
			
				nonsilence_counter = nonsilence_counter - 1
				np_audio_chunk = np.concatenate([np_audio_chunk,np_data])
				
				# Silende detected
				if nonsilence_counter <= 0:
					if np_audio_chunk.size > self.speech_length:
						try:
							self.audiochunks_q.put_nowait(np_audio_chunk)
						except Exception as e:
							print("Audiochunk queue error : " ,e)

					print("STT Detection ended")
					self.speech_detected = False
					nonsilence_counter = 0
					self.vad_model.reset_states() 

					#Send display action 
					self.display.action(3)
					np_audio_chunk = np.empty(0)
					
	# async function to do audio transcription
	def transcribe(self):
		while self.running:
			transcription = ""
			audio_chunk = self.audiochunks_q.get()
			self.display.action(20)

			input_features = self.processor(audio_chunk, sampling_rate=16000, return_tensors="pt", device="cuda").input_features
			
			input_features = input_features.to(device, dtype=torch_dtype)
			
			gen_kwargs = {"max_new_tokens": 128,"num_beams": 1,"return_timestamps": False}
			
			pred_ids = self.stt_model.generate(input_features, **gen_kwargs)
			transcription = self.processor.batch_decode(pred_ids, skip_special_tokens=True, decode_with_timestamps=gen_kwargs["return_timestamps"])

			try:
				self.stt_q.put(transcription)
			except Exception as e:
				print("Transcribe error : ",e)
				self.display.action(3)

			self.audiochunks_q.task_done()

	def start(self):
		self.running = True
		threading.Thread(target=self.transcribe, daemon=True).start()
		threading.Thread(target=self.receive_audio, daemon=True).start()
	
	def stop(self):
		self.running = False
		

