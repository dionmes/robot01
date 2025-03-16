import asyncio
from typing import List
import numpy as np
import socket
import threading
import time
import queue
import torch
from transformers import AutoModelForSpeechSeq2Seq, AutoProcessor, pipeline

device = "cuda" if torch.cuda.is_available() else "cpu"
torch.set_default_device(device)
torch_dtype = torch.float16
model_id = "distil-whisper/distil-large-v3"

UDP_IP = "0.0.0.0" 
UDP_PORT = 3000 # Audio stream receive udp port
sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

# VAD PARAMETERS
MAX_SILENCE_PERIOD = 100
VAD_SENSITIVITY = 0.09
MIN_SPEECH_LENGTH = 41000

# QUEUE size
AUDIOCHUNKS_Q_SIZE = 2

# STT Class with VAD
class STT:
	def __init__(self, brain):
	
		# Engine State flags, loaded = model(s)
		self.loaded = False
		self.running = False
		self.brain = brain

		# VAD Parameters
		self.vad_sensitivity = VAD_SENSITIVITY
		self.max_silence_period = MAX_SILENCE_PERIOD
		self.min_speech_length = MIN_SPEECH_LENGTH
		
		self.audiochunks_q = queue.Queue(maxsize=AUDIOCHUNKS_Q_SIZE)
		
	# Model loading is deferred so class can be loaded without loading model, speeding up startup of the server
	def loadmodels(self):
		if not self.loaded:
			print("Loading STT models")
			
			# Forcing the whole VAD model to CPU as ONNX and input are mixed
			self.vad_model, _ = torch.hub.load(repo_or_dir="snakers4/silero-vad", model="silero_vad", force_reload=False, onnx=True)
			
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
				torch_dtype=torch_dtype
			)

		print("STT Models loaded")
		self.loaded = True
			
	# async audio receiver
	def receive_audio(self):
		
		np_audio_chunk = np.empty(0)
		nonsilence_counter = 0 # Counter for silence period
		speech_detected = False
		
		self.vad_model.reset_states() 
	
		# 25 samples to establish baseline	
		for i in range(25):
			data1, addr = sock.recvfrom(1024)  # Buffer size is 1024 bytes
			data2, addr = sock.recvfrom(1024)  # Buffer size is 1024 bytes
			raw_data = data1 + data2
			np_data = np.frombuffer(raw_data, dtype=np.float32)
			speech_probability = self.vad_model(torch.tensor(np_data).cpu(), 16000).cpu().item()

		print("Ready to listen...")		
		
		while self.running:
			speech_probability = 0
			try:
				# Receive data from the socket (with a buffer size of 1024 bytes)
				# Need a 2048 Sample for VAD so will add two together, as audio packets are 1024 size
				data1, addr = sock.recvfrom(1024)  # Buffer size is 1024 bytes
				data2, addr = sock.recvfrom(1024)  # Buffer size is 1024 bytes
				raw_data = data1 + data2
				
				np_data = np.frombuffer(raw_data, dtype=np.float32)
				
				# VAD
				speech_probability = self.vad_model(torch.tensor(np_data).cpu(), 16000).cpu().item()
				print(speech_probability)
				
			except Exception as e:
				print("UDP audio receive & analysis error : ",type(e).__name__ )

			if speech_probability > self.vad_sensitivity:
				nonsilence_counter = self.max_silence_period
				if not speech_detected:
					speech_detected = True
					print("STT Detection started")
					#Send robot.display action
					self.brain.robot.display.state(19)

			# Create Audio transcription sample until silence is detected via nonsilence_counter. 
			# Currently no max sample size. 
			if nonsilence_counter > 0:	
			
				nonsilence_counter = nonsilence_counter - 1
				np_audio_chunk = np.concatenate([np_audio_chunk,np_data])
				
				# Silende detected
				if nonsilence_counter <= 0:
					if np_audio_chunk.size > self.min_speech_length:		
						try:
							self.audiochunks_q.put_nowait(np_audio_chunk)
						except Exception as e:
							print("Audiochunk queue error : " ,e)

					print("STT Detection ended")
					speech_detected = False
					nonsilence_counter = 0
					self.vad_model.reset_states() 

					#Send robot.display action 
					self.brain.robot.display.state(3)
					np_audio_chunk = np.empty(0)
					
	# async function to do audio transcription
	def transcribe(self):
		print("Ready to transcribe...")
		while self.running:
			transcription = ""
			audio_chunk = self.audiochunks_q.get()
			self.brain.robot.display.state(20)

			input_features = self.processor(audio_chunk, sampling_rate=16000, return_tensors="pt", device="cuda").input_features
			input_features = input_features.to(device, dtype=torch_dtype)
			
			gen_kwargs = {"max_new_tokens": 128,"num_beams": 1,"return_timestamps": False}
			
			pred_ids = self.stt_model.generate(input_features, **gen_kwargs)
			transcription = self.processor.batch_decode(pred_ids, skip_special_tokens=True, decode_with_timestamps=gen_kwargs["return_timestamps"])

			print(transcription)

			try:
				self.brain.stt_q.put_nowait(transcription)
			except Exception as e:
				print("Transcribe error : ",e)

			self.audiochunks_q.task_done()
			self.brain.robot.display.state(3)

	def start(self):
		if not self.running:
			self.running = True
			if not self.loaded:
				self.loadmodels()

			threading.Thread(target=self.receive_audio, daemon=True).start()
			threading.Thread(target=self.transcribe, daemon=True).start()
	
	def stop(self):
		self.running = False
		self.audiochunks_q = queue.Queue(maxsize=AUDIOCHUNKS_Q_SIZE)

		# Wait for save shutdown of threads and queues
		time.sleep(1)

