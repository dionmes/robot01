import threading
import requests
import time

# url for display action
display_action_url = '/displayaction?action='
debug = True

# Class display for driving the led display/face of robot01
# Uses threading for http call to make it non-blocking
# See below for Actions & Items
class DISPLAY:
	def __init__(self, ip, timeout=2):
		# ip of robot/display
		self.ip = ip
		# timeout of http request
		self.timeout = timeout
		# default state (3=neutral)
		self.default_state = 3

	# set state of display
	def state(self, state):
		print("Display state : " + str(state))
		self.default_state = state
		threading.Thread( target=self.safe_http_call, args=['http://' + self.ip + display_action_url + str(self.default_state)] ).start()
	
	# set temporary state of display
	def action(self, action, reset, icon = 0):
		print("Display action : " + str(action) + " item : " + str(icon))
		if icon > 0:
			threading.Thread( target=self.safe_http_call, args=['http://' + self.ip + display_action_url + str(action) + "&index=" + str(icon)] ).start()
		else:
			threading.Thread( target=self.safe_http_call, args=['http://' + self.ip + display_action_url + str(action)] ).start()
		
		# Reset to neutral after n seconds unless 0
		if reset > 0 :
			threading.Thread(target=self.reset_display, args=[reset]).start()
			
	def reset_display(self,reset):
		time.sleep(reset)
		try:
			requests.get('http://' + self.ip + display_action_url + str(self.default_state), timeout=self.timeout)
		except Exception as e:
			print("Request - " + display_action_url + " , error : ",e)
	
	# make the http call to the robot/display
	def safe_http_call(self,url):
		try:
			requests.get(url, timeout=self.timeout)
		except Exception as e:
			if debug:
				print("Display Request - " + url + " , error : ", e)
			else:
				print("Display Request error")
			
