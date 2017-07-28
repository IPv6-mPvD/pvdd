#!/usr/bin/env node

// Simple http server :
// http://localhost:8080 => displays the list of pvds
// http://localhost:8080/<pvd> => displays the attributes for <pvd>

var http = require("http");
var pvdd = require("pvdd");

var cnx = pvdd.connect({ "autoReconnect" : true });

cnx.on("connect", function() {
	allPvd = {};
	cnx.getList();
	cnx.subscribeAttribute("*");
	cnx.subscribeNotifications();
});

cnx.on("error", function(err) {
	allPvd = {};
});

cnx.on("pvdList", function(pvdList) {
	pvdList.forEach(function(pvd) {
		cnx.getAttributes(pvd);
	});
});

cnx.on("pvdAttributes", function(pvd, attrs) {
	allPvd[pvd] = attrs;
});

var server = http.createServer(function(req, res) {
	res.writeHead(200);
	var pvd = req.url.slice(1);
	if (pvd == null || pvd == "") {
		res.end(Object.keys(allPvd) + "\n");
	} else
	if (allPvd[pvd] == null) {
		res.end("No such pvd\n");
	}
	else {
		res.end(JSON.stringify(allPvd[pvd], null, '\t') + "\n");
	}
});

var HttpPort = 8080;

server.listen(HttpPort, "::");

