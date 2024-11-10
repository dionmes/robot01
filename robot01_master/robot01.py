import threading
import requests
import time

body_action_url = "/bodyaction?action=" 
debug = True

# Class robot for managing the robot motors, audio and sensors
# Uses threading for http call to make it non-blocking
class ROBOT:
	def __init__(self, ip, timeout=3):
		self.ip = ip
		self.timeout = timeout

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

	def bodyaction(self, action, direction, steps):
		threading.Thread( target=self.safe_http_call, args=['http://' + self.ip + body_action_url + str(action) + "&direction=" + str(direction) + "&steps=" + str(steps)] ).start()
		print("Body action")

	def safe_http_call(self, url):
		try:
			response = requests.get(url, timeout=self.timeout)
		except Exception as e:
			if debug:
				print("Robot01 Request - " + url + " , error : ",e)
			else:
				print("Robot01 Request error")

