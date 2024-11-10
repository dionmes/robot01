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
ip="192.168.133.75"
text="Hello, I am back again. Sappie in tha house!"

print("Start")
synthesiser = pipeline("text-to-speech", "microsoft/speecht5_tts", device=0)
print("Load Embedding")
embeddings_dataset = load_dataset("Matthijs/cmu-arctic-xvectors", split="validation")
print("Load tensors")
speaker_embeddings = torch.tensor(embeddings_dataset[voice]["xvector"]).to(device).unsqueeze(0)	
print("Synthesize")
speech = synthesiser(text, forward_params={"speaker_embeddings": speaker_embeddings})

print("UDP Send")

audio=speech['audio']
sample_rate = speech['sampling_rate']

for x in range(0, audio.shape[0],368):
    start = x
    end = x + 368
    sock.sendto(audio[start:end].tobytes(), ("", UDP_PORT))
    time.sleep(0.0213) # Audio stream limit
