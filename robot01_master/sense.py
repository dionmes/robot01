import threading
import requests
import time
import base64

class SENSE:
	def __init__(self, ip, timeout=5):

		self.ip = ip
		self.timeout = timeout
		self.health = False
		self.mic = False
		
		# Health update
		threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/health')]).start()
		
	def micstreaming(self, state = -1)->bool:
	
		if state == -1:
			try:
				response = requests.get('http://' + self.ip + "/control?setting=micstreaming", timeout=self.timeout)
				json_obj = response.json()
				self.health_ok = True
				self.mic = json_obj['micstreaming']

			except Exception as e:
				self.health_ok = False
				print("Request - micstatus error : ",e)
							
				self.mic = False
		else:
			threading.Thread(target=self.safe_http_call, args=['http://' + self.ip + "/control?setting=micstreaming&param=" + str(state)]).start()

			self.mic = True if state == 1 else False

		return self.mic
							
	def camstreaming(self, state = -1)->bool:
	
		if state == -1:
			try:
				response = requests.get('http://' + self.ip + "/control?setting=camstreaming", timeout=self.timeout)
				json_obj = response.json()
				self.health_ok = True
				
				return json_obj['camstreaming']
				
			except Exception as e:
				self.health_ok = False
				print("Request - camstreaming error : ",e)

				return False
			
		else:
			threading.Thread(target=self.safe_http_call, args=['http://' + self.ip + "/control?setting=camstreaming&param=" + str(state)]).start()
		
		if state == 1:
			return True
		else:
			return False

	def micgain(self, set_micgain = -1)->int:
	
		if set_micgain == -1 :			
			try:
				response = requests.get('http://' + self.ip + "/control?setting=micgain", timeout=self.timeout)
				json_obj = response.json()
				
				return json_obj['micgain']
				
			except Exception as e:
				print("Request - micgain error : ",e)
		else:
			threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/control?setting=micgain&param=' + str(set_micgain) )]).start()

		return set_micgain

	def capture(self, resolution=8)->str:
	
		image_base64 = ""
		
		try: 
			# Set resolution
			requests.get('http://' + self.ip + '/control?setting=framesize&param=' + str(resolution), timeout = self.timeout )
			self.health_ok = True
		except:
			self.health_ok = False
			Print("Setting request resolution failed")
			
		# Get image
		try:
			response  = requests.get("http://" + self.ip + '/capture', timeout = self.timeout )
			image_base64 = base64.b64encode(response.content)
			self.health_ok = True
		except Exception as e:
			self.health_ok = False
			print("Capture request failed", e)
			
		return image_base64

	def cam_resolution(self,res):
		threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/control?setting=framesize&param=' + res)]).start()
		print("Sense cam resolution send")
	
	def reset(self):
		threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/reset')]).start()
		print("Sense reset send")
	
	def erase_config(self):
		threading.Thread(target=self.safe_http_call, args=[('http://' + self.ip + '/eraseconfig')]).start()
		print("Sense erase config send")

	def safe_http_call(self,url):
		try:
			requests.get(url, timeout=self.timeout)
			self.health_ok = True
		except Exception as e:
			self.health_ok = False
			print("Request - " + url + " , error : ",e)


