import threading
import requests
import time
import queue
import socket

import ollama

from tts_speecht5 import TTS
from display import DISPLAY
from sense import SENSE

body_action_url = "/bodyaction?action=" 
debug = False

LLM_EXPRESSION_MODEL = "gemma2:2b"

# Body actions Queue size
BODY_Q_SIZE = 4
# Minimal sleep between actions 
MINIMAL_SLEEP = 3
# Audio UDP PORT
AUDIO_UDP_PORT = 9000
# output Queue size
OUTPUT_Q_SIZE = 8

# Class robot for managing the robot motors, audio and sensors
# Uses threading for http call to make it non-blocking
class ROBOT:
	def __init__(self, ip, sense_ip, timeout=2):

		self.ip = ip
		self.timeout = timeout
		self.health_ok = False
	
		self.emotion = True

		self.output_busy = False

		# Sense engine
		self.sense = SENSE(sense_ip)

		# Display engine
		self.display = DISPLAY(self.ip)
		self.display.state(20) # Show hour glass animation
				
		# output queue
		self.output_q = queue.Queue(maxsize=OUTPUT_Q_SIZE)

		# Health update
		threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/health')]).start()

		#Queue
		self.body_q = queue.Queue(maxsize=BODY_Q_SIZE)
		
		# Start actions Queue worker
		self.robot_actions = True
		threading.Thread(target=self.bodyactions_worker, daemon=True).start()

		# Start output send worker
		self.output_worker_running = True
		threading.Thread(target=self.output_worker, daemon=True).start()

		# tts engine
		self.output_q = queue.Queue(maxsize=OUTPUT_Q_SIZE)
		self.tts_engine = TTS(self.ip, self.output_q)
		self.tts_engine.start()
		

	# Generate Display worker : Actions from queue : display_q
	def bodyactions_worker(self):
		print("Robot worker started.")	
		while self.robot_actions:	
			task = self.body_q.get()
			print("Robot action : " + str(task['action']) )

			url = 'http://' + self.ip + body_action_url + str(task['action']) + "&direction=" + str(task['direction']) + "&steps=" + str(task['steps'])
	
			try:
				response = requests.get(url, timeout=self.timeout)
				self.health_ok = True
			except Exception as e:
				self.health_ok = False

				if debug:
					print("Robot01 Request - " + url + " , error : ",e)
				else:
					print("Robot01 Request error")

			time.sleep(MINIMAL_SLEEP)		
			self.body_q.task_done()			

	def bodyactions_set_state(self, running):
		
		if running:
			self.body_q = queue.Queue(maxsize=BODY_Q_SIZE)
			self.robot_actions = True
			threading.Thread(target=self.bodyactions_worker, daemon=True).start()
			print("Robot body actions enabled")
		else:
			self.robot_actions = False		
			# Stop action to robot
			url = 'http://' + self.ip + body_action_url + "13&direction=0&steps=0"
	
			try:
				response = requests.get(url, timeout=self.timeout)
				self.health_ok = True
			except Exception as e:
				self.health_ok = False
				if debug:
					print("Robot01 Request - " + url + " , error : ",e)
				else:
					print("Robot01 Request error")
	
			time.sleep(1)		
	
			print("Robot body actions stopped")				
	
	def bodyaction(self, action, direction, steps):

		task = { "action" : action, "direction" : direction, "steps" : steps } 
		try:
			# Add state to queue
			self.body_q.put_nowait(task)
		except Exception as e:
			print("Robot bodyactions queue error : ", type(e).__name__ )

	def output(self, state = -1)->bool:
		if state == -1:			
			try:
				response = requests.get('http://' + self.ip + '/audiostream')
				self.health_ok = True
				json_obj = response.json()
				return json_obj['audiostream']
				
			except Exception as e:
				print("Request - audiostatus error : ",e)
				self.health_ok = False
				
				return False
			
		elif state == 1:
			threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/audiostream?on=1')]).start()

			if not self.tts_engine.running:
				self.tts_engine.start()

			if not self.output_worker_running :
				threading.Thread(target=self.output_worker, daemon=True).start()
				self.output_q = queue.Queue(maxsize=OUTPUT_Q_SIZE)
				self.output_worker_running = True
				print("Robot output engine started")			
				
			return True
		
		elif state == 0:
			threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/audiostream?on=0' )]).start()

			if not self.tts_engine.running:
				self.tts_engine.stop()

			if self.output_worker_running:
				self.output_worker_running = False
				time.sleep(1)		
				print("Robot output engine stopped")			

		return False

	# Send output worker : Gets output action from queue : output_q
	# Checks for TTS action, include emotion response
	# sends audio (16khz mono f32le) over UDP to robot01 ip port 9000
	#
	def output_worker(self):
	
		sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)

		print("Output worker started.")
		
		while self.output_worker_running:
			
			output_action = self.output_q.get()

			if "speech" in output_action['type'] and not self.output_busy:
				self.output_started()
				
			if "text" in output_action:
				self.express_emotion(output_action['text'])
			
			if "audio" in output_action:
				audio = output_action['audio']
			
				# Send packets of max. 1472 / 4 byte sample size (2 channels)
				for x in range(0, audio.shape[0],368):
					start = x
					end = x + 368
					sock.sendto(audio[start:end].tobytes(), (self.ip, AUDIO_UDP_PORT))
					time.sleep(0.021)
					if not self.output_worker_running:
						break
	
				# Send packets of 1 byte to flush buffer on robot side
				sock.sendto(bytes(1), (self.ip, AUDIO_UDP_PORT))

			self.output_q.task_done()
			
			if self.output_q.empty():
				self.output_stopped()

	# Start output state 
	def output_started(self):
		self.output_busy = True
		if not self.sense.mic: 
			self.sense.micstreaming(0)
			
		self.display.state(23)

	# 
	def output_stopped(self):
		self.display.state(3)
		self.bodyaction(16,0,30)
		self.bodyaction(17,0,30)

		if self.sense.mic: 
			self.sense.micstreaming(1)

		self.output_busy = False

	def volume(self, set_volume = -1):
		if set_volume == -1 :			
			try:
				response = requests.get('http://' + self.ip + '/volume')
				json_obj = response.json()
				print(json_obj)
				return json_obj['volume']
				
			except Exception as e:
				print("Request - volume error : ",e)
		else:
			threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/volume?power=' + str(set_volume) )]).start()


	#
	# Robotic expression via a LLM based on sentence
	#
	def express_emotion(self,text):

		if not self.emotion:
			return
			
		if len(text) < 3:
			return
			
		prompt = """"Express yourself, 
		based on the following text respond with one of these words, and only that word:
		SAD, HAPPY, EXCITED, NEUTRAL, DIFFICULT, LOVE, DISLIKE, INTERESTED, OK or LIKE.
		The text is : """ + text

		response = ollama.chat(
			model = LLM_EXPRESSION_MODEL,
			keep_alive = -1,
			messages=[{'role': 'user', 'content': prompt}],
		)
		
		emotion =  response['message']['content']

		print("Robot expression: " + emotion)

		if "SAD" in emotion:
			self.display.action(12,5,38)
			self.bodyaction(16,0,30)
			self.bodyaction(17,0,30)
			
			return
		elif "HAPPY" in emotion:
			self.display.action(12,5,44)
			self.bodyaction(16,1,30)
			self.bodyaction(17,1,30)

			return
		elif "EXCITED" in emotion:
			self.display.action(12,3,13)
			self.bodyaction(7,1,5)

			return
		elif "DIFFICULT" in emotion:
			self.display.action(12,3,52)
			self.bodyaction(12,1,20)

			return
		elif "LOVE" in emotion:
			self.display.action(12,3,19)
			self.bodyaction(17,0,30)

		elif "LIKE" in emotion:
			self.display.action(12,3,13)
			self.bodyaction(17,0,30)

			return
		elif "DISLIKE" in emotion:
			self.display.action(12,3,42)
			self.bodyaction(17,0,30)

			return
		elif "INTERESTED" in emotion:
			self.display.action(12,3,44)
			self.bodyaction(16,1,20)
			self.bodyaction(17,0,20)

			return
		elif "OK" in emotion:
			self.display.action(12,3,7)
			self.bodyaction(16,0,20)
			self.bodyaction(17,0,20)

			return
		elif "NEUTRAL" in emotion:
			self.display.action(8,3)
			self.bodyaction(16,0,20)
			self.bodyaction(17,1,20)

			return

	def wakeupsense(self):
		threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/wakeupsense')]).start()
		print("Sense Wake up signal send.")
		
	def VL53L1X_info(self)->dict:
		json_obj = {}
		try:
			response = requests.get('http://' + self.ip + '/VL53L1X_Info')
			json_obj = response.json()

		except Exception as e:
			print("Request -  VL53L1X response error : ",e)
		
		return json_obj
	
	def BNO08X_info(self)->dict:
		json_obj = {}
		try:
			response = requests.get('http://' + self.ip + '/BNO08X_Info')
			json_obj = response.json()

		except Exception as e:
			print("Request - BNO08X response error : ",e)
		
		return json_obj

	def reset(self):
		threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/reset')]).start()
		print("Robot01 reset send")
		reset = {"reset":"ok"}
		return
		
	def erase_config(self):
		threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/eraseconfig')]).start()
		print("Robot01 erase config send")
		reset = {"erase":"ok"}
		return

	def safe_http_call(self, url):
		try:
			response = requests.get(url, timeout=self.timeout)
			self.health_ok = True
		except Exception as e:
			self.health_ok = False
			if debug:
				print("Robot01 Request - " + url + " , error : ",e)
			else:
				print("Robot01 Request error")

