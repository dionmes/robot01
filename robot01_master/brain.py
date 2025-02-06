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

from typing import List
import re

from langchain_core.tools import tool
from langchain_ollama import ChatOllama
from langchain_core.prompts import ChatPromptTemplate
from langchain_core.runnables import ConfigurableField
from langchain.agents import create_tool_calling_agent, AgentExecutor
from langchain.tools.base import StructuredTool
from pydantic import BaseModel, Field

from langchain_openai import ChatOpenAI

from typing import TypedDict

import ollama

# Ollama LLM Models
LLM_MODEL = "llama3.2"
AGENT_MODEL = "llama3.2"
VISION_MODEL = "minicpm-v"
OLLAMA_KEEP_ALIVE = -1
AGENT_TEMP = 0.1

# STT Queue size
STT_Q_SIZE = 3

CONFIG_FILE = "config.json"

# Default configuration
default_config = {
	"robot01_ip" : "",
	"sense_ip" : "",
	"llm_system":  "",
	"agent_system": "",
	"openAI_api_key": "",
	"weather_api_key": ""
}

# Max tokens to TTS
TTS_MAX_SENTENCE_LENGHT = 20

#
# Class brain for robot01
#
class BRAIN:
	def __init__(self):
	
		self.config = self.load_config()
		
		# Talking
		self.listening = False
		# mode is Chat mode / Agent mode
		self.llm_mode = "chat mode"

		# Last notification received from robot
		self.last_notification = ""
		
		# Robot01
		self.robot = ROBOT(self.config["robot01_ip"], self.config["sense_ip"])
		
		# stt engine
		self.stt_q = queue.Queue(maxsize=STT_Q_SIZE)
		self.stt_engine = STT(self) #uses display for signalling speech detection

		# Original prompt. Used in some tools called by the supervisor
		self.original_prompt = ""
		
		# Audiolist with availalble sounds		
		self.audiolist = [os.path.basename(filepath) for filepath in glob.glob("audio/*.raw")]
		
		# AI / LLM routine helpers
		self.chat_running = False
		self.chat_interrupt = False

		self.agent_running = False
		self.agent_interrupt = False
		
		self.agentInit()
		
	#
	# Start brain	
	#
	def start(self):
		self.robot.bodyaction(16,0,30)
		self.robot.bodyaction(17,0,30)
		
		# STT Worker
		threading.Thread(target=self.stt_worker, daemon=True).start()

		# Show neutral face after time to settle down
		time.sleep(5)
		self.robot.display.state(3) 

	#
	# Stop AI Agent
	#
	def stop(self):

		self.robot.bodyaction(12, 0, 0)
		
		# Cancel chat task
		self.chat_interrupt = True
		if self.chat_running:
			while self.chat_running:
				time.sleep(1)
			print("Chat task done.")

		# Cancel the Agentic task
		self.agent_interrupt = True
		if self.agent_running:
		
			while self.agent_running:
				time.sleep(1)
			print("Agentic task done.")
		
		self.robot.display.state(3)
		
	#
	# Load configuration file
	#
	def load_config(self):
		try:
			with open(CONFIG_FILE, 'r') as file:
				config = json.load(file)
				print("Configuration loaded.")
				return config
		except FileNotFoundError:
			print("Configuration not found. Loading default configuration.")
			return default_config
	
	#
	# Save configuration file
	#
	def save_config(self):
		with open(CONFIG_FILE, 'w') as file:
			json.dump(self.config, file, indent=4)
			print("Configuration saved")
			
			# Reinit of Agent with new prompt
			self.agentInit()

	#
	# Report health satus
	#
	def health_status(self):
		
		status = {}
		status["robot01"] = self.robot.health
		status["robot01_latency"] = self.robot.latency
		status["sense"] = self.robot.sense.health
		status["sense_latency"] = self.robot.sense.latency
		
		return status

	# Super simple Receive notification from robot hack, does not use queuing
	def notification(self, notification):
		self.last_notification = notification;
		
	#
	# Settings handler
	#
	def setting(self,item,value)->bool:
		if item in "robot01_ip robot01sense_ip output micstreaming cam_resolution camstreaming micgain llm_mode bodyactions volume":	

			if item == "robot01_ip":
				if	self.validate_ip_address(value):
					# update running ip
					self.robot.ip = value
					self.robot.display.ip = value
					# update config ip					
					self.config["robot01_ip"] = value
					self.save_config()
				else:
					print("Error updating robot01_ip : ", value)

			if item == "robot01sense_ip":
				if self.validate_ip_address(value):
					# update running ip
					self.robot.sense.ip = value
					# update config ip					
					self.config["sense_ip"] = value
					self.save_config()
				else:
					print("Error updating robot01sense_ip : ", value)

			if item == "volume":
				self.robot.volume(value)

			if item == "output":
				self.robot.output(int(value))

			if item == "bodyactions":
				running = True if value == "1" else False
				self.robot.bodyactions_set_state(running)

			if item == "llm_mode":
				if value == "chat mode":
					self.robot.display.action(12,3,1)
					self.llm_mode = value
				if value == "agent mode":
					self.robot.display.action(12,3,0)
					self.llm_mode = value

			if item == "micstreaming":
				self.robot.sense.micstreaming(value)
				if value == 1:
					self.stt_engine.start()
				else:
					self.stt_engine.stop()

			if item == "micgain":
				self.robot.sense.micgain(value)

			if item == "camstreaming":
				self.robot.sense.camstreaming(value)

			if item == "cam_resolution":
				self.robot.sense.cam_resolution(value)

			print("Update " + item + " with value :", value)
			
			return True
		else:
			print("Setting " + item + " not found")
			return False

	#
	# Settings info handler
	#
	def get_setting(self,item)->str:
		if item in "robot01_ip robot01sense_ip output cam_resolution micstreaming camstreaming micgain llm_mode bodyactions volume":	
			if item == "robot01_ip":
				return self.robot.ip
	
			if item == "robot01sense_ip":
				return self.robot.sense.ip
	
			if item == "llm_mode":
				return self.llm_mode
				
			if item == "bodyactions":
				return self.robot.robot_actions

			if item == "volume":
				return self.robot.volume()
				
			if item == "micgain":
				return self.robot.sense.micgain()
				
			if item == "output":
				return self.robot.output()
	
			if item == "micstreaming":
				self.mic = self.robot.sense.micstreaming()
				return self.mic

			if item == "camstreaming":
				return self.robot.sense.camstreaming()

			if item == "cam_resolution":
				return self.robot.sense.cam_resolution()
								
		else:
			print("Setting " + item + " not found")
			return False

	# 
	# Play wav file via output_q
	# Convert: ffmpeg -i wawawawa.wav -f f32le -c:a pcm_f32le -ac 1 -ar 16000 wawawawa.raw
	#
	def play_audio_file(self,rawaudio_file):		
		if self.robot.output_worker_running:
			try:
				with open("audio/" + rawaudio_file, 'rb') as f:
					# Read the entire 
					audio_data = f.read()
					np_audio = np.frombuffer(audio_data, dtype=np.float32)
					self.robot.output_q.put_nowait({"type" : "file", "audio" : np_audio })

			except Exception as e:
				print("Audio queue error : ",type(e).__name__ )

	#
	# TTS output.
	# Puts text in tts_engine queue in chunks. Also used in Agentic AI as a tool
	#
	def speak(self, param1 : str):
		try:
			if self.robot.output_worker_running:
			
				words_and_delimiters = re.split(r".,?!...;:])", text)
				words_and_delimiters = list(filter(None, words_and_delimiters))
				
				message = ""
				n = 0
				for token in words_and_delimiters:
					if token in ".,?!...;:])" or n > TTS_MAX_SENTENCE_LENGHT:
						message = message + token
						if not message.strip() == "" and len(message) > 1:
							print("From LLM (Speak chunked response): " + message)
							# Put text in output queue
							try:
								self.robot.tts_engine.text_q.put(message)
							except Exception as e:
								print("Speak: TTS text_q queue error : ", type(e).__name__ )
							
						message = ""
						n = 0
					else:
						message = message + token
						n = n + 1
		
		except Exception as e:
			print("Text queue error : ",type(e).__name__ )

		return

	#
	#STT worker
	#
	def stt_worker(self):
		print("stt queue worker started")
		while True:
			text = self.stt_q.get()[0]
			print("From STT: " + text)
			self.prompt(text)
			self.stt_q.task_done()

	#
	# Validate IP Addresses
	#
	def validate_ip_address(self,ip_string):
		try:
			ip_object = ipaddress.ip_address(ip_string)
			return True
		except Exception as e:
			print(e)
			return False			

############################## AI ##############################
	
	#
	# Prompt input for chat and Agent AI
	#
	def prompt(self,text)->str:
		
		# Cancel previous actions
		self.stop();
	
		self.robot.display.state(18)

		# Chat
		if self.llm_mode == "chat mode":
			self.chat_interaction(text)
			
		# Agentic AI
		if self.llm_mode == "agent mode":
			asyncio.run(self.run_agent(text))

	# 
	# chat_interaction : Streaming chat based response 
	# Args:
	# prompt (str) : prompt 
	#
	def chat_interaction(self,prompt: str):
		
		self.chat_running = True
		self.chat_interrupt = False
				
		stream = ollama.chat(
			model = LLM_MODEL,
			keep_alive = OLLAMA_KEEP_ALIVE,
			messages=[{'role': 'system', 'content': self.config['llm_system']},{'role': 'user', 'content': prompt }],
			stream=True,
		)
		
		# iterate over streaming parts and construct sentences to send to tts
		message = ""
		n = 0
		for chunk in stream:
			token = chunk['message']['content']
			
			# print(token)
			if token in ".,?!...;:])" or n > TTS_MAX_SENTENCE_LENGHT:
				message = message + token
				if not message.strip() == "" and len(message) > 1:
					print("From LLM (chat): " + message)

					try:
						self.robot.tts_engine.text_q.put(message)
					except Exception as e:
						print("Chat: TTS text_q queue error : ", type(e).__name__ )
						
				message = ""
				n = 0
			else:
				message = message + token
				n = n + 1
				
			if self.chat_interrupt:  
				break
		
		self.chat_running = False
		return

	# 
	# run_agent : Agentic AI. Run with async.run
	# Args:
	# prompt (str) : prompt 
	#
	async def run_agent(self,prompt: str):

		self.agent_running = True
		self.agent_interrupt = False

		async for event in self.supervisor_agent_executor.astream_events({"input": prompt}, version="v1" ):
			if self.agent_interrupt:
				print("Agent interrupted!!!")
				break;

		self.agent_running = False

	#
	# Agent initialization of (global)executor:
	# self.supervisor_agent_executor
	#
	def agentInit(self):
	
		# Langchain prompt
		supervisor_prompt_template = ChatPromptTemplate.from_messages([
		("system", self.config["agent_system"]),
		("human", "The question or assignment is : {input}. After calling needed tools to respond to the request, output the result with the speak tool"), 
		("placeholder", "{agent_scratchpad}")])
		
		# Langchain tools
		
		# Input schema for int
		class toolIntInput(BaseModel):
			param1: int
		# Input schema for string
		class toolStrInput(BaseModel):
			param1: str

		toolSpeak = StructuredTool.from_function(func=self.speak, name="Speak",  description="""
		Use this tool to output your result. Convert any digits in the result to fully spelled out words.
		As Robot Sappie you are able to use this tool enables the TTS engine allowing Sappie to speak. 
		For example: Use this whenever required to communicate. You can also use this tool for intermediate outputs or singing.
		The function takes your response as input. 
		""", args_schema=toolStrInput)

		toolCurrentHeading = StructuredTool.from_function(func=self.current_heading, name="currentHeading", description="""
		Use this tool can to get the current heading robot Sappie is looking towards.
		When walking forward this is the heading/direction of robot Sappie.
		The heading is in degrees from 180 to -180. Heading 0 is North. Negative is left/west/counter clockwise. Positive is right/east/clockwise. """)
		
		toolDescribeView = StructuredTool.from_function(func=self.describe_view, name="DescribeView", description="""
		Use this tool to see or look what is in front of robot Sappie.
		It returns a description of the environment and a list of what robot Sappie can currently see.
		The tool takes as input any specific details about what to see.
		Use the exact description of this tool, do no add or change anything to the response.""", args_schema=toolStrInput)
		
		toolFindInView = StructuredTool.from_function(func=self.find_in_view, name="FindInView",  description="""
		Use this tool to discover the given subject in front of you as Robot Sappie.
		This tool provides the ability to detect someone, something or an activity.
		The tool will return a positive or negative response based on the question.
		The tool takes as input a description of what to identify, the subject.
		""", args_schema=toolStrInput)

		toolcurrentDateAndTime = StructuredTool.from_function(func=self.current_time_and_date, name="currentDateAndTime",  description="""
		Use this tool to get both the current date and the current time.
		""")

		toolWeatherForecast = StructuredTool.from_function(func=self.weather_forecast, name="weatherForecast",  description="""
		Us this tool to get the current weather and the weather forecast.
		It will provide the current weather, the weather forecast of tomorrow, and the weather forecast for the day after tomorrow. 
		No other weather information besides these days is available. """)
		
		toolWalkForward = StructuredTool.from_function(func=self.walk_forward, name="WalkForward", description=
		"""
		Use this tool to let the robot walk forward.
		The function takes as input a number (integer) with the amount of steps to walk forward with a maximum of 100 steps.
		""",args_schema=toolIntInput)

		toolWalkBackward = StructuredTool.from_function(func=self.walk_backward, name="WalkBackward", description=
		"""
		Use this tool to let the robot walk backwards.
		The function takes as input a number (integer) with the amount of steps to walk backwards with a maximum of 50 steps.
		""",args_schema=toolIntInput)

		toolTurn = StructuredTool.from_function(func=self.turn, name="Turn", description=
		"""
		Use this tool to turn the robot towards the specified heading in degrees.
		The heading is in degrees from -180 degrees to 180 degrees.
		A negative number of degrees is left/counter clockwise and a positive number of degrees right/clockwise.
		Heading 0 is North. Heading 180 is South.
		The function takes as input the desired heading in degrees as a number (integer).
		""",args_schema=toolIntInput)

		toolShake = StructuredTool.from_function(func=self.shake, name="shake", description=
		"""
		Use this tool to shake the robots body.
		The function takes as input a number (integer) with the amount of shakes to do with a maximum of 10 steps.
		""",args_schema=toolIntInput)

		toolMoveRightLowerArm = StructuredTool.from_function(func=self.move_right_lower_arm, name="MoveRightLowerArm", description=
		"""
		Use this tool to move the robots lower right arm up or down.
		Use direction 1 to move the arm up, Use direction 0 to move the arm down.
		The function takes as input a number (integer) for the direction.
		""",args_schema=toolIntInput)

		toolMoveLeftLowerArm = StructuredTool.from_function(func=self.move_left_lower_arm, name="MoveRightLowerArm", description=
		"""
		Use this tool to move the robots lower left arm up or down.
		Use direction 1 to move the arm up, Use direction 0 to move the arm down.
		The function takes as input a number (integer) for the direction.
		""",args_schema=toolIntInput)

		toolMoveRightUpperArm = StructuredTool.from_function(func=self.move_right_upper_arm, name="MoveRightUpperArm", description=
		"""
		Use this tool to move the robots upper right arm up or down.
		Use direction 1 to move the arm up, Use direction 0 to move the arm down.
		The function takes as input a number (integer) for the direction.
		""",args_schema=toolIntInput)
		
		toolMoveLeftUpperArm = StructuredTool.from_function(func=self.move_left_upper_arm, name="MoveLeftUpperArm", description=
		"""
		Use this tool to move the robots upper left arm up or down.
		Use direction 1 to move the arm up, Use direction 0 to move the arm down.
		The function takes as input a number (integer) for the direction.
		""",args_schema=toolIntInput)
		
		llm_tools = [
			toolSpeak, toolCurrentHeading, toolDescribeView, toolFindInView, toolcurrentDateAndTime, toolWeatherForecast,
			toolWalkForward,toolWalkBackward,toolTurn,toolShake,toolMoveRightLowerArm,toolMoveLeftLowerArm,
			toolMoveRightUpperArm, toolMoveLeftUpperArm
			
		]
		
		# Langchain llm
		supervisor_llm = ChatOllama(
			model=AGENT_MODEL,
			temperature=AGENT_TEMP,
			keep_alive = OLLAMA_KEEP_ALIVE,
		)
		
		openAI_LLM = ChatOpenAI(
			model="gpt-4o-2024-08-06",
			temperature=AGENT_TEMP,
			max_tokens=None,
			timeout=None,
			max_retries=2,
			api_key=self.config["openAI_api_key"]
			)
		
		# Langchain agent executor init
		supervisor_agent = create_tool_calling_agent(openAI_LLM, llm_tools, supervisor_prompt_template)
		self.supervisor_agent_executor = AgentExecutor(agent=supervisor_agent, tools=llm_tools, 
										max_iterations=999999,max_execution_time=999999, 
										return_intermediate_value=True, verbose=True)


############################## Agentic Tools ##############################
		
	#
	# Current Heading tool
	#
	def current_heading(self)->str:
		motionsensor = self.robot.motionSensor_info()
		heading = motionsensor['yaw']
		return "Current heading is " + str(heading) + " degrees."
		
	#
	# describe_view tool
	#
	def describe_view(self)->str :		
		self.robot.display.state(13)
		print("Tool : describe_view.")
		image = self.robot.sense.capture()
		self.robot.display.state(20)

		if image=="" : 
			description = "Robot Sappie sees nothing."
			print("No image")
		else:
			description = ollama.generate(
				model = VISION_MODEL,
				keep_alive = OLLAMA_KEEP_ALIVE,
				prompt = """
				Limit any interpretation in your respond, be concise. 
				Do not describe any person/human as old or worn, describe them in a flattering and positive way. 
				""",
				images = [image]			 
			)
			if description['response'] == "":
				description = "Robot Sappie sees nothing."
			else:
				description = description['response']
		
		print(description)
		self.robot.display.state(3)
		return description

	#
	# find_in_view tool
	#
	def find_in_view(self, param1: str)->str:
		self.robot.display.state(13)
		print("Tool : find_in_view.")
		image = self.robot.sense.capture()
		self.robot.display.state(20)

		if image=="" : 
			description = "Robot Sappie sees nothing."
			print("No image")
		else:
			description = ollama.generate(
				model = VISION_MODEL,
				keep_alive = -1,
				prompt = "Do you see a " + param1 + " in the image. Give a short answer.",
				images = [image]			 
			)

			if description['response'] == "":
				description = "Robot Sappe sees nothing."
			else:
				description = description['response']
		
		print("VISION MODEL : " + description)		
		self.robot.display.state(3)
		return description
		
	#
	# Current date and time tool
	#
	def current_time_and_date(self)->str :
		print("Tool : DateTime.")
		now = datetime.now()	
		# Format the current date and time as a string
		output = "The current date is " + now.strftime("%A %Y-%m-%d") + "."
		output = output + "The current time is " +  now.strftime("%H:%M") + "."
		print(output)
		return output

	#
	# Current Weather and weather forecast tool
	#
	def weather_forecast(self)->str :		
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

	#
	# Walk forward tool
	#
	def walk_forward(self, param1: int):
		print("Tool : walk forward " + str(param1))
		self.robot.bodyaction(14,0,param1)
		result = ""
		
		while not "walking" in result:
			time.sleep(1)
			result = self.last_notification

		if "walking_stopped" in result:
			result = "Stopped by request."

		if "walking_blocked" in result:
			self.robot.bodyaction(15,0,10)
			result = "Walking was blocked, took 10 steps backwards to clear path, turning needed."
			
		if "walking_ended" in result:
			result = "Succesfully walked " + str(param1) + " steps."

		self.last_notification = ""
		return result + ". " + self.current_heading()

	#
	# Walk backwards tool
	#
	def walk_backward(self,  param1: int):
		print("Tool : walk backwards " + str(param1))
		self.robot.bodyaction(15,0,param1)
		time.sleep(param1) # Sleep for duration of steps
		return

	#
	# Turn  tool
	#
	def turn(self, param1: int)->str:
		print("Tool : Turn " + str(param1))
		self.robot.bodyaction(10,0,param1)

		result = ""
		while not "turn" in result:
			time.sleep(1)
			result = self.last_notification
			
		if "turn_stopped" in result:
			result = "Stopped by request."

		if "turn_error" in result:
			result = "Error while turning."

		if "turn_blocked" in result:
			self.robot.bodyaction(15,0,10)
			result = "Turning was blocked, took 10 steps back to clear."

		if "walking_ended" in result:
			result = "Succesfully turned."

		self.last_notification = ""
		return result + "." + self.current_heading()

	#
	# Shake tool
	#
	def shake(self, param1: int):
		print("Tool : Shake " + str(param1))
		self.robot.bodyaction(11,0, param1)
		time.sleep(param1)
		return

	#
	# Move right lower arm tool
	#
	def move_right_lower_arm(self, param1: int):
		print("Tool : Move right lower arm  " + str(param1))
		self.robot.bodyaction(4, param1, 15)
		return

	#
	# Move left lower arm tool
	#
	def move_left_lower_arm(self, param1: int):
		print("Tool : Move left lower arm " + str(param1))
		self.robot.bodyaction(3, param1, 15)		
		return

	#
	# Move right upper arm tool
	#
	def move_right_upper_arm(self, param1: int):
		print("Tool : Move right upper " + str(param1))
		self.robot.bodyaction(2, param1, 25)
		return

	#
	# Move left upper arm tool
	#
	def move_left_upper_arm(self, param1: int):
		print("Tool : Move left upper arm " + str(param1))
		self.robot.bodyaction(1, param1, 25)
				
		return

############################## End of Agentic Tools ##############################
			
		
