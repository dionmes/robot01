import threading
import requests
import time
import queue

body_action_url = "/bodyaction?action=" 
debug = False

# Queue size
BODY_Q_SIZE = 3
# Minimal sleep between actions 
MINIMAL_SLEEP = 3

# Class robot for managing the robot motors, audio and sensors
# Uses threading for http call to make it non-blocking
class ROBOT:
	def __init__(self, ip, timeout=2):
		self.ip = ip
		self.timeout = timeout

		#Queue
		self.body_q = queue.Queue(maxsize=BODY_Q_SIZE)
		# Start Queue worker
		threading.Thread(target=self.handle_actions, daemon=True).start()

	# Generate Display worker : Actions from queue : display_q
	def handle_actions(self):
		print("Robot worker started.")	
		while True:	
			task = self.body_q.get()
			print("Robot action : " + str(task['action']) )

			url = 'http://' + self.ip + body_action_url + str(task['action']) + "&direction=" + str(task['direction']) + "&steps=" + str(task['steps'])
	
			try:
				response = requests.get(url, timeout=self.timeout)
			except Exception as e:
				if debug:
					print("Robot01 Request - " + url + " , error : ",e)
				else:
					print("Robot01 Request error")

			time.sleep(MINIMAL_SLEEP)		
			self.body_q.task_done()			
			
	def bodyaction(self, action, direction, steps):
		task = { "action" : action, "direction" : direction, "steps" : steps } 
		try:
			# Add state to queue
			self.body_q.put_nowait(task)
		except Exception as e:
			print("Robot queue error : ", type(e).__name__ )

	def startaudio(self):
		threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/audiostream?on=1')]).start()
		print("Audio enabled")

	def stopaudio(self):
		threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/audiostream?on=0')]).start()
		print("Audio disabled")

	def audiostatus(self)->bool:
		try:
			response = requests.get('http://' + self.ip + '/audiostream')
			json_obj = response.json()
			audio = True if json_obj['audiostream'] == 1 else False

		except Exception as e:
			print("Request - audiostatus error : ",e)
			audio = False
		
		return audio

	def safe_http_call(self, url):
		try:
			response = requests.get(url, timeout=self.timeout)
		except Exception as e:
			if debug:
				print("Robot01 Request - " + url + " , error : ",e)
			else:
				print("Robot01 Request error")

