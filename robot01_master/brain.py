import os
from datetime import datetime
import ipaddress
import requests
import json
import base64
import time
import glob

import numpy as np

import asyncio
import threading
from threading import Timer
import queue

import wave

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

# Ollama LLM Models
LLM_MODEL = "llama3.2"
AGENT_MODEL = "llama3.2"
LLM_EXPRESSION_MODEL = "gemma2:2b"
VISION_MODEL = "minicpm-v"
OLLAMA_KEEP_ALIVE = -1
AGENT_TEMP = 0.1

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

ROBOT01_DEFAULT_IP = "192.168.133.75"
SENSE_DEFAULT_IP = "192.168.133.226"

# Max tokens to TTS
TTS_MAX_SENTENCE_LENGHT = 20

# Class brain for robot01
class BRAIN:
	def __init__(self):
	
		self.config = self.load_config()
		
		# Brain status
		self.busybrain = False
		# Talking
		self.talking = False
		# Talking
		self.listening = False
		# mode is Chat mode / Agent mode
		self.llm_mode = "agent mode"
		# mic is on/off
		self.mic = False

		# Display engine
		self.display = DISPLAY(ROBOT01_DEFAULT_IP)
		self.display.state(20) # Show hour glass animation
		
		# Robot01
		self.robot = ROBOT(ROBOT01_DEFAULT_IP)
		
		# Sense engine
		self.sense = SENSE(SENSE_DEFAULT_IP)

		# stt engine
		self.stt_q = queue.Queue(maxsize=STT_Q_SIZE)
		self.stt_engine = STT(self) #uses display for signalling speech detection

		# Original prompt. Used in some tools called by the supervisor
		self.original_prompt = ""
		
		# Audiolist with availalble sounds		
		self.audiolist = [os.path.basename(filepath) for filepath in glob.glob("audio/*.raw")]
		
	# Start brain	
	def start(self):
		self.robot.bodyaction(16,0,30)
		self.robot.bodyaction(17,0,30)
		
		# enable audio
		self.setting('audio',"1")
		
		# STT Worker
		threading.Thread(target=self.stt_worker, daemon=True).start()

		# Show neutral face after time to settle down
		time.sleep(5)
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

	# Save configuration
	def health_status(self):
		health = {"robot01" :  self.robot.health_ok , "sense" : self.sense.health_ok }
		return health

	#
	# API brain setting handler
	#
	def setting(self,item,value)->bool:
		if item in "robot01_ip robot01sense_ip audio micstreaming cam_resolution camstreaming micgain llm_mode bodyactions volume":	

			if item == "robot01_ip":
				if	self.validate_ip_address(value):
					# update objects with new id
					self.robot.ip = value
					self.display.ip = value
					self.robot.tts_engine.ip = value
				else:
					print("Error updating robot01_ip : ", value)

			if item == "robot01sense_ip":
				if self.validate_ip_address(value):
					self.sense.ip = value
				else:
					print("Error updating robot01sense_ip : ", value)

			if item == "volume":
				self.robot.volume(value)

			if item == "audio":
				self.robot.audio_output(int(value))
					
			if item == "bodyactions":
				running = True if value == "1" else False
				self.robot.bodyactions_set_state(running)

			if item == "llm_mode":
				if value == "chat mode":
					self.display.action(12,3,47)
					self.llm_mode = value
				if value == "agent mode":
					self.display.action(12,3,10)
					self.llm_mode = value

			if item == "micstreaming":
				self.mic = value
				self.sense.micstreaming(self.mic)
				if self.mic == 1:
					self.stt_engine.start()
				else:
					self.stt_engine.stop()

			if item == "micgain":
				self.sense.micgain(value)

			if item == "camstreaming":
				self.sense.camstreaming(value)

			if item == "cam_resolution":
				self.sense.cam_resolution(value)

			print("Update " + item + " with value :", value)
			
			return True
		else:
			print("Setting " + item + " not found")
			return False

	#
	# API internal get setting handler
	#
	def get_setting(self,item)->str:
		if item in "robot01_ip robot01sense_ip audio micstreaming camstreaming micgain llm_mode bodyactions volume":	
			if item == "robot01_ip":
				return self.robot.ip
	
			if item == "robot01sense_ip":
				return self.sense.ip
	
			if item == "llm_mode":
				return self.llm_mode
				
			if item == "bodyactions":
				return self.robot.robot_actions

			if item == "volume":
				return self.robot.volume()
				
			if item == "micgain":
				return self.sense.micgain()
				
			if item == "audio":
				return self.robot.audio_output()
	
			if item == "micstreaming":
				self.mic = self.sense.micstreaming()
				return self.mic

			if item == "camstreaming":
				return self.sense.camstreaming()

		else:
			print("Setting " + item + " not found")
			return False

	# Callback function from speech synthesiser when speech output started // Do not make blocking
	# self.talking flag to prevent repeated calling
	def talking_started(self):
		self.talking = True
		self.sense.micstreaming(0)

		# Display talking animation as default
		self.display.state(23)

	# Callback function from speech synthesiser when speech output stopped // Do not make blocking
	def talking_stopped(self):
		self.talking = False
		if self.mic: # If mic setting was True enable mic again
			self.sense.micstreaming(1)

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

	# Play wav file
	#
	# Convert: ffmpeg -i wawawawa.wav -f f32le -c:a pcm_f32le -ac 1 -ar 16000 wawawawa.raw
	#
	def play_audio_file(self,rawaudio_file):		
		if self.robot.audio_running:
			try:
				with open("audio/" + rawaudio_file, 'rb') as f:
					# Read the entire 
					audio_data = f.read()
					np_audio = np.frombuffer(audio_data, dtype=np.float32)
					self.robot.audio_q.put_nowait(np_audio)
			except Exception as e:
				print("Audio queue error : ",type(e).__name__ )

	# direct TTS
	def speak(self,text):
		try:
			if self.robot.audio_running:
				self.robot.tts_engine.text_q.put_nowait(text)
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
			if token in ".,?!...;:" or n > TTS_MAX_SENTENCE_LENGHT:
				message = message + token
				if not message.strip() == "" and len(message) > 1:
					print("From LLM (chat): " + message)
					self.speak(message)
					self.robot_expression(message)
					# Slow down if tts queue gets larger
					time.sleep(self.robot.tts_engine.queue_size() * 0.700)
	
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
			When as a robot you are asked to determine if you see an specific object, activity, environment, item or person, use the "find_in_view" tool. 
			When as a robot you need or want to describe what is in front of you, use the "describe_view" tool. This tool will provide you with a description and list what you can see as a robot.
			When as a robot you need or want information about the current date and time, use the "current_time_and_date" tool.
			When as a robot you need or want information about the current weather or the weather forecast, use the "weather_forecast" tool. When responding with the temperatures convert digits to written out numerical.
			When as a robot you need or want to move use the "walk_forward", "walk_backward", "turn_left" or "turn_right" tool with the number of steps you need to take.
			When as a robot you need or want to shake use the "shake" tool with the number of times you need to shake.
			When as a robot you need or want to move your arms use the "move_right_lower_arm", "walk_backward", "move_left_lower_arm", "move_right_upper_arm" or "move_left_upper_arm" tools, with the number of movements you need to do.
		"""),
		("human", "{input}"), 
		("placeholder", "{agent_scratchpad}"),
		])
		
		# Langchain tools
		llm_tools = [
			StructuredTool.from_function(self.find_in_view),
			StructuredTool.from_function(self.current_time_and_date),
			StructuredTool.from_function(self.describe_view),
			StructuredTool.from_function(self.weather_forecast),
			StructuredTool.from_function(self.walk_forward),
			StructuredTool.from_function(self.walk_backward),
			StructuredTool.from_function(self.turn_left),
			StructuredTool.from_function(self.turn_right),
			StructuredTool.from_function(self.shake),
			StructuredTool.from_function(self.move_right_lower_arm),
			StructuredTool.from_function(self.move_left_lower_arm),
			StructuredTool.from_function(self.move_right_upper_arm),
			StructuredTool.from_function(self.move_left_upper_arm)	
		]
		
		# Langchain llm
		supervisor_llm = ChatOllama(
			model=AGENT_MODEL,
			temperature=AGENT_TEMP,
			keep_alive = OLLAMA_KEEP_ALIVE,
		)
		
		# Langchain agent executor
		supervisor_agent = create_tool_calling_agent(supervisor_llm, llm_tools, supervisor_prompt_template)
		supervisor_agent_executor = AgentExecutor(agent=supervisor_agent, tools=llm_tools, max_iterations=999,max_execution_time=300.0, return_intermediate_steps=True, verbose=True, )
		
		output = supervisor_agent_executor.invoke({"input": input_prompt})

		response = output['output']		
		print(response)		
		self.tts_output_chunked(response)

		self.busybrain = False
		self.display.state(3)

###############
############### Agentic Tools ############### 
###############
	
	# Looking tool
	def describe_view(self,prompt = "")->str :
		""" 
		This function/tool provides the ability to see or look what is in front of you.
		The tool can refer in the response as to what you see as an image. What is in the image is actually in front of you.
		It will return a description of the environment and list of what you can currently see.
		It can also return an response of anything visible.
		
		Args:
			input_prompt (str) : if anything specific needs to be described
		
		Returns:
			str: what you see in front of you.
		"""	
		
		
		self.display.state(13)
		print("Tool : describe_view.")
		image = self.sense.capture()
		
		self.display.state(20)

		if image=="" : 
			description = "You see nothing."
			print("No image")
		else:
			description = ollama.generate(
				model = VISION_MODEL,
				keep_alive = OLLAMA_KEEP_ALIVE,
				prompt = """
				Limit any interpretation in your respond, be concise. 
				Do not describe any person/human as old or worn, describe them in a flattering way. 
				Give a always positive description of any persons that you find.
				""" + prompt,
				images = [image]			 
			)

			if description['response'] == "":
				description = "I see nothing."
			else:
				description = description['response']
		
		print(description)

		self.display.state(3)

		return description

	# can you see tool
	def find_in_view(self, prompt)->str :
		""" 
		If asked if you can see a certain object, item, person, entity or activity, use this tool / function.
		This function provides the ability to detect someone, something or an activity.
		It will return a positive or negative response based on the question.

		Args:
			input_prompt (str) : the prompt 

		Returns:
			str: if the the requested object, item, person, entity or activity is visible.
		"""	
		
		
		self.display.state(13)
		print("Tool : find_in_view.")
		
		image = self.sense.capture()

		self.display.state(20)
		
		if image=="" : 
			description = "You see nothing."
			print("No image")
		else:
			description = ollama.generate(
				model = VISION_MODEL,
				keep_alive = -1,
				prompt = "Do you see a " + prompt + " in the image: ",
				images = [image]			 
			)

			if description['response'] == "":
				description = "You see nothing."
			else:
				description = description['response']
		
		print("VISION MODEL : " + description)
		
		self.display.state(3)

		return description
		
	# Current date and time tool
	def current_time_and_date(self)->str :
		""" This tool provides the current date and time. It provides both the date and time.

		Args:
			none
				
		Returns:
			str: The current date and time.
		"""	
		
		print("Tool : DateTime.")

		now = datetime.now()	
		# Format the current date and time as a string
		
		output = "The current date is " + now.strftime("%A %Y-%m-%d") + "."
		output = output + "The current time is " +  now.strftime("%H:%M") + "."
		print(output)
		
		return output

	# Current Weather and weather forecast tool
	def weather_forecast(self)->str :
		""" This tool provides the current weather and a five day weather forecast.
		It responds with a json structure with the current weather and weather forecast in the json data.

		Args:
			none
				
		Returns:
			str: a five day weather forecast in json format.
		"""	
		
		print("Tool : weather_forecast.")

		# Weather Vleuten		
		url = "https://api.openweathermap.org/data/2.5/forecast?exclude=minutely,hourly,alerts&lat=52.1099315&lon=5.0019636&units=metric&appid=" + self.config["weather_api_key"]
		
		try:
			response = requests.get(url, timeout=6)
		except Exception as e:
			if debug:
				print("Weather Request - " + url + " , error : ", e)
			else:
				print("Weather Request error")

		json_response = response.json()
		# Format output
		
		output = "Current temperature is : " + str(round(json_response["list"][0]["main"]["temp"])) + " celsius. "
		output = output + "The current temperature feels like : " + str(round(json_response["list"][0]["main"]["feels_like"])) + " celsius. "
		output = output + "The current weather is like : " + json_response["list"][0]["weather"][0]["description"]
		output = output + "\n--------------------------------------------\n"
		output = output + "Tomorrow the temperature will be : " + str(round(json_response["list"][9]["main"]["temp"])) + " celsius. "
		output = output + "Tomorrow the temperature feels like : " + str(round(json_response["list"][9]["main"]["feels_like"])) + " celsius. "
		output = output + "Tomorrow the weather is likely going to be : " + json_response["list"][9]["weather"][0]["description"]
		output = output + "\n--------------------------------------------\n"
		output = output + "The day after tomorrow the temperature will be : " + str(round(json_response["list"][17]["main"]["temp"])) + " celsius. "
		output = output + "The day after tomorrow the temperature feels like : " + str(round(json_response["list"][17]["main"]["feels_like"])) + " celsius. "
		output = output + "The day after tomorrow the weather is likely going to be : " + json_response["list"][17]["weather"][0]["description"]

		return output

	# Walk forward tool
	def walk_forward(self, steps):
		""" This tool allows the robot to take steps in a forward direction / motions.
			Call this tool with an integer as input

		Args:
			steps (int) : the number of steps to take 
				
		Returns:
			none
		"""	

		self.display.action(12,3,26)
		
		if (type(steps) == str):
			try:
				steps = int(steps)
			except:
				print("Tool :Error - cannot use input")
				return
					
		print("Tool : walk forward " + str(steps) + " steps.")
		self.robot.bodyaction(14,0,steps)

		time.sleep(steps)

		return

	# Walk backwards tool
	def walk_backward(self, steps):
		""" This tool allows the robot to take steps in a backward direction / motions.
			Call this tool with an integer as input

		Args:
			steps (int) : the number of steps to take 
				
		Returns:
			none
		"""	

		self.display.action(12,3,26)

		if (type(steps) == str):
			try:
				steps = int(steps)
			except:
				print("Tool :Error - cannot use input")
				return
			
		print("Tool : walk backwards " + str(steps) + " steps.")
		self.robot.bodyaction(15,0,steps)

		time.sleep(steps)

		return

	# Turn right tool
	def turn_right(self, steps):
		""" This tool allows the robot to take steps to turn right.
			Call this tool with an integer as input

		Args:
			steps (int) : the number of steps to take 
				
		Returns:
			none
		"""	

		self.display.action(12,3,26)
		
		if (type(steps) == str):
			try:
				steps = int(steps)
			except:
				print("Tool :Error - cannot use input")
				return
			
		print("Tool : Turn right " + str(steps) + " steps.")
		self.robot.bodyaction(10,0,steps)

		time.sleep(steps)

		return

	# Turn left tool
	def turn_left(self, steps):
		""" This tool allows the robot to take steps to turn left.
			Call this tool with an integer as input

		Args:
			steps (int) : the number of steps to take 
				
		Returns:
			none
		"""	

		self.display.action(12,3,26)


		if (type(steps) == str):
			try:
				steps = int(steps)
			except:
				print("Tool :Error - cannot use input")
				return
			
		print("Tool : Turn left " + str(steps) + " steps.")
		self.robot.bodyaction(10,1,steps)

		time.sleep(steps)

		return

	# Shake tool
	def shake(self, times):
		""" This tool allows the robot to shake its body
			Call this tool with an integer as input
		Args:
			times (int) : the number of shakes to do
				
		Returns:
			none
		"""	

		self.display.action(12,3,26)

		if (type(times) == str):
			try:
				times = int(times)
			except:
				times = 0
			
		if times < 3:
			times = 3

		print("Tool : Shake " + str(times) + " shakes.")
		self.robot.bodyaction(11,0,times)
		
		time.sleep(times)
		
		return

	# Move right lower arm tool
	def move_right_lower_arm(self, steps, direction=0):
		""" This tool allows the robot to move its lower right arm up or down.
			Use direction 1 to move the arm up, Use direction 0 to move the arm down.
			Call this tool with an integers as input
		Args:
			steps (int) : the number of steps/movements to do
			direction (int) : the direction, 1 for up, 0 for down.
		Returns:
			none
		"""	

		self.display.action(12,3,26)
		
		if (type(steps) == str):
			try:
				steps = int(steps)
			except:
				print("Tool :Error - cannot use input")
				return
	
		if steps < 5:
			steps = 40

		print("Tool : Move right lower arm  " + str(steps) + " steps.")
		self.robot.bodyaction(4,direction,steps)
				
		return

	# Move left lower arm tool
	def move_left_lower_arm(self, steps, direction=0):
		""" This tool allows the robot to move its lower left arm up or down.
			Use direction 1 to move the arm up, Use direction 0 to move the arm down.
			Call this tool with an integers as input
		Args:
			steps (int) : the number of steps/movements to do
			direction (int) : the direction, 1 for up, 0 for down.
		Returns:
			none
		"""	
		
		self.display.action(12,3,26)

		if (type(steps) == str):
			try:
				steps = int(steps)
			except:
				print("Tool :Error - cannot use input")
				return
			
		if steps < 5:
			steps = 40

		print("Tool : Move left lower arm " + str(steps) + " steps.")
		self.robot.bodyaction(3,direction,steps)
				
		return

	# Move right upper arm tool
	def move_right_upper_arm(self, steps, direction=0):
		""" This tool allows the robot to move its upper right arm up or down.
			Use direction 1 to move the arm up, Use direction 0 to move the arm down.
			Call this tool with an integers as input
		Args:
			steps (int) : the number of steps/movements to do
			direction (int) : the direction, 1 for up, 0 for down.
		Returns:
			none
		"""	
		
		self.display.action(12,3,26)

		if (type(steps) == str):
			try:
				steps = int(steps)
			except:
				print("Tool :Error - cannot use input")
				return

		if steps < 5:
			steps = 40
			
		print("Tool : Move right upper " + str(steps) + " steps.")
		self.robot.bodyaction(2,direction,steps)
		
		return


	# Move left upper arm tool
	def move_left_upper_arm(self, steps, direction=0):
		""" This tool allows the robot to move its upper left arm up or down.
			Use direction 1 to move the arm up, Use direction 0 to move the arm down.
			Call this tool with an integers as input
		Args:
			steps (int) : the number of steps/movements to do
			direction (int) : the direction, 1 for up, 0 for down.
		Returns:
			none
		"""	

		self.display.action(12,3,26)

		if (type(steps) == str):
			try:
				steps = int(steps)
			except:
				print("Tool :Error - cannot use input")
				return

		if steps < 5:
			steps = 40
			
		print("Tool : Move left upper arm " + str(steps) + " steps.")
		self.robot.bodyaction(1,direction,steps)
				
		return

		
###############
############### End of Agentic Tools ############### 
###############
	

	#
	# Robotic expression via a LLM based on sentence
	#
	def robot_expression(self,sentence):
		
		if len(sentence) < 3:
			return
		prompt = """"Express yourself, 
		based on the following sentence respond with one of these words, and only that word:
		SAD, HAPPY, EXCITED, NEUTRAL, DIFFICULT, LOVE, DISLIKE, INTERESTED, OK or LIKE.
		The sentence is : """ + sentence

		response = ollama.chat(
			model = LLM_EXPRESSION_MODEL,
			keep_alive = -1,
			messages=[{'role': 'user', 'content': prompt}],
		)
		
		emotion =  response['message']['content']

		print("Robot expression: " + emotion)

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
			self.display.action(12,3,13)
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
			self.display.action(12,3,13)
			self.robot.bodyaction(17,0,30)

			return
		elif "DISLIKE" in emotion:
			self.display.action(12,3,42)
			self.robot.bodyaction(17,0,30)

			return
		elif "INTERESTED" in emotion:
			self.display.action(12,3,44)
			self.robot.bodyaction(16,1,20)
			self.robot.bodyaction(17,0,20)

			return
		elif "OK" in emotion:
			self.display.action(12,3,7)
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
			if token in ".,?!...;:" or n > TTS_MAX_SENTENCE_LENGHT:
				message = message + token
				if not message.strip() == "" and len(message) > 1:
					print("From LLM (chunked response): " + message)
					self.speak(message)
	
				message = ""
				n = 0
			else:
				message = message + token
				n = n + 1

		return

	def safe_http_call(self, url, timeout):
		try:
			response = requests.get(url, timeout=timeout)
		except Exception as e:
			if debug:
				print("Request - " + url + " , error : ",e)
			else:
				print("Request error : " + url)

