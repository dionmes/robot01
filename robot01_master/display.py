import threading
import requests
import time
import queue

# url for display action
display_action_url = '/displayaction?action='
debug = True

# Queue size
DISPLAY_Q_SIZE = 3
# Minimal sleep between actions 
MINIMAL_SLEEP = 3

# Class display for driving the led display/face of robot01
# Uses threading for http call to make it non-blocking
# See below for Actions & Items
class DISPLAY:
	def __init__(self, ip, timeout=2):
		# ip of robot/display
		self.ip = ip
		# timeout of http request
		self.timeout = timeout
		# state (default = 3 : Neutral)
		self.display_state  = 3
		#Queue
		self.display_q = queue.Queue(maxsize=DISPLAY_Q_SIZE)
		# Start Queue worker
		threading.Thread(target=self.handle_actions, daemon=True).start()

	# Generate Display worker : Actions from queue : display_q
	def handle_actions(self):
		print("DISPLAY worker started.")	
		
		while True:	
			task = self.display_q.get()

			print("Display action : " + str(task) )
			
			if task['type'] == "state":
				self.display_state = task['action']
				url = 'http://' + self.ip + display_action_url + str(task['action'])

				try:
					requests.get(url, timeout=self.timeout)
				except Exception as e:
					if debug:
						print("Display Request - " + url + " , error : ", e)
					else:
						print("Display Request error")
					
			elif  task['type'] == "action":
			
				url = 'http://' + self.ip + display_action_url + str(task['action'])

				if task['icon'] > 0:
					url = url + "&index=" + str(task['icon'])

				try:
					requests.get(url, timeout=self.timeout)
				except Exception as e:
					if debug:
						print("Display Request - " + url + " , error : ", e)
					else:
						print("Display Request error")

			if task['reset'] < MINIMAL_SLEEP:
				sleep = MINIMAL_SLEEP
			else:
				sleep = task['reset']
				
			time.sleep(sleep)
			
			if self.display_q.empty():				
				if task['type'] == "action":
					try:
						requests.get('http://' + self.ip + display_action_url + str(self.display_state), timeout=self.timeout)
					except Exception as e:
						print("Request - " + display_action_url + " , error : ",e)
	
				time.sleep(MINIMAL_SLEEP)
					
			self.display_q.task_done()

	# set state of display
	def state(self, action, reset=0):
		task = {"type": "state", "action" : action, "reset" : reset }
		
		try:
			# Empty the queue
			while not self.display_q.empty():
				self.display_q.get()

			# Add state to queue
			self.display_q.put_nowait(task)
		except Exception as e:
			print("Display queue error : ", type(e).__name__ )

	# set temporary state of display
	def action(self, action, reset, icon = 0):
		task = {"type": "action", "action" : action, "reset" : reset, "icon" : icon } 
		try:
			# Add state to queue
			self.display_q.put_nowait(task)
		except Exception as e:
			print("Display queue error : ", type(e).__name__ )

