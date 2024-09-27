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

device="cuda"
voice=4830

class TTS:	
	def __init__(self, ip, display, sense):
		self.loaded = False
		self.dest_ip = ip
		self.display = display
		self.sense = sense
		self.audio_q = queue.Queue(maxsize=5)
		self.text_q = queue.Queue(maxsize=8)
		self.running = False
		self.micstopped = False
		
	def loadModel(self):
		print("Loading TTS models")

		if not self.loaded:
			self.synthesiser = pipeline("text-to-speech", "microsoft/speecht5_tts", device=0)
			
			embeddings_dataset = load_dataset("Matthijs/cmu-arctic-xvectors", split="validation")
			self.speaker_embeddings = torch.tensor(embeddings_dataset[voice]["xvector"]).to(device).unsqueeze(0)	
	
			threading.Thread(target=self.sendaudio, daemon=True).start()

		print("STT Models loaded")
		self.loaded = True

	def speak(self, text):
		try:
			self.text_q.put_nowait(text)
		except Exception as e:
			print("Text queue error : ",type(e).__name__ )
		
	def generate_speech(self):
		while self.running:
			text = self.text_q.get()
			
			speech = self.synthesiser(text, forward_params={"speaker_embeddings": self.speaker_embeddings})
	
			if not self.micstopped:		
				self.sense.stopmic() # Stop mic to prevent speech loop
				self.display.action(23) # Show chat animation in display
				self.micstopped = True
			
			try:
				self.audio_q.put_nowait(speech)
			except Exception as e:
				print("Audio queue error : ", type(e).__name__ )
			
			self.text_q.task_done()

	def sendaudio(self):
		UDP_PORT = 9000
		sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)

		while self.running:
			speech = self.audio_q.get()

			audio=speech['audio']
			sample_rate = speech['sampling_rate']
	
			for x in range(0, audio.shape[0],368):
				start = x
				end = x + 368
				sock.sendto(audio[start:end].tobytes(), (self.dest_ip, UDP_PORT))
				time.sleep(0.022) # Audio stream limit

			self.audio_q.task_done()
			
			if self.audio_q.empty() and self.text_q.empty():
				time.sleep(1) # Wait for queue fill before enabling again
				if self.audio_q.empty() and self.text_q.empty():
					self.display.action(3) # Show neutral face
					self.sense.startmic() # Start mic
					self.micstopped = False

	def queue_size(self):
		return self.text_q.size()
		
	def start(self):
		self.running = True
		threading.Thread(target=self.generate_speech, daemon=True).start()
		threading.Thread(target=self.sendaudio, daemon=True).start()
	
	def stop(self):
		self.running = False
