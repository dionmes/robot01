import threading
import requests
import time
import queue

body_action_url = "/bodyaction?action=" 
debug = False

# Queue size
BODY_Q_SIZE = 4
# Minimal sleep between actions 
MINIMAL_SLEEP = 3

# Class robot for managing the robot motors, audio and sensors
# Uses threading for http call to make it non-blocking
class ROBOT:
	def __init__(self, ip, timeout=2):

		self.ip = ip
		self.timeout = timeout
		self.health_ok = False

		# Health update
		threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/health')]).start()

		#Queue
		self.body_q = queue.Queue(maxsize=BODY_Q_SIZE)
		
		# Start Queue worker
		self.robot_actions = True
		threading.Thread(target=self.handle_actions, daemon=True).start()

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
				print("Got audio volume")
				return json_obj['volume']
				
			except Exception as e:
				print("Request - volume error : ",e)
		else:
			threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/volume?power=' + str(set_volume) )]).start()
			print("Set audio volume " + str(set_volume))

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
			
		else:
			threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/audiostream?on=' + str(state) )]).start()

		if state == 1:
			return True
		else:
			return False
			
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

