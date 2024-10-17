import threading
import requests
import time

class ROBOT:
	def __init__(self, ip, timeout=5):
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
			response = requests.get('http://' + self.ip + '/audiostream', timeout=self.timeout)
			json_obj = response.json()
			audio = True if json_obj['audiostream'] == 1 else False

		except Exception as e:
			print("Request - audiostatus error : ",e)
			audio = False
		
		return audio

	def safe_http_call(self,url):
		try:
			response = requests.get(url, timeout=self.timeout)
		except Exception as e:
			print("Request - " + url + " , error : ",e)
