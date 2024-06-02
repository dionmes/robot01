from flask import Flask, send_from_directory, render_template, jsonify, request

sappie_state = {}

#
context = {"sappie_ip":"", "sappiesense_ip" : "", "sappie" : sappie_state}

# Set static_folder to serve static files from 'static' directory
app = Flask(__name__, static_folder='static')
    
# Serve the index.html file from the static/html directory
@app.route('/')
def index(context=context):
    return render_template('index.html',context=context)
    
@app.route('/<path:filename>')
def serve_html(filename,context=context):
    return render_template(filename,context=context) 
# Serve CSS files
@app.route('/css/<path:filename>')
def serve_css(filename):
    return send_from_directory('static/css', filename)
# Serve JavaScript files
@app.route('/js/<path:filename>')
def serve_js(filename):
    return send_from_directory('static/js', filename)
# Serve images
@app.route('/images/<path:filename>')
def serve_images(filename):
    return send_from_directory('static/images', filename)


# API Endpoints
@app.route('/api/register_sappie', methods=['GET'])
def register_sappie():
    sappie_ip = request.args.get('ip')
    context['sappie_ip'] = sappie_ip
    return "ok"

@app.route('/api/register_sense', methods=['GET'])
def register_sense():
    sense_ip = request.args.get('ip')
    context['sappiesense_ip'] = sense_ip
    return "ok"

# Example API endpoint that echoes posted JSON data
@app.route('/api/echo', methods=['POST'])
def echo():
    data = request.get_json()
    return jsonify(data)

#
#
#
if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)


