import http.server
import json
import queue
import signal
import socketserver
import socket
import struct
import time
import threading
from io import BytesIO

running = True
pos = (0.0, 0.0, 0.0)
q = queue.Queue()

class HTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        return http.server.SimpleHTTPRequestHandler.do_GET(self)

    def do_POST(self):
        global pos
        
        content_length = int(self.headers['Content-Length'])
        body = self.rfile.read(content_length)
        
        data = json.loads(body)
        if data['action'] == 'SET' or data['action'] == 'GET':
            q.put(data)
        
        self.send_response(200)
        self.end_headers()
        response = BytesIO()
        
        if data['action'] == 'GET':
            response.write(json.dumps(pos).encode('utf-8'))
        elif data['action'] == 'SET':
            response.write(b'"Good"');
        else:
            response.write(b'"Bad"');
        self.wfile.write(response.getvalue())

        
class client(threading.Thread):
    def __init__(self):
        super(client, self).__init__()
        self.sock = None
        self.connected = False

    def getPos(self):
        try:    
            self.sock.send(b'GET')
            
        except:
            print("ERROR: Send failed.")
            self.sock.close()
            self.connected = False
            return None
        
        try:
            buf = self.sock.recv(1024)
            return struct.unpack('ddd', buf)
            
        except:
            print("ERROR: Recv failed.")
            self.sock.close()
            self.connected = False
            return None

    def setPos(self, pos):
        try:
            self.sock.send(b'SET' + struct.pack('ddd', float(pos['lat']), float(pos['lng']), 8.0))
            return True
            
        except:
            print('ERROR: Send failed')
            self.sock.close()
            self.connected = False
            return None

    def connect(self):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect(('127.0.0.1', 5678))
            
        except:
            self.sock = None
            print("ERROR: Unable to connect to server, reconnecting...")
        
    def run(self):
        global running
        global pos
        self.lastGet = time.time()
        
        while running:
            if not self.connected:
                self.connect()
                if self.sock == None:
                    continue
                else:
                    self.connected = True
                    self.lastGet = time.time()
                    pos = self.getPos()
                    print("Connected.")
            
            if time.time() - self.lastGet > 10:
                pos = self.getPos()
                self.lastGet = time.time()
            
            while not q.empty() and self.connected and running:
                msg = q.get()
                if msg['action'] == 'SET':
                    self.setPos(msg['pos'])
                elif msg['action'] == 'GET':
                    self.getPos()
            
            
            time.sleep(0.1)
            
def signal_handler(signals, frame):
    global running
    print('Shutting down!')
    running = False
    
def main():
    global running    
    signal.signal(signal.SIGINT, signal_handler)
    running = True
    
    clientThread = client()
    clientThread.start()
    print("Client thread started.")
    
    handler = HTTPRequestHandler
    httpd = socketserver.TCPServer(("", 8080), handler)
    serverThread = threading.Thread(target=httpd.serve_forever)
    serverThread.start()
    print("HTTP Server listening on 127.0.0.1:8080")        
    
    while running:
        time.sleep(0.1)
    
    httpd.shutdown()
        
    
    
if __name__ == '__main__':
    main()