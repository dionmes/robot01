from flask import Flask, send_from_directory, render_template, jsonify, request

sappie_state = {}

#
context = {"sappie_ip":"192.168.133.75", "sappiesense_ip" : "192.168.133.248", "sappie" : sappie_state}

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
@app.route('/api/greet', methods=['GET'])
def greet():
    name = request.args.get('name', 'World')
    return jsonify({'message': f'Hello, {name}!'})

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


