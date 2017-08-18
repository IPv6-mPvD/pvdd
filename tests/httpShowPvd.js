#!/usr/bin/env node

/*
	Copyright 2017 Cisco

	Licensed under the Apache License, Version 2.0 (the "License");
	you may not use this file except in compliance with the License.
	You may obtain a copy of the License at

		http://www.apache.org/licenses/LICENSE-2.0

	Unless required by applicable law or agreed to in writing, software
	distributed under the License is distributed on an "AS IS" BASIS,
	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
	See the License for the specific language governing permissions and
	limitations under the License.
*/
/*
 * Simple http server :
 * http://localhost:8200 => displays the list of pvds
 * http://localhost:8200/<pvd> => displays the attributes for <pvd>
 */
var http = require("http");
var pvdd = require("pvdd");

var cnx = pvdd.connect({ "autoReconnect" : true });
var allPvd = {};

cnx.on("connect", function() {
	cnx.subscribeAttribute("*");
	cnx.subscribeNotifications();
	cnx.getList();
});

cnx.on("error", function(err) {});

cnx.on("pvdList", function(pvdList) {
	pvdList.forEach(function(pvd) {
		cnx.getAttributes(pvd);
	});
	allPvd = {};
});

cnx.on("pvdAttributes", function(pvd, attrs) {
	allPvd[pvd] = attrs;
});

function pvd2str(pvd) {
	return JSON.stringify(allPvd[pvd] || {}, null, '\t');
}

var server = http.createServer(function(req, res) {
	h = {};
	pvd = req.url.slice(1);
	if (pvd == null || pvd == "") {
		s = "<!DOCTYPE html><html><head>" +
		    "<title>Provisionning domains</title>" +
		    "</head><body>\n";
		Object.keys(allPvd).forEach(function(pvd) {
			s += "<a href=http://" + req.headers.host + "/" + pvd +
			     " title='" + pvd2str(pvd).replace("'", "&#39;") +
			     "'>" + pvd + "</a><br>\n";
		});
		s += "</body></html>";
	}
	else {
		h = { 'Content-Type': 'application/json' };
		s = pvd2str(pvd);
	}
	res.writeHead(200, h);
	res.end(s);
});

var HttpPort = 8200;

server.listen(HttpPort, "::");

