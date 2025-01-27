import threading
import requests
import time
import queue
import socket
import random
import ollama
from ping3 import ping, verbose_ping

from tts_speecht5 import TTS
from display import DISPLAY
from sense import SENSE

debug = False

LLM_EXPRESSION_MODEL = "gemma2:2b"

# Body actions Queue size
BODY_Q_SIZE = 4
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
		self.fw_distance = {};
		
		# output queue
		self.output_q = queue.Queue(maxsize=OUTPUT_Q_SIZE)

		#Queue
		self.body_q = queue.Queue(maxsize=BODY_Q_SIZE)
		
		# Start actions Queue worker
		self.robot_actions = True
		threading.Thread(target=self.bodyactions_worker, daemon=True).start()

		# Start output send worker
		self.output_worker_running = True
		threading.Thread(target=self.output_worker, daemon=True).start()

		# Health check worker
		threading.Thread(target=self.health_check_worker, daemon=True).start()

		# tts engine
		self.tts_engine = TTS(self.ip, self.output_q)
		self.tts_engine.start()
		
	# Health check worker (ping robot)
	def health_check_worker(self):
		print("Robot health check worker started.")	
		while True:
			try:
				self.latency = ping(self.ip)
				if self.latency is None or self.latency > 300:
					self.health_error += 1
					if self.health_error > 2:
						self.health = False
				else:
					self.health_error = 0
					self.health = True
			except Exception as e:
				print(f"Robot ping error : {e}")			
	
			time.sleep(HEALTH_CHECK_INTERVAL)		

	# Generate Display worker : Actions from queue : display_q
	def bodyactions_worker(self):
		print("Robot worker started.")	
		while self.robot_actions:	
			task = self.body_q.get()
			print("Robot action : " + str(task['action']) )

			url = "/bodyaction?action=" + str(task['action']) + "&direction=" + str(task['direction']) + "&value=" + str(task['value'])
			response = self.robot_http_call(url)

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
			url = "/bodyaction?action=13&direction=0&value=0"
	
			response = self.robot_http_call(url)
	
			print("Robot body actions stopped")				
	
	def bodyaction(self, action, direction, value):

		task = { "action" : action, "direction" : direction, "value" : value }
		if self.robot_actions:
			try:
				# Add state to queue
				self.body_q.put_nowait(task)
			except Exception as e:
				print("Robot bodyactions queue error : ", type(e).__name__ )

	def output(self, state = -1)->bool:
		# Get output status
		if state == -1:			
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

		# Start output
		elif state == 1:
			threading.Thread(target=self.robot_http_call, args=[('/audiostream?on=1')]).start()

			if not self.tts_engine.running:
				self.tts_engine.start()

			if not self.output_worker_running :
				self.output_worker_running = True
				threading.Thread(target=self.output_worker, daemon=True).start()
				self.output_q = queue.Queue(maxsize=OUTPUT_Q_SIZE)
				self.tts_engine.output_q = self.output_q # Reestablish reference
				print("Robot output engine started")			
				
			return True
		
		# Stio output
		elif state == 0:
			self.output_worker_running = False
			threading.Thread(target=self.robot_http_call, args=[('/audiostream?on=0' )]).start()
			self.output_q = queue.Queue(maxsize=OUTPUT_Q_SIZE)
			self.tts_engine.output_q = self.output_q # Reestablish reference
			self.tts_engine.stop()
			self.audio_socket.close()

			print("Robot output engine stopped")			
			time.sleep(1)		

		return False

	# Send output worker : Gets output action from queue : output_q
	# Checks for TTS action, include emotion response
	# sends audio (16khz mono f32le) over UDP to robot01 ip port 9000
	#
	def output_worker(self):
		print("Output worker started.")
		while self.output_worker_running:
			
			# Try to connect
			if not self.connect_tcp_audio():
				return
						
			output_action = self.output_q.get()

			if "speech" in output_action['type'] and not self.output_busy:
				self.output_started()
				
			
			if "audio" in output_action:
				audio = output_action['audio']
				# Send packets of max. 1024 / 4 byte sample size (2 channels)
				for x in range(0, audio.shape[0],256):
					start = x
					end = x + 256

					try :
						self.audio_socket.sendall(audio[start:end].tobytes())
					except Exception as e:
						print("Sending TCP audio error : " + self.ip + ":" + str(AUDIO_TCP_PORT) + " " + str(e))
						self.audio_socket.close()
						break

					time.sleep(0.015)
					if not self.output_worker_running:
						break
	
			if "text" in output_action:
				threading.Thread(target=self.express_emotion,args=[output_action['text']],daemon=True).start()
			
			self.output_q.task_done()
			
			if self.output_q.empty():
				self.output_stopped()

	# Connect to TCP audio
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


	# Callback : Started output state 
	def output_started(self):
		self.output_busy = True
		if not self.sense.mic: 
			self.sense.micstreaming(0)
			
		self.display.state(23)

	# Callback : Stopped output
	def output_stopped(self):
		self.display.state(3)
		self.bodyaction(16,0,30)
		self.bodyaction(17,0,30)

		if self.sense.mic: 
			self.sense.micstreaming(1)

		self.output_busy = False

	def volume(self, set_volume = -1):
		if set_volume == -1 :			
			response = self.robot_http_call('/volume')
			try: 
				json_obj = response.json()
			except Exception as e:
				print("Volune : ",e)
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
			
		prompt = """"Express yourself, 
		based on the following text respond with one of these words, and only that word:
		SAD, HAPPY, EXCITED, NEUTRAL, DIFFICULT, LOVE, DISLIKE, INTERESTED or LIKE.
		The text is : """ + text

		response = ollama.chat(
			model = LLM_EXPRESSION_MODEL,
			keep_alive = -1,
			messages=[{'role': 'user', 'content': prompt}],
		)
		
		emotion =  response['message']['content']

		print("Robot expression: " + emotion)

		if "SAD" in emotion:
			self.display.action(12,1,8)

			d1 = random.randint(0, 1)
			d2 = random.randint(0, 1)

			self.bodyaction(16,d1,30)
			self.bodyaction(17,d2,30)
			
			return
		elif "HAPPY" in emotion:
			self.display.action(12,1,5)
			d1 = random.randint(0, 1)
			d2 = random.randint(0, 1)

			self.bodyaction(16,d1,30)
			self.bodyaction(17,d2,30)

			return
		elif "EXCITED" in emotion:
			self.display.action(12,1,2)
			self.bodyaction(11,0,5)

			return
		elif "DIFFICULT" in emotion:
			self.display.action(12,1,9)
			self.bodyaction(12,1,20)

			return
		elif "LOVE" in emotion:
			self.display.action(12,1,6)
			d1 = random.randint(0, 1)
			self.bodyaction(17,d1,30)

		elif "LIKE" in emotion:
			self.display.action(12,1,2)
			d1 = random.randint(0, 1)
			self.bodyaction(17,d1,30)

			return
		elif "DISLIKE" in emotion:
			self.display.action(12,1,3)
			d1 = random.randint(0, 1)
			self.bodyaction(17,d1,30)

			return
		elif "INTERESTED" in emotion:
			self.display.action(12,1,9)
			d1 = random.randint(0, 1)
			d2 = random.randint(0, 1)

			self.bodyaction(16,d1,30)
			self.bodyaction(17,d2,30)

			return

		elif "NEUTRAL" in emotion:
		
			self.bodyaction(16,0,30)
			self.bodyaction(17,0,30)

			return

	def wakeupsense(self):
		threading.Thread(target=self.robot_http_call, args=[('/wakeupsense')]).start()
		print("Sense Wake up signal send.")
		
	def VL53L1X_info(self)->dict:
		response = self.robot_http_call('/VL53L1X_Info')
		try: 
			self.fw_distance = response.json()
		except Exception as e:
			print("VL53L1X_Info : ",e)
			return ""
		
		return self.fw_distance
	
	def BNO08X_info(self)->dict:
		response = self.robot_http_call('/BNO08X_Info')
		try: 
			self.motionsensor = response.json()
		except Exception as e:
			print("BNO08X_Info : ",e)
			return ""
		return self.motionsensor

	def reset(self):
		threading.Thread(target=self.robot_http_call, args=[('/reset')]).start()
		print("Robot01 reset send")
		reset = {"reset":"ok"}
		return
		
	def erase_config(self):
		threading.Thread(target=self.robot_http_call, args=[('/eraseconfig')]).start()
		print("Robot01 erase config send")
		reset = {"erase":"ok"}
		return

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
