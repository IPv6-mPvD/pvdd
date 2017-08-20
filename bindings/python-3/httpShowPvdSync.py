#!/usr/bin/env python3

#
#	Copyright 2017 Cisco
#
#	Licensed under the Apache License, Version 2.0 (the "License");
#	you may not use this file except in compliance with the License.
#	You may obtain a copy of the License at
#
#		http://www.apache.org/licenses/LICENSE-2.0
#
#	Unless required by applicable law or agreed to in writing, software
#	distributed under the License is distributed on an "AS IS" BASIS,
#	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#	See the License for the specific language governing permissions and
#	limitations under the License.
#

# Simple http server :
# http://localhost:8100 => displays the list of pvds
# http://localhost:8100/<pvd> => displays the attributes for <pvd>
from http import server
import pvddsync
import json

PORT = 8100

class MyHandler(server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    def do_GET(self):
        host = self.headers["Host"] if "Host" in self.headers else "localhost:" + str(PORT)
        self.send_response(200)
        if self.path == '/':
            s = "<!DOCTYPE html><html><head>" +\
                "<title>Provisionning domains</title>" +\
                "</head><body>\n"
            for pvd in sorted(pvdd.getSyncList()):
                s = s + "<a href=http://" + host + "/" + pvd +\
                        " title='" + pvd2str(pvd).replace("'", "&#39;") +\
                        "'>" + pvd + "</a><br>\n"
            s = s + "</body></html>"
            self.send_header("Content-Type", "text/html")
        else:
            self.send_header("Content-Type", "application/json")
            s = pvd2str(self.path.strip("/"))

        self.end_headers()
        self.wfile.write(s.encode("utf-8"))

def pvd2str(pvd):
        attrs = pvdd.getSyncAttributes(pvd)
        return json.dumps(attrs, indent='\t') if attrs != None else "{}"

pvdd = pvddsync.pvddsync(TO = 0.1)
pvdd.connect(autoReconnect = True, verbose = False)

httpd = server.HTTPServer(("", PORT), MyHandler)
sa = httpd.socket.getsockname()
print("Serving HTTP on", sa[0], "port", sa[1], "...")
try:
    httpd.serve_forever()
except:
    pass

httpd.server_close()
pvdd.leave()
