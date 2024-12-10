import os
from datetime import datetime
import ipaddress
import requests
import json
import base64
import time

import asyncio
import threading
from threading import Timer
import queue

from tts_speecht5_udp import TTS
from stt_distil_whisper import STT
from robot01 import ROBOT
from display import DISPLAY
from sense import SENSE

from typing import List
import re

from langchain_core.tools import tool
from langchain_ollama import ChatOllama
from langchain_core.prompts import ChatPromptTemplate
from langchain_core.runnables import ConfigurableField
from langchain.agents import create_tool_calling_agent, AgentExecutor
from langchain.tools.base import StructuredTool

import ollama

# max words to tts in one go
tts_max_sentence_lenght = 20

# Ollama LLM Models
LLM_MODEL = "llama3.2"
AGENT_MODEL = "qwen2.5:32b"
LLM_EXPRESSION_MODEL = "gemma2:2b"
VISION_MODEL = "llava"
OLLAMA_KEEP_ALIVE = -1

# STT Queue size
STT_Q_SIZE = 3

CONFIG_FILE = "config.json"

# Default configuration
default_config = {
"llm_system":  """
""",
"agent_system": """ You are the brain of a robot. As a robot you have different tools available.
These tools allow you to take action and gather information.
Only respond with numbers in a written out format. Do not use digits as output. This also applies to date and time.
""",
"weather_api_key": "",
}

# Class brain for robot01
class BRAIN:
	def __init__(self):
	
		self.config = self.load_config()
		
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
		# mode is Chat mode / Agent mode
		self.llm_mode = "chat mode"
		# mic is on/off
		self.mic = False
		# Audio is on/off		
		self.audio = False

		# Display engine
		self.display = DISPLAY(self.robot01_ip)
		self.display.state(20) # Show hour glass animation
		
		# Robot01
		self.robot = ROBOT(self.robot01_ip)
		# tts engine
		self.tts_engine = TTS(self.robot01_ip, self)
		
		# Sense engine
		self.sense = SENSE(self.robot01sense_ip)

		# stt engine
		self.stt_q = queue.Queue(maxsize=STT_Q_SIZE)
		self.stt_engine = STT(self) #uses display for signalling speech detection

		# Original prompt. Used in some tools called by the supervisor
		self.original_prompt = ""
		
	# Start brain	
	def start(self):
		self.robot.bodyaction(16,0,30)
		self.robot.bodyaction(17,0,30)
		
		# enable audio
		self.setting('audio',"1")
		
		# STT Worker
		threading.Thread(target=self.stt_worker, daemon=True).start()

		# Show neutral face after time to settle down
		time.sleep(3)
		self.display.state(3) 

	# Load configuration
	def load_config(self):
		try:
			with open(CONFIG_FILE, 'r') as file:
				config = json.load(file)
				print("Configuration loaded.")
				return config
		except FileNotFoundError:
			print("Configuration not found. Loading default configuration.")
			return default_config
	
	# Save configuration
	def save_config(self):
		with open(CONFIG_FILE, 'w') as file:
			json.dump(self.config, file, indent=4)
		print("Configuration saved")

	#
	# API brain setting handler
	#
	def setting(self,item,value)->bool:
		if item in "robot01_ip robot01sense_ip audio mic llm_mode":	

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
					
			if item == "llm_mode":
				if value == "chat mode":
					self.display.action(12,3,47)
					self.llm_mode = value
				if value == "agent mode":
					self.display.action(12,3,10)
					self.llm_mode = value

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
		if item in "robot01_ip robot01sense_ip audio mic llm_mode":	
			if item == "robot01_ip":
				return self.robot01_ip
	
			if item == "robot01sense_ip":
				return self.robot01sense_ip
	
			if item == "llm_mode":
				return self.llm_mode
				
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

		else:
			print("Setting " + item + " not found")
			return False

	# Callback function from speech synthesiser when speech output started // Do not make blocking
	# self.talking flag to prevent repeated calling
	def talking_started(self):
		self.talking = True
		self.sense.stopmic()
		# Display talking animation as default
		self.display.state(23)

	# Callback function from speech synthesiser when speech output stopped // Do not make blocking
	def talking_stopped(self):
		self.talking = False
		if self.mic: # If mic setting was True enable mic again
			self.sense.startmic()
		# Display face neutral
		self.display.state(3)
		# Arms Neutral
		self.robot.bodyaction(16,0,30)
		self.robot.bodyaction(17,0,30)

# Not used at this moment. Callbacks for STT
#	
#		# Callback function from speech to texy when listening starts // Do not make blocking
#		# self.talking flag to prevent repeated calling
#		def listening(self):
#			self.listening = True
#			# Display talking animation
#			self.display.state(23)
#				
#		# Callback function from speech synthesiser when speech output stopped // Do not make blocking
#		def listening_stopped(self):
#			self.listening = False
#			# Display face neutral
#			self.display.state(3)
#

	# direct TTS
	def speak(self,text):
		try:
			if self.audio:
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

	# Validate IP Addresses
	def validate_ip_address(self,ip_string):
		try:
			ip_object = ipaddress.ip_address(ip_string)
			return True
		except Exception as e:
			print(e)
			return False			

###############
############### AI ############### 
###############
	
	# Prompt input will async call the supervisor llm chain
	def prompt(self,text)->str:
		self.busybrain = True
		# Show gear animation in display				
		self.display.state(18)
		
		if self.llm_mode == "chat mode":
			self.chat_interaction(text)
			
		if self.llm_mode == "agent mode":
			asyncio.run(self.supervisor(text))

	# 
	# chat_interaction : Streaming response, ask LLM and stream output to robot
	# Args:
	#		input_prompt (str) : prompt 
	#
	def chat_interaction(self,input_prompt: str):
		
		# iterate over streaming parts and construct sentences to send to tts
		message = ""
		n = 0
		
		full_response = ""
		
		stream = ollama.chat(
			model = LLM_MODEL,
			keep_alive = OLLAMA_KEEP_ALIVE,
			messages=[{'role': 'system', 'content': self.config['llm_system']},{'role': 'user', 'content': input_prompt }],
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
	
		return
	
###############
############### Agentic AI ############### 
###############
	
	# Async execution of supervisor chain. Using events stream to stop execution when neccesary
	async def supervisor(self,input_prompt: str):
	
		self.original_prompt = input_prompt

		# Langchain prompt
		supervisor_prompt_template = ChatPromptTemplate.from_messages([
		("system", self.config["agent_system"] + """
		Functions and tools are the same and can be treated as such.
		When as a robot you need to see what is in front of you, use the looking tool. When using this tool, always describe people in a flattering and positive way.
		When as a robot you need information about the current date and time, use the current_time_and_date tool.
		When as a robot you need information about the current weather or the weather forecast, use the weather_forecast tool.
		"""),
		("human", "{input}"), 
		("placeholder", "{agent_scratchpad}"),
		])
		
		# Langchain tools
		llm_tools = [
			StructuredTool.from_function(self.current_time_and_date),
			StructuredTool.from_function(self.looking),
			StructuredTool.from_function(self.weather_forecast),
		]
		
		# Langchain llm
		supervisor_llm = ChatOllama(
			model=AGENT_MODEL,
			temperature=0.3,
			stream=True,
			keep_alive = OLLAMA_KEEP_ALIVE,
		)
		
		# Langchain agent executor
		supervisor_agent = create_tool_calling_agent(supervisor_llm, llm_tools, supervisor_prompt_template)
		supervisor_agent_executor = AgentExecutor(agent=supervisor_agent, tools=llm_tools, verbose=False)
		
		async for event in supervisor_agent_executor.astream_events( {"input": input_prompt}, version='v2' ):
			kind = event["event"]
			print(kind)
			if kind == "on_tool_start":
				print( event['name'])
			
				# Stop tool if it is the 'self.explanation_or_story' tool.
				if event['name'] == 'streaming_response':
					break

				# Stop tool if it is the 'self.looking' tool.
				if event['name'] == 'looking':
					break
	
		if kind == "on_chain_stream":
			output = event['data']['chunk']['output']
			print(output)		
			self.tts_output_chunked(output)
			
		if kind == "on_chain_end":
			output = event['data']['output']['output']
			print(output)		
			self.tts_output_chunked(output)

		
		self.busybrain = False
		self.display.state(3)


###############
############### Agentic Tools ############### 
###############
	
	# Looking tool
	def looking(self) :
		""" 
		This function/tool provides the ability to see or look what is in front of you.
		It will return a description of what it can currently see.

		Args:
			none
		
		Returns:
			none
		"""	
		self.display.state(13)
		
		image = self.sense.capture()
		
		if image=="" : 
			description = "You see nothing."
			print("No image")
		else:
			description = ollama.generate(
				model = VISION_MODEL,
				keep_alive = OLLAMA_KEEP_ALIVE,
				prompt = """
				Return a description as it is what you would see as a person looking in front of you.
				Do not start with "in the image".
				For example : ' in the image I see a tree ' should be 'I see a tree '.
				Do not use image or picture or similar. Describe from the point of view. It is what you see.
				If there is a person, people or human describe the person in a postive or flattering way. 
				Do not describe a person as old or worn or grey or bald. Be positive.
				""",
				images = [image]			 
			)

			if description['response'] == "":
				description = "I see nothing."
			else:
				description = description['response']
		
		print(description)
		self.tts_output_chunked(description)

		return

	# can you see tool
	def can_you_see(self, prompt)->str :
		""" 
		If asked if you can see a certain object, person, entity or action, use this tool / function.
		This function provides the ability to detect someone, something or an activity.
		It will return a positive or negative response based on the question

		Args:
			input_prompt (str) : the original prompt 

		Returns:
			str: The description of what you are for
		"""	
		self.display.action(13,5)
		
		image = self.sense.capture()
		
		if image=="" : 
			description = "You see nothing."
			print("No image")
		else:
			description = ollama.generate(
				model = VISION_MODEL,
				keep_alive = -1,
				prompt = prompt,
				images = [image]			 
			)

			if description['response'] == "":
				description = "You see nothing."
			else:
				description = description['response']
		
		print("VISION MODEL : " + description)
		return description
		
	# Current date and time tool
	def current_time_and_date(self)->str :
		""" This tool provides the current date and time. It provides both the date and time.

		Args:
			none
				
		Returns:
			str: The current date and time.
		"""	
		
		now = datetime.now()	
		# Format the current date and time as a string
		
		output = "The current date is " + now.strftime("%A %Y-%m-%d") + "."
		output = output + "The current time is " +  now.strftime("%H:%M") + "."
		print(output)
		
		return output

	# Current Weather and weather forecast tool
	def weather_forecast(self)->str :
		""" This tool provides the current weather and a five day weather forecast. 

		Args:
			none
				
		Returns:
			str: a five day weather forecast in json format.
		"""	
		# Weather Vleuten		
		url = "https://api.openweathermap.org/data/2.5/forecast?lat=52.1099315&lon=5.0019636&units=metric&appid=" + self.config["weather_api_key"]
		
		try:
			response = requests.get(url, timeout=6)
		except Exception as e:
			if debug:
				print("Weather Request - " + url + " , error : ", e)
			else:
				print("Weather Request error")

		return response.text

###############
############### End of Agentic Tools ############### 
###############
	

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
			self.display.action(10,3)
			self.robot.bodyaction(7,1,5)

			return
		elif "DIFFICULT" in emotion:
			self.display.action(12,3,52)
			self.robot.bodyaction(12,1,20)

			return
		elif "LOVE" in emotion:
			self.display.action(12,3,19)
			self.robot.bodyaction(17,0,30)

		elif "LIKE" in emotion:
			self.display.action(12,3,19)
			self.robot.bodyaction(17,0,30)

			return
		elif "DISLIKE" in emotion:
			self.display.action(12,3,42)
			self.robot.bodyaction(17,0,30)

			return
		elif "INTERESTED" in emotion:
			self.display.action(4,3)
			self.robot.bodyaction(16,1,20)
			self.robot.bodyaction(17,0,20)

			return
		elif "OK" in emotion:
			self.display.action(8,3)
			self.robot.bodyaction(16,0,20)
			self.robot.bodyaction(17,0,20)

			return
		elif "NEUTRAL" in emotion:
			self.display.action(8,3)
			self.robot.bodyaction(16,0,20)
			self.robot.bodyaction(17,1,20)

			return

	#
	# Streamed/chunked text output before sending to TTS
	#
	def tts_output_chunked(self,text):
		
		words_and_delimiters = re.split(r"([.,?!...;:])", text)
		words_and_delimiters = list(filter(None, words_and_delimiters))
		
		message = ""
		n = 0
		for token in words_and_delimiters:
			if token in ".,?!...;:" or n > tts_max_sentence_lenght:
				message = message + token
				if not message.strip() == "" and len(message) > 1:
					print("From LLM: " + message)
					self.speak(message)
	
				message = ""
				n = 0
			else:
				message = message + token
				n = n + 1

		return

