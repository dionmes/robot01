import threading
import time
import queue
from display import DISPLAY

from stt_distil_whisper import STT

display = DISPLAY("192.168.133.75")

stt = STT(display)

#STT worker
def stt_worker():
	while True:
		text = stt.stt_q.get()[0]
		print(text)

if __name__ == "__main__":
	stt.load_models()
	
	while not stt.loaded:
		print("Loading")
		time.sleep(3)

	stt.start()
	#stt worker
	threading.Thread(target=stt_worker, daemon=True).start()

	time.sleep(999)
