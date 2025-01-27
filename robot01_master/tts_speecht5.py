#!/usr/bin/env python
# coding: utf-8

from transformers import pipeline
from datasets import load_dataset
import numpy as np
import time
import threading
import queue
import torch
import time

device="cuda"
voice=4830

# queue sizes
TEXT_Q_SIZE = 15

# Text to speech class for robot01 project
class TTS:	
	# init
	# ip = ip of robot01
	def __init__(self, ip, output_q):
	
		self.dest_ip = ip
		self.output_q = output_q

		# Engine State flags, loaded = model(s)
		self.loaded = False
		self.running = False
		
		#Queues
		self.text_q = queue.Queue(maxsize=TEXT_Q_SIZE)

	# Load TTS model
	def loadmodel(self):
		print("Loading TTS model")

		if not self.loaded:
			self.synthesiser = pipeline("text-to-speech", "microsoft/speecht5_tts", device=0)
			embeddings_dataset = load_dataset("Matthijs/cmu-arctic-xvectors", split="validation")
			self.speaker_embeddings = torch.tensor(embeddings_dataset[voice]["xvector"]).to(device).unsqueeze(0)	
	
		print("STT Model loaded")
		self.loaded = True

	# Generate Audio worker : Synthesises text from queue : output_q
	# puts audio (16khz mono f32le) into output_q
	#
	def generate_speech(self):
		print("TTS Speech synthesizer worker started.")	
		while self.running:
			
			text = self.text_q.get()
			synth_speech = self.synthesiser(text, forward_params={"speaker_embeddings": self.speaker_embeddings})
			try:
				self.output_q.put_nowait({"type" : "speech", "text" : text, "audio" : synth_speech['audio']})
			except Exception as e:
				print("Audio queue error : ", type(e).__name__ )
			
			self.text_q.task_done()

	# returns text_q size
	def queue_size(self)->int:
		return self.text_q.qsize()
	
	# Start workers
	def start(self):
		if not self.running:
			if not self.loaded:
				self.loadmodel()		
			self.running = True
			threading.Thread(target=self.generate_speech, daemon=True).start()
	
	# Stop workers
	def stop(self):
		self.running = False
		# Wait for save shutdown of threads and queues
		time.sleep(1)
