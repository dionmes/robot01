from flask import Flask, send_from_directory, render_template, jsonify, request
import ipaddress
import requests
import json
import threading
from threading import Timer
import queue
import base64
from datetime import datetime

from tts_speecht5_udp import TTS
from stt_distil_whisper import STT

from robot01 import ROBOT
from display import DISPLAY
from sense import SENSE

import os
# Get the user's home directory
home_directory = os.path.expanduser("~")

# Sappie state object, not yet defined
sappie_state = {}
# Sappie current context. IPs will be overwritten by remote devices with current ip
sappie_context = {"sappie_ip":"192.168.133.75", "sappiesense_ip" : "192.168.133.226", "sappie" : sappie_state}

# Robot01
robot = ROBOT(sappie_context['sappie_ip'])

# Display engine
display = DISPLAY(sappie_context['sappie_ip'])

# Sense engine
sense = SENSE(sappie_context['sappiesense_ip'])

# tts engine
tts_engine = TTS()
tts_max_sentence_lenght = 12
tts_q = queue.Queue(3) 

# stt engine
stt_engine = STT(display)

# Model
llm_model = "mskimomadto/chat-gph-vision"
#llm context
llm_context_id = "1"

# llm url
llm_url = "http://localhost:11434/api/chat"
# llm template
llm_template = ""

# Set static_folder to serve static files from 'static' directory
app = Flask(__name__, static_folder='static')
app.config['TEMPLATES_AUTO_RELOAD'] = True

#
# Serve the index.html file from the static/html directory
#
@app.route('/')
def index(context=sappie_context):
    return render_template('index.html',context=sappie_context)
 
#
# Serve any file as template
#   
@app.route('/<filename>')
def serve_html(filename,context=sappie_context):
    return render_template(filename,context=sappie_context) 
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

###############  API Endpoints

#
# GET: /api/register_sappie?ip={IP ADRESS SAPPIE}
#
@app.route('/api/register_sappie', methods=['GET'])
def register_sappie():
    sappie_ip = request.args.get('ip')

    if validate_ip_address(sappie_ip):
        sappie_context['sappie_ip'] = sappie_ip

        # reinit objects with ip
        display = DISPLAY(sappie_context['sappie_ip'])

        api_response = { 'status': 'Registration successful', 'error' : 0 }    
    else:
        sappie_context['sappiesense_ip'] = ""
        api_response = { 'status': 'Registration failed', 'error' : 101 }    
            
    return jsonify(api_response)

#
# GET: /api/register_sense?ip={IP ADRESS SENSE}
#
@app.route('/api/register_sense', methods=['GET'])
def register_sense():
    sense_ip = request.args.get('ip')

    if validate_ip_address(sense_ip):
        sappie_context['sappiesense_ip'] = sense_ip
        api_response = { 'status': 'Registration successful', 'error' : 0 }    
    else:
        sappie_context['sappiesense_ip'] = ""
        api_response = { 'status': 'Registration failed', 'error' : 101 }    
            
    return jsonify(api_response)

#
# GET: /api/register_sense?ip={IP ADRESS SENSE}
#
@app.route('/api/clearips', methods=['GET'])
def clear_ips():
    sappie_context['sappie_ip'] = ""
    sappie_context['sappiesense_ip'] = ""
    
    api_response = { 'status': 'Clear successful', 'error' : 0 }    
     
    return jsonify(api_response)

#
# POST: /api/ask_sappie {"text" : string }
#
@app.route('/api/ask_sappie', methods=['POST'])
def ask_sappie():
    data = request.get_json()
    text = data["text"]
    vision = data["vision"]
    
    try:
        prompt(text,vision)
    except:
        print("Prompt error")

    api_response = { 'status': 'Successful', 'error' : 0 }

    return jsonify(api_response)

#
# POST: /api/tts {"text" : string }
#
@app.route('/api/tts', methods=['POST'])
def tts_api():
    data = request.get_json()
    text = data["text"]

    if sappie_context['sappie_ip'] != "":
        try:    
            tts_q.put_nowait(text)
        except:
            print("tts queue full")

        api_response = { 'status': 'ok', 'error' : 0 }
    else:
        api_response = { 'status': 'Error', 'error' : 1 }

    return jsonify(api_response)

############### General functions

#
# Validate IP Addresses
#
def validate_ip_address(ip_string):
    try:
        ip_object = ipaddress.ip_address(ip_string)
        return True
    except ValueError:
        return False

#
# prompt handler
#
def prompt(text,vision = False):

    # Read systems prompt
    with open(home_directory + '/Sappie_Master/llm_template.txt', 'r') as file:
        # Read the entire content of the file
        llm_template = file.read()

    now = datetime.now()
    system_template = llm_template + "Date and time is : " + now.strftime("%A %m/%d/%Y, %H:%M")
    
    llm_obj = { \
    "model": llm_model,  \
    "keep_alive": -1, \
    "context": [llm_context_id], \
    "messages" : [ {\
        "role" : "system", \
        "content" : system_template \
        } , { \
        "role" : "user", \
        "content" : text \
        }]
    }
    
    if vision:
        # Show cylon in display
        display.action(13)
        llm_obj["messages"][1]['images'] = sense.capture()

    # Show loader in display    
    display.action(21)

    # Streaming post request
    try:
        llm_response = requests.post(llm_url, json = llm_obj, stream=True, timeout=60)    
        llm_response.raise_for_status()
    except:
        print("Error calling llm")
    
    # Show gear animation in display
    display.action(18)

    # iterate over streaming parts and construct sentences to send to tts
    message = ""
    n = 0
    
    for line in llm_response.iter_lines():
        
        body = json.loads(line)
        token = body['message']['content']

        if token in ".,?!...\"" or n > tts_max_sentence_lenght:
            message = message + token
            #print(message)
            if not message.strip() == "" and len(message) > 1:
                tts_q.put_nowait(message)
            message = ""
            n = 0
        else:
            message = message + token
            n = n + 1
    
    if 'context' in body:
        print(body['context'])


#STT worker
def stt_worker():
    print("stt_worker started")
    stt_engine.load_models()
    
    while not stt_engine.loaded:
        print("STT Model Loading")
        time.sleep(3)

    stt_engine.start()
    
    while True:
        text = stt_engine.stt_q.get()[0]
        print(text)
        try:
            prompt(text,False)
        except:
            print("Prompt error")

        stt_engine.stt_q.task_done()

#
# Start mic again after tts output
#
def end_tts():
    print("Start mic")
    display.action(3) # Show neutral face
    stt_engine.start()

#
# tts_worker for queue
#
def tts_worker():
    print("tts_worker started")

    if not tts_engine.loaded:
        print("Load TTS model")    
        tts_engine.loadModel()
        
        # Show neutral face
        display.action(3)
        # Call to enable Audio receive
        robot.startaudio()

    while True:
        text = tts_q.get()
        stt_engine.stop() # Stop tts

        display.action(23) # Show chat animation in display

        try:
            tts_mic_timeout.cancel()
        except:
            print("No mic timeout")
        
        tts_engine.speak(text,sappie_context['sappie_ip']) 
        tts_q.task_done()
        tts_mic_timeout = Timer(len(text) * 0.1, end_tts)
        tts_mic_timeout.start()       
#
#
#
if __name__ == '__main__':
    # Show gear animation in display
    display.action(18)
    
    #TTS worker
    threading.Thread(target=tts_worker, daemon=True).start()
    #STT worker
    threading.Thread(target=stt_worker, daemon=True).start()

    #Flask App
    app.run(host='0.0.0.0', port=5000,threaded=True)

