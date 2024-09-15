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
audio_q = queue.Queue(maxsize=3)

class TTS:	
	def __init__(self):
	    self.loaded = False
	    
	def loadModel(self):
		self.synthesiser = pipeline("text-to-speech", "microsoft/speecht5_tts", device=0)
		
		embeddings_dataset = load_dataset("Matthijs/cmu-arctic-xvectors", split="validation")
		self.speaker_embeddings = torch.tensor(embeddings_dataset[voice]["xvector"]).to(device).unsqueeze(0)	

		threading.Thread(target=self.sendaudio, daemon=True).start()

		self.loaded = True
	
	def speak(self, text, destIP):
		UDP_PORT = 9000
		sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
		speech = self.synthesiser(text, forward_params={"speaker_embeddings": self.speaker_embeddings})
		try:
			audio_q.put_nowait(speech)
		except:
			print("Audio speech queue full")
			
	def sendaudio(self):
		while True:
			speech = audio_q.get()

			audio=speech['audio']
			sample_rate = speech['sampling_rate']
	
			for x in range(0, audio.shape[0],368):
				start = x
				end = x + 368
				sock.sendto(audio[start:end].tobytes(), (destIP, UDP_PORT))
				time.sleep(0.022) # Audio stream limit

