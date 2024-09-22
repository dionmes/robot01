import threading
import requests
import time

url = '/displayaction?action='

class SENSE:
	def __init__(self, ip, timeout=5):
		self.ip = ip
		self.timeout = timeout

	def startmic(self):
		threading.Thread(target=self.safe_http_call, args=['http://' + self.ip + "/control?setting=micstreaming&param=1"]).start()
		print("Mic enabled")
		
	def stopmic(self):
		threading.Thread(target=self.safe_http_call, args=['http://' + self.ip + "/control?setting=micstreaming&param=0"]).start()
		print("Mic disabled")
	
	def capture(self, resolution=5):
		image_base64 = ""
		try: 
			# Set resolution
			requests.get('http://' + self.ip + '/control?setting=framesize&param=' + str(resolution), timeout = self.timeout )
		except:
			Print("Setting request resolution failed")
			# Get image
		
		try:
			response  = requests.get("http://" + robot01_context['robot01sense_ip'] + '/capture', timeout = self.timeout )
			image_base64 = base64.b64encode(response.content)
		except:
			print("Capture request failed")
			
		return [image_base64]

	def safe_http_call(self,url):
		
		try:
			requests.get(url, timeout=self.timeout)
		except Exception as e:
			print("Request - " + url + " , error : ",e)


