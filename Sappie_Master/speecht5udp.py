#!/usr/bin/env python
# coding: utf-8


from transformers import pipeline
from datasets import load_dataset
import numpy as np
import socket
import time
import soundfile as sf
import torch
device="cuda"

class TTS:
	def __init__(self):
		self.synthesiser = pipeline("text-to-speech", "microsoft/speecht5_tts",device=0)
		
		embeddings_dataset = load_dataset("Matthijs/cmu-arctic-xvectors", split="validation")
		self.speaker_embeddings = torch.tensor(embeddings_dataset[6500]["xvector"]).to(device).unsqueeze(0)
	
	def speak(self, text, destIP):
		UDP_PORT = 9000
		sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
		speech = self.synthesiser(text, forward_params={"speaker_embeddings": self.speaker_embeddings})
				
		audio=speech['audio']
		sample_rate = speech['sampling_rate']
				
		
		for x in range(0, audio.shape[0],368):
			start = x
			end = x + 368
			sock.sendto(audio[start:end].tobytes(), (destIP, UDP_PORT))
			time.sleep(0.022)
		
