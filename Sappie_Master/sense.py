import threading
import requests
import time

url = '/displayaction?action='

class SENSE:
	def __init__(self, ip):
		self.ip = ip
	
	def startmic(self):
		threading.Thread(target=requests.get, args=['http://' + self.ip + "/control?setting=micstreaming=1"]).start()
	
	def stopmic(self):
		threading.Thread(target=requests.get, args=['http://' + self.ip + "/control?setting=micstreaming=0"]).start()
	
	def capture(self, resolution=5):
		# Set resolution
		requests.get('http://' + self.ip + '/control?setting=framesize&param=' + str(resolution) )
		# Get image
		response  = requests.get("http://" + sappie_context['sappiesense_ip'] + '/capture')
		image_base64 = base64.b64encode(response.content)

		return [image_base64]

