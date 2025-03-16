import threading
import requests
import time
import queue
from queue import PriorityQueue

import socket
import random
import ollama
import numpy as np

from ping3 import ping, verbose_ping

from tts_engine import TTS
from display import DISPLAY
from sense import SENSE

debug = False

LLM_EXPRESSION_MODEL = "gemma2:2b"

# Body actions Queue size
BODY_Q_SIZE = 6
# Minimal sleep between actions 
MINIMAL_SLEEP = 1
# Audio UDP PORT
AUDIO_TCP_PORT = 9000
# output Queue size
OUTPUT_Q_SIZE = 6
# Health check interval (seconds)
HEALTH_CHECK_INTERVAL = 5

# Class robot for managing the robot motors, audio and sensors
# Uses threading for http call to make it non-blocking
#
class ROBOT:
	def __init__(self, ip, sense_ip):

		self.ip = ip
		# Prepare socket
		self.audio_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
		self.audio_socket.settimeout(5)

		self.emotion = True
		self.output_busy = False

		# health 
		self.latency = 999
		self.health = False
		self.health_error = 0
		
		# Sense engine
		self.sense = SENSE(sense_ip)

		# Display engine
		self.display = DISPLAY(self.ip)
		self.display.state(20) # Show hour glass animation
				
		# Motion sensor
		self.motionsensor = {};
		# Forward distance clearance sensor
		self.distance_sensor = {};
		
		# output queue
		self.output_q = queue.Queue(maxsize=OUTPUT_Q_SIZE)

		#Queue
		self.body_q = queue.Queue(maxsize=BODY_Q_SIZE)
		
		# Start actions Queue worker
		self.robot_actions = True
		threading.Thread(target=self.bodyactions_worker, daemon=True).start()

		# If allowed to walk
		self.agent_walking = False
		
		# Start output send worker
		self.output_worker_running = True
		threading.Thread(target=self.output_worker, daemon=True).start()

		# Health check worker
		threading.Thread(target=self.health_check_worker, daemon=True).start()

		# tts engine
		self.tts_engine = TTS(self.ip, self.output_q)
		self.tts_engine.start()
	
	#	
	# Health check worker (ping robot)
	#
	def health_check_worker(self):
		print("Robot health check worker started.")	
		while True:
			try:
				self.latency = ping(self.ip)
				if self.latency is None or self.latency > 300:
					self.health_error += 1
					if self.health_error > 2:
						self.health = False
						self.display.health = False
				else:
					self.health_error = 0
					if not self.health:
						# Restart output
						self.output(0) # Stop output
						time.sleep(1)
						self.output(1) # Start output
					self.health = True
					self.display.health = True

			except Exception as e:
				print(f"Robot ping error : {e}")			
	
			time.sleep(HEALTH_CHECK_INTERVAL)		

		print("Robot health check worker started.")	

	#
	# Bodyactions workerGenerate Process body_q queue items
	#
	def bodyactions_worker(self):
		print("Body actions worker started.")	
		while self.robot_actions:	
			task = self.body_q.get()
			if not self.robot_actions:
				break

			url = "/bodyaction?action=" + str(task['action']) + "&direction=" + str(task['direction']) + "&value=" + str(task['value'])
			print("Robot action : " + url )

			response = self.robot_http_call(url)

			time.sleep(MINIMAL_SLEEP)	
			if not self.body_q.empty():
				self.body_q.task_done()			

		print("Body actions worker stopped.")	

	#
	# Bodyactions set state of worker
	#
	def bodyactions_set_state(self, running):
		if running:
			self.body_q = queue.Queue(maxsize=BODY_Q_SIZE)
			self.robot_actions = True
			threading.Thread(target=self.bodyactions_worker, daemon=True).start()
		else:
			self.robot_actions = False
			self.body_q.put({}) # dummy value	
			# Stop action to robot
			url = "/bodyaction?action=12&direction=0&value=0"
	
			response = self.robot_http_call(url)
		
	#
	# Bodyaction : Put bodyaction in queue. Action 12 - immediate stop
	#
	def bodyaction(self, action, direction, value):
		# stop immediately
		if action == 12:
			print("Robot bodyactions immediate stop.")
			# empty queue
			self.bodyactions_set_state(False)
			url = "/bodyaction?action=12&direction=0&value=0"
			response = self.robot_http_call(url)
			time.sleep(3)
			self.bodyactions_set_state(True)
						
			return

		if self.robot_actions:
			try:
				task = { "action" : action, "direction" : direction, "value" : value }
				self.body_q.put_nowait(task)
			except Exception as e:
				print("Robot bodyactions queue error : ", type(e).__name__ )

	#
	# Get Output worker status
	#
	def get_output(self)->bool:
		response = self.robot_http_call('/audiostream')
		try: 
			json_obj = response.json()
		except Exception as e:
			print("audiostream : ",e)
			return ""
		if json_obj['audiostream'] and self.output_worker_running:
			return True
		else:
			return False

	#
	# Set/get Output worker status
	#
	def output(self, on)->bool:

		# Start output
		if on:
			threading.Thread(target=self.robot_http_call, args=[('/audiostream?on=1')]).start()

			if not self.tts_engine.running:
				self.tts_engine.start()

			if not self.output_worker_running :
				self.output_q = queue.Queue(maxsize=OUTPUT_Q_SIZE)
				self.tts_engine.output_q = self.output_q # Reestablish reference
				self.output_worker_running = True
				threading.Thread(target=self.output_worker, daemon=True).start()
				
			return True
		
		# Stio output
		else:
			self.output_worker_running = False
			self.output_q.put({}) # dummy value
			threading.Thread(target=self.robot_http_call, args=[('/audiostream?on=0' )]).start()
			self.tts_engine.stop()
			self.audio_socket.close()

			time.sleep(1)		

		return False

	#
	# Output worker : Gets output action from queue : output_q
	# Checks for TTS action, include emotion response
	# sends audio over TCP
	#
	def output_worker(self):
		print("Robot output engine started")			
		while self.output_worker_running:
			
			# Try to connect
			if not self.connect_tcp_audio():
				return
						
			output_action = self.output_q.get()
			
			if not self.output_worker_running:
				break
			
			if "audio" in output_action:
				audio = output_action['audio']
				
				# Convert to PCM16le
				audio = np.asarray(audio * 32767, dtype="<i2")

				for x in range(0, audio.shape[0],1024):
				
					if "speech" in output_action['type'] and not self.output_busy:
						self.output_started()
							
					start = x
					end = x + 1024

					try :
						self.audio_socket.sendall(audio[start:end].tobytes())
					except Exception as e:
						print("Sending TCP audio error : " + self.ip + ":" + str(AUDIO_TCP_PORT) + " " + str(e))
						self.audio_socket.close()
						break

					if not self.output_worker_running:
						break
	
			if "text" in output_action:
				print("From output worker : " + output_action['text'])
				threading.Thread(target=self.express_emotion,args=[output_action['text']],daemon=True).start()

			self.output_q.task_done()
			
			if self.output_q.empty():
				# Flush buffer with empty packets
				for i in range(0, 4):
					try :
						self.audio_socket.sendall( bytes(1024) )
					except Exception as e:
						print("Sending TCP audio error : " + self.ip + ":" + str(AUDIO_TCP_PORT) + " " + str(e))
						self.audio_socket.close()
						break
			
				self.output_stopped()

		print("Robot output engine stopped")			

	#
	# Connect TCP audio
	#
	def connect_tcp_audio(self)->bool:
		# Check if connection is open,
		try:
			self.audio_socket.getpeername()
			return True
		except OSError:
			print("TCP Audio: No connection.")
		
		self.audio_socket.close()			
		# Prepare TCP socket
		self.audio_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
		self.audio_socket.settimeout(5)
		self.audio_socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPCNT, 5) # Keep alive
		
		try :
			self.audio_socket.connect((self.ip, AUDIO_TCP_PORT))
			print("TCP Audio: Connection established.")
			return True
		except Exception as e:
			print("TCP Audio: Connection error : " + self.ip + ":" + str(AUDIO_TCP_PORT) + " " + str(e))
			self.audio_socket.close()
			return False


	#
	# Callback : Started output state 
	#
	def output_started(self):
		self.output_busy = True
		if not self.sense.mic: 
			self.sense.micstreaming(0)
			
		self.display.state(23)

	#
	# Callback : Stopped output
	#
	def output_stopped(self):
		n = 0
		while self.output_q.empty() and n < 3:
			time.sleep(1);
			n += 1
		
		if self.output_q.empty():
			self.display.state(3)
			self.bodyaction(16,0,30)
			self.bodyaction(17,0,30)
		
			if self.sense.mic: 
				self.sense.micstreaming(1)

			self.output_busy = False

	#
	# Set/Get volume
	#
	def volume(self, set_volume = -1):
		if set_volume == -1 :			
			response = self.robot_http_call('/volume')
			try: 
				json_obj = response.json()
			except Exception as e:
				print("Volume : ",e)
				return ""
			return json_obj['volume']
		else:
			threading.Thread(target=self.robot_http_call, args=[('/volume?power=' + str(set_volume) )]).start()

	#
	# Robotic expression via a LLM based on sentence
	#
	def express_emotion(self,text):

		if not self.emotion or not text:
			return
			
		if len(text) < 30:
			return
			
		prompt = """Try to match the emotion or expression in the text as much as possible with of one the following words:
		sad, happy, excited, difficult, love, like, dislike, interesting, relieved, embarrassed, fear or neutral. 
		Your results should only be that word, no explanation.
		The text is : """ + text

		response = ollama.chat(
			model = LLM_EXPRESSION_MODEL,
			keep_alive = -1,
			messages=[{'role': 'user', 'content': prompt}],
		)
		
		emotion =  response['message']['content'].lower()
		
		print("Robot expression: " + emotion)
		
		# Random motion directions
		d1 = random.randint(0, 1)
		d2 = random.randint(0, 1)

		if "sad" in emotion:
			self.display.action(12,1,8)
			self.bodyaction(16,d1,20)
			
			return
		elif "happy" in emotion:
			self.display.action(12,1,5)
			self.bodyaction(17,d2,30)

			return
		elif "excited" in emotion:
			self.display.action(12,1,2)
			self.bodyaction(11,0,4)

			return
		elif "difficult" in emotion:
			self.display.action(12,1,9)
			self.bodyaction(16,1,15)

			return
		elif "love" in emotion:
			self.display.action(12,1,6)
			self.bodyaction(17,d1,20)

		elif "like" in emotion:
			self.display.action(12,1,2)
			self.bodyaction(17,d1,20)

			return
		elif "dislike" in emotion:
			self.display.action(12,1,3)
			self.bodyaction(16,d1,20)

			return
		elif "interesting" in emotion:
			self.display.action(12,1,9)
			self.bodyaction(17,d2,15)

			return

		elif "fear" in emotion:
			self.display.action(12,1,13)
			self.bodyaction(16,d2,10)

			return

		elif "embarrassed" in emotion:
			self.display.action(12,1,12)
			self.bodyaction(17,d2,10)

			return

		elif "relieved" in emotion:
			self.display.action(12,1,11)
			self.bodyaction(17,d2,10)

			return

		elif "neutral" in emotion:
			self.bodyactions_set_state(False)
			time.sleep(2)
			self.bodyactions_set_state(True)
			self.bodyaction(16,0,30)
			self.bodyaction(17,0,30)

			return

	#
	# wakeupsense : Give signal to io pin for waking up sense device
	#
	def wakeupsense(self):
		threading.Thread(target=self.robot_http_call, args=[('/wakeupsense')]).start()
		print("Sense Wake up signal send.")
		
	#
	# distanceSensor_info : Get distance / ToF sensor info
	#
	def distanceSensor_info(self)->dict:
		response = self.robot_http_call('/distanceSensor_info')
		if self.health:	
			try: 
				self.distance_sensor = response.json()
			except Exception as e:
				print("distanceSensor_info : ",e)
				return 0
		
		return self.distance_sensor
	
	#
	# motionSensor_info : Get motion/gyro sensor data
	#
	def motionSensor_info(self)->dict:
		response = self.robot_http_call('/motionSensor_info')
		if self.health:	
			try: 
				self.motionsensor = response.json()
			except Exception as e:
				print("motionSensor_info : ",e)
				return 0
			return self.motionsensor

	#
	# Reset robot
	#
	def reset(self):
		threading.Thread(target=self.robot_http_call, args=[('/reset')]).start()
		print("Robot01 reset send")
		reset = {"reset":"ok"}
		return
		
	#
	# Erase/wipe config of robot
	#
	def erase_config(self):
		threading.Thread(target=self.robot_http_call, args=[('/eraseconfig')]).start()
		print("Robot01 erase config send")
		reset = {"erase":"ok"}
		return

	#
	# Safe http call to robot
	#
	def robot_http_call(self, url)-> any:
		if self.health:
			try:
				response = requests.get("http://" + self.ip + url, timeout=2)
				return response
			except Exception as e:
				if debug:
					print("Robot01 Request - " + url + " , error : ",e)
				else:
					print("Robot01 Request error")

		return
