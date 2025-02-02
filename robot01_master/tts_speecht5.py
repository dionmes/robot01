#!/usr/bin/env python
# coding: utf-8

from transformers import pipeline
from datasets import load_dataset
import torch
import time

device="cuda"
voice=4830

# Text to speech model class for robot01 project
class ROBOT_TTS_MODEL:	
	def __init__(self):	
		print("Loading TTS model")

		self.synthesiser = pipeline("text-to-speech", "microsoft/speecht5_tts", device=0)
		embeddings_dataset = load_dataset("Matthijs/cmu-arctic-xvectors", split="validation")
		self.speaker_embeddings = torch.tensor(embeddings_dataset[voice]["xvector"]).to(device).unsqueeze(0)	

	# Generate Audio worker : Synthesises text from queue : output_q
	# puts audio (16khz mono f32le) into output_q
	#
	def synthesize(self, text)->any :
		synth_speech = self.synthesiser(text, forward_params={"speaker_embeddings": self.speaker_embeddings})
		
		return synth_speech['audio']
