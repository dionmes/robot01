import os
from flask import Flask, send_from_directory, render_template, jsonify, request, abort
import ipaddress
import requests
import json
import threading
from threading import Timer
import queue
import base64
from datetime import datetime

from brain import BRAIN

# Set static_folder to serve static files from 'static' directory
app = Flask(__name__, static_folder='static')
app.config['TEMPLATES_AUTO_RELOAD'] = True

# robot01 current context.

# Brain init
brain = BRAIN()

#
# Serve the index.html file from the static/html directory
#
@app.route('/')
def index():
	robot01_context = {"robot01_ip": brain.robot.ip, "robot01sense_ip" : brain.sense.ip}

	try:
		return render_template('index.html',context=robot01_context)
	except Exception as e:
		abort(404)

#
# Serve any file as template
#
@app.route('/<filename>')
def serve_html(filename):
	robot01_context = {"robot01_ip": brain.robot.ip, "robot01sense_ip" : brain.sense.ip}

	if filename in "controlsPage.html":
		try:
			return render_template(filename,context={"audiolist": brain.audiolist}) 
		
		except Exception as e:
			abort(404)
		
	if filename.endswith('.html'):
		try:
			return render_template(filename,context=robot01_context) 
		except Exception as e:
			abort(404)
			
	return abort(403)
		
#
# Serve CSS files
#
@app.route('/css/<path:filename>')
def serve_css(filename):
	try:
		return send_from_directory('static/css', filename)
	except Exception as e:
		abort(404)

#
# Serve JavaScript files
#
@app.route('/js/<path:filename>')
def serve_js(filename):
	try:
		return send_from_directory('static/js', filename)
	except Exception as e:
		abort(404)

#
# Serve images
#
@app.route('/images/<path:filename>')
def serve_images(filename):
	try:
		return send_from_directory('static/images', filename)
	except Exception as e:
		abort(404)

###############	 API Endpoints ############### 
#
# Load config
# GET: /api/load_config {config}
#
# Return json response
#
@app.route('/api/load_config', methods=['GET'])
def load_config():
	try:
		api_response = brain.config
	except Exception as e:
		print(e)
		abort(404)
	
	return jsonify(api_response)

#
# Save config
# POST: /api/save_config {config}
#
# Return json response
#
@app.route('/api/save_config', methods=['POST'])
def save_config():
	try:
		data = request.get_json()
		brain.config = data
		brain.save_config()
	except Exception as e:
		print(e)
		abort(404)
	
	api_response = { 'save': 'ok' }

	return jsonify(api_response)

#
# Restart master
# GET: /api/restart_master {"text" : string }
#
# Return json response
#
@app.route('/api/restart_master', methods=['GET'])
def restart_master():
	try:
		brain = BRAIN()
		brain.start()

	except Exception as e:
		print(e)
		abort(404)
	
	api_response = { 'restart': 'ok' }

	return jsonify(api_response)

#
# Text and image to robot post request
# POST: /api/ask_robot01 {"text" : string }
#
# Return json response
#
@app.route('/api/ask_robot01', methods=['POST'])
def ask_robot01():
	try:
		data = request.get_json()
		prompt = data["text"]
	except Exception as e:
		print(e)
		abort(404)
	
	if not brain.busybrain:
		print("start processing")
		try:
			# Start the background task
			prompt_task = threading.Thread(target=brain.prompt, args=([prompt]))
			prompt_task.start()
		except Exception as e:
			print("Prompt error : ", e)
	else:
		return abort(503)
		
	api_response = { 'status': 'ok' }

	return jsonify(api_response)

#
# Text to speech request
# POST: /api/tts {"text" : string }
#
# Return json response
#
@app.route('/api/tts', methods=['POST'])
def tts_api():
	try:
		data = request.get_json()
		text = data["text"]
	except Exception as e:
		print(e)
		abort(404)

	if brain.robot.ip != "":
		try:	
			# Put text in the brain queue
			brain.speak(text)
			api_response = { 'status': 'ok' }
		except Exception as e:
			print("TTS error : " ,e)
			abort(404)
	else:
		return abort(404)

	return jsonify(api_response)

#
# Robot body action request
# GET: /api/bodyaction?action=n&direction=n&steps=n	
#
# Return json response
#
@app.route('/api/bodyaction', methods=['GET'])
def bodyaction():

	if "action" in request.args:
		action = request.args.get('action')
	else:
		action = 0
		
	if "direction" in request.args:
		direction = request.args.get('direction')
	else:
		direction = 0

	if "steps" in request.args:
		steps = request.args.get('steps')
	else:
		steps = 0

	brain.robot.bodyaction(action,direction,steps)

	api_response = { 'status': 'ok' }

	return jsonify(api_response)

#
# Display action request
# GET: /api/displayaction?action=n&
#
# Return json response
#
@app.route('/api/displayaction', methods=['GET'])
def displayaction():

	if "action" in request.args:
		action = int(request.args.get('action'))
	else:
		action = 0
		
	if "text" in request.args:
		text = request.args.get('text')
	else:
		text = ""

	if "index" in request.args:
		img_index = int(request.args.get('index'))
	else:
		img_index = 0
	
	brain.display.state(action, img_index, text)	

	api_response = { 'status': 'ok' }
	
	return jsonify(api_response)

#
# Update brain settings
# GET: /api/setting?item={item}&value={value}
#
# Return json response
#
@app.route('/api/setting', methods=['GET'])
def setting():
	try:
		item = request.args.get('item')
		value = request.args.get('value')
	except Exception as e:
		print(e)
		abort(404)

	try:	
		result = brain.setting(item, value)
		api_response = { item : result }
		return jsonify(api_response)
	except Exception as e:
		print(e)
		abort(404)

#
# Get brain settings
# GET: /api/setting?item={item}
#
# Return json response
#
@app.route('/api/get_setting', methods=['GET'])
def get_setting():
	try:
		item = request.args.get('item')
	except Exception as e:
		print(e)
		abort(404)

	try:	
		result = brain.get_setting(item)
		api_response = { 'item': item, "value": result }
		return jsonify(api_response)

	except Exception as e:
		print(e)
		abort(404)

#
# Capture an image with the camera
# GET api/img_capture
# Return image in base 64.
#
# Return base64 string 
#
@app.route('/api/img_capture', methods=['GET'])
def img_capture():
	try:
		img = brain.sense.capture()
	except Exception as e:
		print(e)
		abort(404)

	return img

#
# Get VL53L1X sensor readings
# GET api/VL53L1X_Info
#
# Return json response
#
@app.route('/api/VL53L1X_Info', methods=['GET'])
def VL53L1X_Info():
	try:
		info = brain.robot.VL53L1X_info()
	except Exception as e:
		print(e)
		abort(404)

	return jsonify(info)

#
# Get BNO08X sensor readings
# GET api/BNO08X_Info
#
# Return json response
#
@app.route('/api/BNO08X_Info', methods=['GET'])
def BNO08X_Info():
	try:
		info = brain.robot.BNO08X_info()
	except Exception as e:
		print(e)
		abort(404)

	return jsonify(info)

#
# Wake up sense, lets main board send a wake up signal to the sense board
# GET: /api/wakeupsense
#
# Return "ok"
#
@app.route('/api/wakeupsense', methods=['GET'])
def wakeupsense():
	try:
		brain.robot.wakeupsense()
	except Exception as e:
		print(e)
		abort(404)

	return "ok"

#
# Play raw audio file
# GET api/play_audio_file/{{name}}
#
# Return "ok"
#
@app.route('/api/play_audio_file', methods=['GET'])
def play_audio_file():
	try:
		brain.play_audio_file(request.args.get('file'))
	except Exception as e:
		print(e)
		abort(404)

	return "ok"

# Robot/Sense Heath status
# GET: /api/health
#
# Return json response
#
@app.route('/api/health', methods=['GET'])
def health_status():
	api_response = brain.health_status()
	return jsonify(api_response)

#
# Robot reset
# GET: /api/robot01_reset
#
# Return json response
#
@app.route('/api/robot01_reset', methods=['GET'])
def robot01_reset():
	api_response = brain.robot.reset()
	return jsonify(api_response)

#
# Sense reset
# GET: /api/sense_reset
#
# Return json response
#
@app.route('/api/sense_reset', methods=['GET'])
def sense_reset():
	api_response = brain.sense.reset()
	return jsonify(api_response)

#
# Robot erase config
# GET: /api/robot01_eraseconfig
#
# Return json response
#
@app.route('/api/robot01_eraseconfig', methods=['GET'])
def robot01_eraseconfig():
	api_response = brain.robot.erase_config()
	return jsonify(api_response)

#
# Sense erase config
# GET: /api/sense_eraseconfig
#
# Return json response
#
@app.route('/api/sense_eraseconfig', methods=['GET'])
def sense_eraseconfig():
	api_response = brain.sense.erase_config()
	return jsonify(api_response)

#
# Clear remote ip's
# GET: /api/clearips
#
# Return json response
#
@app.route('/api/clearips', methods=['GET'])
def clear_ips():

	brain.robot.ip = ""
	brain.sense.ip = ""
	
	api_response = { 'status': 'ok' }
	return jsonify(api_response)

#
#
#
if __name__ == '__main__':

	# Start brain
	brain.start()
	#Flask App
	app.run(host='0.0.0.0', port=5000,threaded=True)

