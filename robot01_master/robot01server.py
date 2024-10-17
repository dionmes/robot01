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
	robot01_context = {"robot01_ip": brain.robot01_ip, "robot01sense_ip" : brain.robot01sense_ip}

	try:
		return render_template('index.html',context=robot01_context)
	except Exception as e:
		abort(404)

#
# Serve any file as template
#
@app.route('/<filename>')
def serve_html(filename):
	robot01_context = {"robot01_ip": brain.robot01_ip, "robot01sense_ip" : brain.robot01sense_ip}

	if filename.endswith('.html'):
		try:
			return render_template(filename,context=robot01_context) 
		except Exception as e:
			abort(404)
	else:
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
# Text and image to robot post request
# POST: /api/ask_robot01 {"text" : string }
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
@app.route('/api/tts', methods=['POST'])
def tts_api():
	try:
		data = request.get_json()
		text = data["text"]
	except Exception as e:
		print(e)
		abort(404)

	if brain.robot01_ip != "":
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
# Update brain settings
# GET: /api/setting?item={item}&value={value}
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
		result = brain.setting(item,value)
		api_response = { item : result }
		return jsonify(api_response)
	except Exception as e:
		print(e)
		abort(404)

#
# Get brain settings
# GET: /api/setting?item={item}
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
# Clear remote ip's
# GET: /api/clearips
#
@app.route('/api/clearips', methods=['GET'])
def clear_ips():
	brain.robot01_ip = ""
	brain.robot01sense_ip = ""
	
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

