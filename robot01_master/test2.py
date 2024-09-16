import asyncio
import time

class COUNTER:
	def __init__(self,counter=1):
		self.counter = counter
	async def send(self):
		while True:
			time.sleep(3)
			self.counter+=1
			yield self.counter
