import os
from flask import Flask, send_from_directory, render_template, jsonify, request, abort
import logging
from werkzeug.serving import WSGIRequestHandler

import ipaddress
import requests
import json
import threading
from threading import Timer
import queue
import base64
from datetime import datetime

from brain import BRAIN

# Flask logging
log = logging.getLogger('werkzeug')
log.setLevel(logging.WARNING)

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
	robot01_context = {"robot01_ip": brain.robot.ip, "robot01sense_ip" : brain.robot.sense.ip}

	try:
		return render_template('index.html',context=robot01_context)
	except Exception as e:
		abort(500, description=str(e))

#
# Serve any file as template
#
@app.route('/<filename>')
def serve_html(filename):

	if filename in "controlsPage.html":
		try:
			return render_template(filename,context={"audiolist": brain.audiolist}) 
		except Exception as e:
			abort(500, description=str(e))
		
	if filename in "sensePage.html":
		cam_setting_list = [ "framesize", "quality", "contrast", "brightness", "saturation", "gainceiling", "colorbar", "awb", "agc", "aec", "hmirror", "vflip",  "awb_gain", "agc_gain", "aec_value", "aec2", "dcw", "bpc", "wpc", "raw_gma", "lenc", "special_effect", "wb_mode", "ae_level" ]
		try:
			return render_template(filename,context={ "robot01sense_ip" : brain.robot.sense.ip, "cam_setting_list": cam_setting_list}) 
		except Exception as e:
			abort(500, description=str(e))
		
	if filename in "managePage.html":
		try:
			return render_template(filename,context={"robot01_ip": brain.robot.ip, "robot01sense_ip" : brain.robot.sense.ip}) 
		except Exception as e:
			abort(500, description=str(e))

	if filename.endswith('.html'):
		try:
			return render_template(filename) 
		except Exception as e:
			abort(500, description=str(e))
			
	return abort(403)
		
#
# Serve CSS files
#
@app.route('/css/<path:filename>')
def serve_css(filename):
	try:
		return send_from_directory('static/css', filename)
	except Exception as e:
		abort(500, description=str(e))

#
# Serve JavaScript files
#
@app.route('/js/<path:filename>')
def serve_js(filename):
	try:
		return send_from_directory('static/js', filename)
	except Exception as e:
		abort(500, description=str(e))

#
# Serve images
#
@app.route('/images/<path:filename>')
def serve_images(filename):
	try:
		return send_from_directory('static/images', filename)
	except Exception as e:
		abort(500, description=str(e))

###################################	 API Endpoints ##########################################


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
		abort(500, description=str(e))
	
	print("start processing")
	# Show hour glass animation in display				
	brain.robot.display.state(20)
		
	try:
		# Start the background task
		prompt_task = threading.Thread(target=brain.prompt, args=([prompt]))
		prompt_task.start()
	except Exception as e:
		print("Prompt error : ", e)
		
	api_response = { 'status': 'ok' }

	return jsonify(api_response)

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
		abort(500, description=str(e))
	
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
		abort(500, description=str(e))
	
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
		abort(500, description=str(e))
	
	api_response = { 'restart': 'ok' }

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
		abort(500, description=str(e))

	if brain.robot.ip != "":
		try:	
			# Put text in the brain queue
			brain.speak(text)

		except Exception as e:
			print("TTS error : " ,e)
			abort(500, description=str(e))
	else:
		abort(404)

	api_response = { 'status': 'ok' }

	return jsonify(api_response)

#
# Robot brain and body stop
# GET: /api/stop
#
# Return json response
#
@app.route('/api/stop', methods=['GET'])
def stop():

	brain.stop()
	api_response = { 'status': 'ok' }
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
	
	brain.robot.display.state(action, img_index, text)	

	api_response = { 'status': 'ok' }
	
	return jsonify(api_response)

#
# Image test
# POST: /api/tts {"image" : string }
#
# Return json response
#
@app.route('/api/image_test', methods=['POST'])
def image_test():
	try:
		data = request.get_json()
		image_data = data["image"]
	except Exception as e:
		print(e)
		abort(500, description=str(e))

	try:	
		# Put text in the brain queue
		brain.robot.display.imagetest(image_data)
	except Exception as e:
		print("Image test error : ",e)
		abort(500, description=str(e))

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
		abort(500, description=str(e))

	try:	
		result = brain.setting(item, value)
		api_response = { item : result }
		return jsonify(api_response)
	except Exception as e:
		print(e)
		abort(500, description=str(e))

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
		abort(500, description=str(e))

	try:	
		result = brain.get_setting(item)
		api_response = { 'item': item, "value": result }
		return jsonify(api_response)

	except Exception as e:
		print(e)
		abort(500, description=str(e))

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
		res = request.args.get('res')
		img = brain.robot.sense.capture(res)
	except Exception as e:
		print(e)
		abort(500, description=str(e))

	return img

#
# Get distance Sensor sensor readings
# GET api/distanceSensor_info
#
# Return json response
#
@app.route('/api/distanceSensor_info', methods=['GET'])
def distanceSensor_info():
	try:
		info = brain.robot.distanceSensor_info()
	except Exception as e:
		print(e)
		abort(500, description=str(e))

	return jsonify(info)

#
# Get Motion sensor readings
# GET api/motionSensor_info
#
# Return json response
#
@app.route('/api/motionSensor_info', methods=['GET'])
def motionSensor_info():
	try:
		info = brain.robot.motionSensor_info()
	except Exception as e:
		print(e)
		abort(500, description=str(e))

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
		abort(500, description=str(e))

	return "ok"

#
# Send Sense to sleep
# GET: /api/go2sleep_sense
#
# Return "ok"
#
@app.route('/api/go2sleep_sense', methods=['GET'])
def go2sleep_sense():
	try:
		brain.robot.sense.go2sleep()
	except Exception as e:
		print(e)
		abort(500, description=str(e))

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
		abort(500, description=str(e))

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
	api_response = brain.robot.sense.reset()
	return jsonify(api_response)

#
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
	api_response = brain.robot.sense.erase_config()
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
	brain.robot.sense.ip = ""
	
	api_response = { 'status': 'ok' }
	return jsonify(api_response)

#
# Robot Managed notification
# GET: /api/notification
#
# Return ""
#
@app.route('/api/notification', methods=['GET'])
def notification():

	if "message" in request.args:
		message = request.args.get('message')
	else:
		message = ""

	brain.notification(message)
	return ""

#
#
#
if __name__ == '__main__':

	# Start brain
	brain.start()
	#Flask App
	app.run(host='0.0.0.0', port=5000,threaded=True)

