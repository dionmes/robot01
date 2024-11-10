import os
from datetime import datetime
import ipaddress
import requests
import json
import base64
import time

import threading
from threading import Timer
import queue

from tts_speecht5_udp import TTS
from stt_distil_whisper import STT
from robot01 import ROBOT
from display import DISPLAY
from sense import SENSE

import ollama

# max words to tts in one go
tts_max_sentence_lenght = 20

# Ollama LLM Models
LLM_MODEL = "llama3.2"
LLM_EXPRESSION_MODEL = "gemma2:2b"

# llm url
LLM_URL = "http://localhost:11434/api/chat"

# LLM system message
LLM_SYSTEM = """Respond like you are not an AI language model or program, but respond like you are a small robot called Sappie.
You do not have to introduce yourself everytime, only when asked.
Do not make robot sounds.
Only respond with numbers in a written out format.
"""

# STT Queue size
STT_Q_SIZE = 3

# Class brain for robot01
class BRAIN:
	def __init__(self):
		# If brain is busy
		
		# Robot01 IP (with default ip)
		self.robot01_ip = "192.168.133.75"
		# Sense IP	(with default ip)
		self.robot01sense_ip = "192.168.133.226"
		# Brain status
		self.busybrain = False
		# Talking
		self.talking = False
		# Talking
		self.listening = False
		# mic is on/off
		self.mic = False
		# Audio is on/off		
		self.audio = False

		# Display engine
		self.display = DISPLAY(self.robot01_ip)
		self.display.state(18) # Show face gears animation
		
		# Robot01
		self.robot = ROBOT(self.robot01_ip)
		# tts engine
		self.tts_engine = TTS(self.robot01_ip, self)
		
		# Sense engine
		self.sense = SENSE(self.robot01sense_ip)

		# stt engine
		self.stt_q = queue.Queue(maxsize=STT_Q_SIZE)
		self.stt_engine = STT(self) #uses display for signalling speech detection

	# Start brain	
	def start(self):
		self.robot.bodyaction(16,0,30)
		self.robot.bodyaction(17,0,30)
		
		# STT
		#self.stt_engine.start()

		# enable audio
		self.setting('audio',"1")
		
		# Worker
		threading.Thread(target=self.stt_worker, daemon=True).start()
		# Show neutral face
		self.display.state(3) 

	#
	# API brain setting handler
	#
	def setting(self,item,value)->bool:
		if item in "robot01_ip robot01sense_ip audio mic":	

			if item == "robot01_ip":
				if	self.validate_ip_address(value):
					self.robot01_ip = value
					# update objects with new id
					self.robot = ROBOT(self.robot01_ip)
					self.display = DISPLAY(self.robot01_ip)
					self.tts_engine = TTS(self.robot01_ip, self)
				else:
					print("Error updating robot01_ip : ", value)

			if item == "robot01sense_ip":
				if self.validate_ip_address(value):
					self.robot01sense_ip = value
					# Sense engine update with new id
					self.sense = SENSE(self.robot01sense_ip)
				else:
					print("Error updating robot01sense_ip : ", value)

			if item == "audio":
				self.audio = True if value == "1" else False
				if self.audio:
					self.tts_engine.start()
					self.robot.startaudio()
				else:
					self.robot.stopaudio()
					self.tts_engine.stop()
					
			if item == "mic":
				self.mic = True if value == "1" else False
				if self.mic:
					self.stt_engine.start()
					self.sense.startmic()
				else:
					self.stt_engine.stop()
					self.sense.stopmic()

			print("Update " + item + " with value :", value)
			
			return True
		else:
			print("Setting " + item + " not found")
			return False


	#
	# API internal get setting handler
	#
	def get_setting(self,item)->str:
		if item == "robot01_ip":
			return self.robot01_ip

		if item == "robot01sense_ip":
			return self.robot01sense_ip

		if item == "audio":
			self.audio = self.robot.audiostatus()
			if self.audio:
				return "on"
			else:
				return "off"

		if item == "mic":
			self.mic = self.sense.micstatus()
			if self.mic:
				return "on"
			else:
				return "off"
		
		return "Not found"
			
	#
	# prompt handler
	#
	def prompt(self,text)->str:
		self.busybrain = True

		# Show gear animation in display				
		self.display.state(18)
		
		# iterate over streaming parts and construct sentences to send to tts
		message = ""
		n = 0
		
		full_response = ""
		
		stream = ollama.chat(
			model = LLM_MODEL,
			keep_alive = -1,
			messages=[{'role': 'system', 'content': LLM_SYSTEM},{'role': 'user', 'content': text}],
			stream=True,
		)
		
		for chunk in stream:  
			token = chunk['message']['content']
			full_response = full_response + token
			
			# print(token)
			if token in ".,?!...;:" or n > tts_max_sentence_lenght:
				message = message + token
				if not message.strip() == "" and len(message) > 1:
					print("From LLM: " + message)
					self.speak(message)
					# Slow down if tts queue gets larger
					time.sleep(self.tts_engine.queue_size() * 0.700)
	
				message = ""
				n = 0
			else:
				message = message + token
				n = n + 1
				
		self.busybrain = False

		return full_response
		
	# direct TTS
	def speak(self,text):
		try:
			self.tts_engine.text_q.put_nowait(text)
		except Exception as e:
			print("Text queue error : ",type(e).__name__ )

	#STT worker
	def stt_worker(self):
		print("stt queue worker started")
		while True:
			text = self.stt_q.get()[0]
			print("From STT: " + text)
			self.prompt(text)
			self.stt_q.task_done()

	#
	# Robotic expression via a LLM based on sentence
	#
	def robot_expression(self,sentence):
		expression_system = """"Express yourself in 1 word based on the user sentence with one of the following categories: 
		 SAD, HAPPY, EXCITED, NEUTRAL, DIFFICULT, LOVE, DISLIKE, INTERESTED, OK, LIKE. """
		response = ollama.chat(
			model = LLM_EXPRESSION_MODEL,
			keep_alive = -1,
			messages=[{'role': 'system', 'content': expression_system},{'role': 'user', 'content': sentence}],
		)
		
		emotion =  response['message']['content']
		if "SAD" in emotion:
			self.display.action(12,5,38)
			self.robot.bodyaction(16,0,30)
			self.robot.bodyaction(17,0,30)
			
			return
		elif "HAPPY" in emotion:
			self.display.action(12,5,44)
			self.robot.bodyaction(16,1,30)
			self.robot.bodyaction(17,1,30)

			return
		elif "EXCITED" in emotion:
			self.display.action(10,5)
			self.robot.bodyaction(7,1,5)

			return
		elif "DIFFICULT" in emotion:
			self.display.action(12,5,52)
			self.robot.bodyaction(12,1,20)

			return
		elif "LOVE" in emotion:
			self.display.action(12,5,19)
			self.robot.bodyaction(17,0,30)

		elif "LIKE" in emotion:
			self.display.action(12,5,19)
			self.robot.bodyaction(17,0,30)

			return
		elif "DISLIKE" in emotion:
			self.display.action(12,5,42)
			self.robot.bodyaction(17,0,30)

			return
		elif "INTERESTED" in emotion:
			self.display.action(13,1)
			self.robot.bodyaction(16,1,20)
			self.robot.bodyaction(17,0,20)

			return
		elif "OK" in emotion:
			self.display.action(8,1)
			self.robot.bodyaction(16,0,20)
			self.robot.bodyaction(17,0,20)

			return
		elif "NEUTRAL" in emotion:
			self.display.action(3,1)
			self.robot.bodyaction(16,0,20)
			self.robot.bodyaction(17,1,20)

			return

	# Callback function from speech synthesiser when speech output started // Do not make blocking
	# self.talking flag to prevent repeated calling
	def talking_started(self):
		self.talking = True
		self.sense.stopmic()
		# Display talking animation
		self.display.state(23)

	# Callback function from speech synthesiser when speech output stopped // Do not make blocking
	def talking_stopped(self):
		self.talking = False
		if self.mic: # If mic setting was True enable mic again
			self.sense.startmic()
		# Display face neutral
		self.display.state(3)

	# Callback function from speech to texy when listening starts // Do not make blocking
	# self.talking flag to prevent repeated calling
	def listening(self):
		self.listening = True
		# Display talking animation
		self.display.state(23)
			
	# Callback function from speech synthesiser when speech output stopped // Do not make blocking
	def listening_stopped(self):
		self.listening = False
		# Display face neutral
		self.display.state(3)

###############
############### Helper functions ############### 
###############

	# Validate IP Addresses
	def validate_ip_address(self,ip_string):
		try:
			ip_object = ipaddress.ip_address(ip_string)
			return True
		except Exception as e:
			print(e)
			return False


