
import http.server
import ssl
import os

PORT = 8443
CERT_FILE = 'cert.pem'
KEY_FILE = 'key.pem'

# Change to the script's directory to serve files from there
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Create an SSL context
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
try:
    context.load_cert_chain(certfile=CERT_FILE, keyfile=KEY_FILE)
except FileNotFoundError:
    print(f"Error: Certificate ('{CERT_FILE}') or Key ('{KEY_FILE}') not found.")
    print("Please ensure they are in the same directory as the script.")
    exit(1)

httpd = http.server.HTTPServer(('0.0.0.0', PORT), http.server.SimpleHTTPRequestHandler)
httpd.socket = context.wrap_socket(httpd.socket, server_side=True)

print(f"Serving HTTPS on port {PORT}...")
httpd.serve_forever()
