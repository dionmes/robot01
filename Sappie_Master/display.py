import threading
import requests
import time

url = '/displayaction?action='

class DISPLAY:
	def __init__(self, ip):
		self.ip = ip
	
	def action(self, action, n=0):
		
		threading.Thread(target=requests.get, args=['http://' + self.ip + url + str(action)]).start()
		
		# Reset to neutral after n seconds unless 0
		if n > 0 :
			time.sleep(n)
			threading.Thread(target=requests.get, args=['http://' + self.ip + url + str(3)]).start()

