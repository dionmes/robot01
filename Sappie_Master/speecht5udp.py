#!/usr/bin/env python
# coding: utf-8

# In[1]:


from transformers import pipeline
from datasets import load_dataset
import numpy as np
import socket
import time
import soundfile as sf
import torch
device="cuda"


# In[2]:


synthesiser = pipeline("text-to-speech", "microsoft/speecht5_tts",device=0)


# In[192]:


embeddings_dataset = load_dataset("Matthijs/cmu-arctic-xvectors", split="validation")
speaker_embeddings = torch.tensor(embeddings_dataset[6500]["xvector"]).to(device).unsqueeze(0)


# In[193]:


speech = synthesiser("A robot is a machine—especially one programmable by a computer.", forward_params={"speaker_embeddings": speaker_embeddings})


# In[194]:


audio=speech['audio']
sample_rate =speech['sampling_rate']


# In[195]:


UDP_IP = "192.168.133.75"
UDP_PORT = 9000
audio


# In[199]:


sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)

for x in range(0, audio.shape[0],368):
    start = x
    end = x + 368
    sock.sendto(audio[start:end].tobytes(), (UDP_IP, UDP_PORT))
    time.sleep(0.022)


# In[ ]:





# In[ ]:




