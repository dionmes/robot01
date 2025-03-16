import threading
import requests
import time
import base64
from ping3 import ping, verbose_ping

# Health check interval (seconds)
HEALTH_CHECK_INTERVAL = 5

class SENSE:
	def __init__(self, ip):

		self.ip = ip
		self.mic = False
		
		# health
		self.health = False
		self.latency = 999
		self.health_error = 0

		# Health worker
		threading.Thread(target=self.health_check_worker).start()

	# Health check worker (ping robot)
	def health_check_worker(self):
		print("Sense health check worker started.")
		while True:
			try:
				self.latency = ping(self.ip)
				if self.latency is None or self.latency > 300:
					self.health_error += 1
					if self.health_error > 2:
						self.health = False
				else:
					self.health_error = 0
					self.health = True
			except Exception as e:
				print(f"Sense ping error : {e}")			
	
			time.sleep(HEALTH_CHECK_INTERVAL)		
		
	def micstreaming(self, state = -1)->bool:
		
		print("Micstreaming state : " + str(state))
	
		if state == -1:
			try:
				response = self.sense_http_call("/control?setting=micstreaming")
				json_obj = response.json()
				self.mic = json_obj['micstreaming']

			except Exception as e:
				print("Request - micstreaming error : ",e)
				self.mic = False
		else:
	
			threading.Thread(target=self.sense_http_call, args=["/control?setting=micstreaming&param=" + str(state)]).start()
			self.mic = True if state == 1 else False

		return self.mic
							
	def camstreaming(self, state = -1)->bool:
	
		if state == -1:
			try:
				response = self.sense_http_call("/control?setting=camstreaming")
				json_obj = response.json()
				return json_obj['camstreaming']
				
			except Exception as e:
				print("Request - camstreaming error : ",e)
				return False
			
		else:
			threading.Thread(target=self.sense_http_call, args=["/control?setting=camstreaming&param=" + str(state)]).start()
		
		if state == 1:
			return True
		else:
			return False

	def micgain(self, set_micgain = -1)->int:
	
		if set_micgain == -1 :			
			response = self.sense_http_call("/control?setting=micgain")
			try:
				json_obj = response.json()
				return json_obj['micgain']
				
			except Exception as e:
				print("Request - micgain error : ",e)
		else:
			threading.Thread(target=self.sense_http_call, args=[('/control?setting=micgain&param=' + str(set_micgain) )]).start()

		return set_micgain

	def capture(self, resolution=8)->str:
	
		image_base64 = ""
		try: 
			# Set resolution
			self.sense_http_call('/control?setting=framesize&param=' + str(resolution))
		except:
			print("Setting request resolution failed")
			
		# Get image
		response  = self.sense_http_call('/capture')
		try:
			image_base64 = base64.b64encode(response.content)
		except Exception as e:
			print("Capture request failed", e)
				
		return image_base64

	def cam_resolution(self, res=-1)->int:
		if res == -1:			
			response = self.sense_http_call('/control?setting=framesize&param=-1')
			try:
				json_obj = response.json()
				return json_obj['framesize']					
			except Exception as e:
				print("Request - cam_resolution error : ",e)
		else:		
			threading.Thread(target=self.sense_http_call, args=[('/control?setting=framesize&param=' + str(res))]).start()

		return res
	
	def go2sleep(self):
		threading.Thread(target=self.sense_http_call, args=[('/go2sleep')]).start()
		print("Sense go2sleep send")
	
	def reset(self):
		threading.Thread(target=self.sense_http_call, args=[('/reset')]).start()
		print("Sense reset send")
	
	def erase_config(self):
		threading.Thread(target=self.sense_http_call, args=[('/eraseconfig')]).start()
		print("Sense erase config send")

	def sense_http_call(self,url) -> any:
		if self.health:
			try:
				response = requests.get('http://' + self.ip + url, timeout=10)
				return response
			except Exception as e:
				print("Request - " + url + " , error : ",e)
		return ""

