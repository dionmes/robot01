import threading
import requests
import time
import queue
from tts_speecht5 import TTS
import socket

body_action_url = "/bodyaction?action=" 
debug = False

# Body actions Queue size
BODY_Q_SIZE = 4
# Minimal sleep between actions 
MINIMAL_SLEEP = 3
# Audio UDP PORT
AUDIO_UDP_PORT = 9000
# Audio Queue size
AUDIO_Q_SIZE = 8

# Class robot for managing the robot motors, audio and sensors
# Uses threading for http call to make it non-blocking
class ROBOT:
	def __init__(self, ip, timeout=2):

		self.ip = ip
		self.timeout = timeout
		self.health_ok = False

		# Audio queue
		self.audio_q = queue.Queue(maxsize=AUDIO_Q_SIZE)

		# Health update
		threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/health')]).start()

		#Queue
		self.body_q = queue.Queue(maxsize=BODY_Q_SIZE)
		
		# Start actions Queue worker
		self.robot_actions = True
		threading.Thread(target=self.handle_actions, daemon=True).start()

		# tts engine
		self.audio_q = queue.Queue(maxsize=BODY_Q_SIZE)
		self.tts_engine = TTS(self.ip, self.audio_q)

		# Start Audio send worker
		self.audio_running = False
		self.audio_output(1)
		

	# Generate Display worker : Actions from queue : display_q
	def handle_actions(self):
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
			threading.Thread(target=self.handle_actions, daemon=True).start()
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
			print("Robot queue error : ", type(e).__name__ )

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

	def audio_output(self, state = -1)->bool:
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

			if not self.audio_running :
				threading.Thread(target=self.sendaudio, daemon=True).start()
				self.audio_q = queue.Queue(maxsize=BODY_Q_SIZE)
				self.audio_running = True
				print("Robot audio engine started")			
				
			return True
		
		elif state == 0:
			threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/audiostream?on=0' )]).start()

			if not self.tts_engine.running:
				self.tts_engine.stop()

			if self.audio_running:
				self.audio_running = False
				time.sleep(1)		
				print("Robot audio engine stopped")			

		return False

	# Send Audio worker : Gets audio wave from queue : audio_q
	# sends audio (16khz mono f32le) over UDP to robot01 ip port 9000
	#
	def sendaudio(self):
		sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)

		print("TTS Audio over UDP worker started.")
		
		while self.audio_running:
			audio = self.audio_q.get()
			# Send packets of max. 1472 / 4 byte sample size (2 channels)
			for x in range(0, audio.shape[0],368):
				start = x
				end = x + 368
				sock.sendto(audio[start:end].tobytes(), (self.ip, AUDIO_UDP_PORT))
				time.sleep(0.021)
				if not self.audio_running:
					break

			# Send packets of 1 byte to flush buffer on robot side
			sock.sendto(bytes(1), (self.ip, AUDIO_UDP_PORT))

			self.audio_q.task_done()
			
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

