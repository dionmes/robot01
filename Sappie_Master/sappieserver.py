from flask import Flask, send_from_directory, render_template, jsonify, request
import ipaddress
import requests
import json
import threading
import queue
import base64
from datetime import datetime

from speecht5udp import TTS

import os
# Get the user's home directory
home_directory = os.path.expanduser("~")

# Sappie state object, not yet defined
sappie_state = {}
# Sappie current context. 
sappie_context = {"sappie_ip":"192.168.133.75", "sappiesense_ip" : "192.168.133.226", "sappie" : sappie_state}
# tts engine
tts_engine = TTS()
tts_max_sentence_lenght = 12

# Model
llm_model = "mskimomadto/chat-gph-vision"
#llm context
llm_context_id = "1"

# Queue for TTS sentences
tts_q = queue.Queue()
# llm url
llm_url = "http://localhost:11434/api/chat"
#
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
        api_response = { 'status': 'Sappie IP registration @ master successful', 'error' : 0 }    
    else:
        sappie_context['sappiesense_ip'] = ""
        api_response = { 'status': 'Sappie IP registration @ master failed', 'error' : 101 }    
            
    return jsonify(api_response)

#
# GET: /api/register_sense?ip={IP ADRESS SENSE}
#
@app.route('/api/register_sense', methods=['GET'])
def register_sense():
    sense_ip = request.args.get('ip')

    if validate_ip_address(sense_ip):
        sappie_context['sappiesense_ip'] = sense_ip
        api_response = { 'status': 'Sense IP registration @ master successful', 'error' : 0 }    
    else:
        sappie_context['sappiesense_ip'] = ""
        api_response = { 'status': 'Sense IP registration @ master failed', 'error' : 101 }    
            
    return jsonify(api_response)

#
# GET: /api/register_sense?ip={IP ADRESS SENSE}
#
@app.route('/api/clearips', methods=['GET'])
def clear_ips():
    sappie_context['sappie_ip'] = ""
    sappie_context['sappiesense_ip'] = ""
    
    api_response = { 'status': 'Sense IP registration @ master successful', 'error' : 0 }   
     
    return jsonify(api_response)

#
# POST: /api/ask_sappie {"text" : string }
#
@app.route('/api/ask_sappie', methods=['POST'])
def ask_sappie():
    data = request.get_json()
    text = data["text"]
    vision = data["vision"]
    
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
        requests.get("http://" + sappie_context['sappie_ip'] + '/displayaction?action=13')
        # Set resolution
        requests.get("http://" + sappie_context['sappiesense_ip'] + '/control?setting=framesize&param=5' )
        # Get image
        response  = requests.get("http://" + sappie_context['sappiesense_ip'] + '/capture')
        image_base64 = base64.b64encode(response.content)
        llm_obj["messages"][1]['images'] = [image_base64]

    # Show loader in display    
    requests.get("http://" + sappie_context['sappie_ip'] + '/displayaction?action=21')

    # Streaming post request
    llm_response = requests.post(llm_url, json = llm_obj, stream=True, timeout=60)    
    llm_response.raise_for_status()
    
    # Show gear animation in display
    requests.get("http://" + sappie_context['sappie_ip'] + '/displayaction?action=18' )

    # iterate over streaming parts and construct sentences to send to tts
    message = ""
    n = 0
    for line in llm_response.iter_lines():
        
        body = json.loads(line)
        token = body['message']['content']
        print(body)
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
    
    api_response = { 'Response': "ok" }
        
    return jsonify(api_response)

#
# POST: /api/tts {"text" : string }
#
@app.route('/api/tts', methods=['POST'])
def tts_api():
    data = request.get_json()
    text = data["text"]

    if sappie_context['sappie_ip'] != "":        
        tts_q.put_nowait(text)
        
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
# tts_worker for queue
#
def tts_worker():

    if not tts_engine.loaded:
        print("Load model")    
        tts_engine.loadModel()
        
        # Show neutral face
        requests.get("http://" + sappie_context['sappie_ip'] + '/displayaction?action=3')

    while True:
        # Call to enable Audio receive
        requests.get("http://" + sappie_context['sappie_ip'] + "/audiostream?on=1")

        text = tts_q.get()
        # Show chat animation in display
        requests.get("http://" + sappie_context['sappie_ip'] + '/displayaction?action=23')
        tts_engine.speak(text,sappie_context['sappie_ip']) 
        tts_q.task_done()
        
        if not tts_q.qsize() > 0:
            # Show neutral face
            requests.get("http://" + sappie_context['sappie_ip'] + '/displayaction?action=3')

#
#
#
if __name__ == '__main__':
    # Show gear animation in display
    requests.get("http://" + sappie_context['sappie_ip'] + '/displayaction?action=18' )
    
    #TTS worker
    threading.Thread(target=tts_worker, daemon=True).start()

    #Flask App
    app.run(host='0.0.0.0', port=5000,threaded=True)

