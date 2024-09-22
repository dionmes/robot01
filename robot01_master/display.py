import threading
import requests
import time

url = '/displayaction?action='

class DISPLAY:
	def __init__(self, ip, timeout=5):
		self.ip = ip
		self.timeout = timeout

	def action(self, action, reset=0, item=0):
		
		if item > 0:
			threading.Thread( target=self.safe_http_call, args=['http://' + self.ip + url + str(action) + "&index=" + str(item)] ).start()
		else:
			threading.Thread( target=self.safe_http_call, args=['http://' + self.ip + url + str(action)] ).start()
		
		# Reset to neutral after n seconds unless 0
		if reset > 0 :
			time.sleep(reset)
			threading.Thread( target=self.safe_http_call, args=['http://' + self.ip + url + str(3)] ).start()

	def safe_http_call(self,url):
		
		try:
			requests.get(url, timeout=self.timeout)
		except Exception as e:
			print("Request - " + url + " , error : ",e)
			
