#!/usr/bin/env python
# coding: utf-8

from transformers import pipeline
from datasets import load_dataset
import numpy as np
import socket
import time
import threading
import queue
import soundfile as sf
import torch
import time

device="cuda"
voice=4830

# queue sizes
AUDIO_Q = 8
TEXT_Q = 15

# Rececing buffer size for emptying ( RECEIVING_BUFFER * 1472)
RECEIVING_BUFFER = 6

# Text to speech class for robot01 project
class TTS:	
	# init
	# ip = ip of robot01
	# brain = brain object
	def __init__(self, ip, brain):
	
		self.dest_ip = ip
		self.brain = brain
		
		# Engine State flags, loaded = model(s)
		self.loaded = False
		self.running = False
		
		#Queues
		self.text_q = queue.Queue(maxsize=TEXT_Q)
		self.audio_q = queue.Queue(maxsize=AUDIO_Q)
	
	# Load TTS model
	def loadmodels(self):
		print("Loading TTS model")

		if not self.loaded:
			self.synthesiser = pipeline("text-to-speech", "microsoft/speecht5_tts", device=0)
			embeddings_dataset = load_dataset("Matthijs/cmu-arctic-xvectors", split="validation")
			self.speaker_embeddings = torch.tensor(embeddings_dataset[voice]["xvector"]).to(device).unsqueeze(0)	
	
		print("STT Models loaded")
		self.loaded = True

	# Generate Audio worker : Synthesises text from queue : audio_q
	# puts audio (16khz mono f32le) into audio_q
	#
	def generate_speech(self):
		print("TTS Speech synthesizer worker started.")	
		while self.running:
			
			text = self.text_q.get()
			synth_speech = self.synthesiser(text, forward_params={"speaker_embeddings": self.speaker_embeddings})

			try:
				self.audio_q.put_nowait([synth_speech,text])
			except Exception as e:
				print("Audio queue error : ", type(e).__name__ )
			
			self.text_q.task_done()

			if not self.brain.talking:
				self.brain.talking_started()

	# Send Audio worker : Gets audio wave from queue : audio_q
	# sends audio (16khz mono f32le) over UDP to robot01 ip port 9000
	#
	def sendaudio(self):
		UDP_PORT = 9000
		sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)

		print("TTS Audio over UDP worker started.")
		
		while self.running:
		
			speech = self.audio_q.get()
			synth_speech = speech[0]
			text = speech[1]
			self.brain.robot_expression(text)

			audio=synth_speech['audio']
			sample_rate = synth_speech['sampling_rate']
	
			# Send packets of max. 1472 / 4 byte sample size
			for x in range(0, audio.shape[0],368):
				start = x
				end = x + 368
				sock.sendto(audio[start:end].tobytes(), (self.dest_ip, UDP_PORT))
				time.sleep(0.021)

			# Send packets of 1 byte to flush buffer on robot side
			sock.sendto(bytes(1), (self.dest_ip, UDP_PORT))
			self.audio_q.task_done()
			
			if self.audio_q.empty() and self.text_q.empty():
				if self.brain.talking:
					self.brain.talking_stopped()

	# returns text_q size
	def queue_size(self)->int:
		return self.text_q.qsize()
	
	# Start workers
	def start(self):
		if not self.running:
			if not self.loaded:
				self.loadmodels()		
			self.running = True
			threading.Thread(target=self.generate_speech, daemon=True).start()
			threading.Thread(target=self.sendaudio, daemon=True).start()
	
	# Stop workers
	def stop(self):
		self.running = False
		self.text_q = queue.Queue(maxsize=TEXT_Q)
		self.audio_q = queue.Queue(maxsize=AUDIO_Q)
		
		# Wait for save shutdown of threads and queues
		time.sleep(1)


