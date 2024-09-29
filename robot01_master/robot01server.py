from flask import Flask, send_from_directory, render_template, jsonify, request
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
robot01_context = {"robot01_ip":"192.168.133.75", "robot01sense_ip" : "192.168.133.226"}

# Brain init
brain = BRAIN(robot01_context)

#
# Serve the index.html file from the static/html directory
#
@app.route('/')
def index(robot01_context=brain.robot01_context):
	return render_template('index.html',context=robot01_context)
 
#
# Serve any file as template
#
@app.route('/<filename>')
def serve_html(filename,robot01_context=brain.robot01_context):
	return render_template(filename,context=robot01_context) 
#
# Serve CSS files
#
@app.route('/css/<path:filename>')
def serve_css(filename):
	return send_from_directory('static/css', filename)

#
# Serve JavaScript files
#
@app.route('/js/<path:filename>')
def serve_js(filename):
	return send_from_directory('static/js', filename)

#
# Serve images
#
@app.route('/images/<path:filename>')
def serve_images(filename):
	return send_from_directory('static/images', filename)

###############	 API Endpoints ############### 

#
# Register remote device ip
# GET: /api/register_ip?device={DEVICE}&ip={IP ADRESS SENSE}
#
@app.route('/api/register_ip', methods=['GET'])
def register_ip():
	ip = request.args.get('ip')
	device = request.args.get('device') + "_ip"
	
	if validate_ip_address(ip):
		robot01_context[device] = ip
		brain.robot01_context[device] = ip
		api_response = { 'status': 'Registration successful', 'error' : 0 }	   
	else:
		robot01_context[device] = ""
		api_response = { 'status': 'Registration failed', 'error' : 101 }	 

	return jsonify(api_response)

#
# Clear remote ip's
# GET: /api/clearips
#
@app.route('/api/clearips', methods=['GET'])
def clear_ips():
	robot01_context = {}
	api_response = { 'status': 'Clear successful', 'error' : 0 }	
	return jsonify(api_response)

#
# Text and image to robot post request
# POST: /api/ask_robot01 {"text" : string }
#
@app.route('/api/ask_robot01', methods=['POST'])
def ask_robot01():
	data = request.get_json()
	text = data["text"]
	vision = data["vision"]
	
	if not brain.busy:
		print("start processing")
		try:
			# Start the background task
			prompt_task = threading.Thread(target=brain.prompt, args=(text,vision))
			prompt_task.start()
		except Exception as e:
			print("Prompt error : ", e)

	api_response = { 'status': 'Successful', 'error' : 0 }

	return jsonify(api_response)

#
# Text to speech request
# POST: /api/tts {"text" : string }
#
@app.route('/api/tts', methods=['POST'])
def tts_api():
	data = request.get_json()
	text = data["text"]

	if robot01_context['robot01_ip'] != "":
		try:	
			# Put text in the brain queue
			brain.speak(text)
			api_response = { 'status': 'ok', 'error' : 0 }
		except Exception as e:
			print("TTS error : " ,e)
			api_response = { 'status': 'Error', 'error' : 1 }		 
	else:
		api_response = { 'status': 'Error', 'error' : 1 }

	return jsonify(api_response)

############### Helper functions ############### 
#
# Validate IP Addresses
#
def validate_ip_address(ip_string):
	try:
		ip_object = ipaddress.ip_address(ip_string)
		return True
	except Exception as e:
		print(e)
		return False

#
#
#
if __name__ == '__main__':

	# Start brain
	brain.start()
	#Flask App
	app.run(host='0.0.0.0', port=5000,threaded=True)

