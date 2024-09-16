import threading
import requests
import time

url = '/displayaction?action='

class DISPLAY:
	def __init__(self, ip, timeout=5):
		self.ip = ip
		self.timeout = timeout

	def action(self, action, reset=0):
		
		threading.Thread( target=self.safe_http_call, args=['http://' + self.ip + url + str(action)] ).start()
		
		# Reset to neutral after n seconds unless 0
		if reset > 0 :
			time.sleep(n)
			threading.Thread( target=self.safe_http_call, args=['http://' + self.ip + url + str(3)] ).start()

	def safe_http_call(self,url):
		
		try:
			requests.get(url, timeout=self.timeout)
		except:
			print("request failed - " + url)
			
