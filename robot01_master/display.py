import threading
import requests
import time
import queue
import base64

# url for display action
display_action_url = '/displayaction?action='
debug = True

# Queue size
DISPLAY_Q_SIZE = 3
# Minimal sleep between actions 
MINIMAL_SLEEP = 1

# Image test bitmap byte size (128x64x8bits)
IMAGE_BMP_SIZE = 1024

# Custom exception class for image test image size
class imageTestSizeException(Exception):
	def __init__(self, message):
		super().__init__(message)  
	def __str__(self):
		return f"[Error Image bytes invalid ]: {super().__str__()}"

# Class display for driving the led display/face of robot01
# Uses threading for http call to make it non-blocking
# See below for Actions & Items
class DISPLAY:
	def __init__(self, ip, timeout=10):
		# ip of robot/display
		self.ip = ip
		# timeout of http request
		self.timeout = timeout
		# Latest state (default Neutral)
		self.latest_state = {"type": "state", "action" : 3, "reset" : 0, "img_index" : 0, "text" : ""}

		#Queue
		self.display_q = queue.Queue(maxsize=DISPLAY_Q_SIZE)
		# Start Queue worker
		threading.Thread(target=self.handle_actions, daemon=True).start()

	# Generate Display worker : Actions from queue : display_q
	def handle_actions(self):
		print("DISPLAY worker started.")	

		while True: 
			task = self.display_q.get()		
						
			if task['type'] == "state":
			
				self.latest_state = task
				url = 'http://' + self.ip + display_action_url + str(task['action'])

				# text display
				if task['action'] == 1 or task['action'] == 2 or task['action'] == 14:
					url = url + "&text=" + task['text']

				url = url + "&index=" + str(task['img_index'])
				
				self.safe_http_call(url)
										
			elif  task['type'] == "action":
				
				url = 'http://' + self.ip + display_action_url + str(task['action'])

				# text display
				if task['action'] == 1 or task['action'] == 2 or task['action'] == 14:
					url = url + "&text='" + task['text']

				url = url + "&index=" + str(task['img_index'])

				self.safe_http_call(url)
				
				reset_timer = task['reset']
				if reset_timer >= MINIMAL_SLEEP:
					time.sleep(reset_timer - MINIMAL_SLEEP)
					
			time.sleep(MINIMAL_SLEEP)
				
			self.display_q.task_done()

			# Reset to latest state
			if self.display_q.empty() and task['type'] == "action":
				self.display_q.put_nowait(self.latest_state)

	# set state of display
	def state(self, action, img_index = 0, text = ""):
		task = {"type": "state", "action" : action, "img_index" : img_index, "text" : text}
		
		try:
			# Empty the queue
			while not self.display_q.empty():
				self.display_q.get()

			# Add state to queue
			self.display_q.put_nowait(task)
		except Exception as e:
			print("Display queue error : ", type(e).__name__ )

	# set temporary state of display
	def action(self, action, reset=0, img_index = 0, text=""):
		task = {"type": "action", "action" : action, "reset" : reset, "img_index" : img_index, "text" : text}
		try:
			# Add state to queue
			self.display_q.put_nowait(task)
		except Exception as e:
			print("Display queue error : ", type(e).__name__ )

	# Image test
	# Convert imagedata with https://javl.github.io/image2cpp/ (plain bytes in hex, Remove '0x' and commas from output)
	def imagetest(self, imagedata):

		try:
			image_bytes = bytes.fromhex(imagedata)
		except Exception as e:
			print("Image test data conversion, error : ",e)
		
		# Raise the custom exception
		if len(image_bytes) > IMAGE_BMP_SIZE:
			raise imageTestSizeException("Too many image bytes. (>" + str(IMAGE_BMP_SIZE) + ")")
		if len(image_bytes) < IMAGE_BMP_SIZE:
			raise imageTestSizeException("Not enough image bytes (<" + str(IMAGE_BMP_SIZE) + ")")
			
		headers = { "Content-Type": "application/octet-stream" }
		url = 'http://' + self.ip + '/drawbmp'
		try:
			response = requests.post(url, data=image_bytes, headers=headers, timeout=self.timeout)
		except Exception as e:
			print("Display Request - " + url + " , error : ",e)
			
		return
		
	def safe_http_call(self, url):
		try:
			response = requests.get(url, timeout=self.timeout)
		except Exception as e:
			if debug:
				print("Display Request - " + url + " , error : ",e)
			else:
				print("Display Request error")



