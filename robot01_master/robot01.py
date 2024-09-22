import threading
import requests
import time

class ROBOT:
	def __init__(self, ip, timeout=5):
		self.ip = ip
		self.timeout = timeout

	def startaudio(self):
		threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/audiostream?on=1')])

	def stopaudio(self):
		threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/audiostream?on=0')])

	def safe_http_call(self,url):
		try:
			requests.get(url, timeout=self.timeout)
		except Exception as e:
			print("Request - " + url + " , error : ",e)
