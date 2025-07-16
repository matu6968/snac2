#!/usr/bin/env python3

# This is an example of a snac webhook that automatically follows all new followers.

# To use it, configure the user webhook to be http://localhost:12345, and run this program.

# copyright (C) 2025 grunfink et al. / MIT license

from http.server import BaseHTTPRequestHandler, HTTPServer
import time
import json
import os

host_name = "localhost"
server_port = 12345

class SnacAutoResponderServer(BaseHTTPRequestHandler):

    def do_POST(self):
        self.send_response(200)
        self.end_headers()

        content_type = self.headers["content-type"]
        content_length = int(self.headers["content-length"])
        payload = self.rfile.read(content_length).decode("utf-8")

        if content_type == "application/json":
            try:
                noti = json.loads(payload)

                type = noti["type"]

                if type == "Follow":
                    actor = noti["actor"]
                    uid = noti["uid"]
                    basedir = noti["basedir"]

                    cmd = "snac follow %s %s %s" % (basedir, uid, actor)

                    os.system(cmd)

            except:
                print("Error parsing notification")

if __name__ == "__main__":        
    webServer = HTTPServer((host_name, server_port), SnacAutoResponderServer)
    print("Webhook started http://%s:%s" % (host_name, server_port))

    try:
        webServer.serve_forever()
    except KeyboardInterrupt:
        pass

    webServer.server_close()
    print("Webhook stopped.")
