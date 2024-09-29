import os
import ipaddress
import requests
import json
import threading
from threading import Timer
import queue
import base64
from datetime import datetime
import time

from tts_speecht5_udp import TTS
from stt_distil_whisper import STT
from robot01 import ROBOT
from display import DISPLAY
from sense import SENSE

# max words to tts in one go
tts_max_sentence_lenght = 12

# Ollama LLM Model
llm_model = "robot01brain"

class BRAIN:
	def __init__(self, robot01_context ):
		# Get the user's home directory
		self.home_directory = os.path.expanduser("~")
		
		# robot01 state object, not yet defined
		self.robot01_state = {}
		# robot01 current context. IPs will be overwritten by remote devices with current ip
		self.robot01_context = robot01_context
		
		# Robot01
		self.robot = ROBOT(self.robot01_context['robot01_ip'])
		
		# Display engine
		self.display = DISPLAY(self.robot01_context['robot01_ip'])
		
		# Sense engine
		self.sense = SENSE(self.robot01_context['robot01sense_ip'])
		
		# tts engine
		self.tts_engine = TTS(self.robot01_context['robot01_ip'],self.display,self.sense)

		# stt engine
		self.stt_engine = STT(self.display) #uses display for signalling speech detection

		#
		# LLM Model
		#
		#llm context
		self.llm_context_id = "1"
		
		# llm url
		self.llm_url = "http://localhost:11434/api/chat"
				
		# Busy indicator
		self.busy = True
		
	def start(self):
		# Show face gears animation
		self.display.action(18) 

		# Start STT Engine/worker
		self.stt_engine.load_models()
		
		while not self.stt_engine.loaded:
			print("STT Model Loading")
			time.sleep(3)

		#STT worker
		threading.Thread(target=self.stt_worker, daemon=True).start()
		print("stt_worker started")
	
		# Start TTS Engine/worker
		self.tts_engine.loadModel()

		while not self.tts_engine.loaded:
			print("TTS model Loading")    
			time.sleep(3)

		self.tts_engine.start()

		# Call to enable Audio receive
		self.robot.startaudio()
		
		# Show neutral face
		self.display.action(3) 
		self.busy = False

	#
	# prompt handler
	#
	def prompt(self,text,vision = False):
		self.busy = True

		if vision:
			# Show cylon in display
			self.display.action(13)
		
		# LLM request object
		llm_obj = { \
			"model": llm_model,  \
			"keep_alive": -1, \
			"context": [self.llm_context_id], \
			"messages" : [ \
			{ "role" : "user", "content" : text } \
			]}

		if vision:
			llm_obj["messages"][1]['images'] = sense.capture()

		# Show loader in display    
		self.display.action(21)
	
		# Streaming post request
		try:
			llm_response = requests.post(self.llm_url, json = llm_obj, stream=True, timeout=60)    
			llm_response.raise_for_status()
		except Exception as e:
			print("LLM error : ",e)
			# Show gear animation in display
			self.display.action(action=12, reset=5, item=14)
			return
	
		# Show gear animation in display
		self.display.action(18)
	
		# iterate over streaming parts and construct sentences to send to tts
		message = ""
		n = 0
		
		for line in llm_response.iter_lines():
			
			body = json.loads(line)
			token = body['message']['content']
			print(token)
			if token in ".,?!...;:" or n > tts_max_sentence_lenght:
				message = message + token
				#print(message)
				if not message.strip() == "" and len(message) > 1:
					self.speak(message)
					# Slow down if tts queue gets larger
					time.sleep(self.tts_engine.queue_size() * 0.500)
	
				message = ""
				n = 0
			else:
				message = message + token
				n = n + 1
		
		if 'context' in body:
			print(body['context'])
		
		self.busy = False

	# direct TTS
	def speak(self,text):
		self.tts_engine.speak(text)
			
	#STT worker
	def stt_worker(self):
	
		self.stt_engine.start()
		
		while True:
			text = self.stt_engine.stt_q.get()[0]
			
			print(text)
			
			self.prompt(text,False)
	
			self.stt_engine.stt_q.task_done()
	

