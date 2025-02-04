import numpy as np
import time
import threading
import queue
from tts_speecht5 import ROBOT_TTS_MODEL

# queue sizes
TEXT_Q_SIZE = 15

# Text to speech class for robot01 project
class TTS:	
	# init
	# ip = ip of robot01
	def __init__(self, ip, output_q):
	
		self.dest_ip = ip
		self.output_q = output_q

		# TTS_Model
		self.tts_model = ROBOT_TTS_MODEL()
		
		#Queues
		self.text_q = queue.Queue(maxsize=TEXT_Q_SIZE)
		self.running = False
		
	# Generate Audio worker : Synthesises text from queue : output_q
	# puts audio (16khz mono f32le) into output_q
	#
	def generate_speech(self):
		print("TTS Speech synthesizer worker started.")	
		while self.running:
			text = self.text_q.get()
			audio = self.tts_model.synthesize(text)
			try:
				self.output_q.put_nowait({"type" : "speech", "text" : text, "audio" : audio})
			except Exception as e:
				print("Audio queue error : ", type(e).__name__ )
			
			self.text_q.task_done()

	# returns text_q size
	def queue_size(self)->int:
		return self.text_q.qsize()
	
	# Start workers
	def start(self):
		if not self.running:
			self.running = True
			threading.Thread(target=self.generate_speech, daemon=True).start()
	
	# Stop workers
	def stop(self):
		self.running = False
		# Wait for save shutdown of threads and queues
		time.sleep(1)
