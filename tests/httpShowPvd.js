#!/usr/bin/env node

// Simple http server :
// http://localhost:8080 => displays the list of pvds
// http://localhost:8080/<pvd> => displays the attributes for <pvd>

var http = require("http");
var pvdd = require("pvdd");

var cnx = pvdd.connect({ "autoReconnect" : true });
var allPvd = {};

cnx.on("connect", function() {
	cnx.getList();
	cnx.subscribeAttribute("*");
	cnx.subscribeNotifications();
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

var server = http.createServer(function(req, res) {
	var host = "http://" + req.headers.host;
	res.writeHead(200);
	var pvd = req.url.slice(1);
	if (pvd == null || pvd == "") {
		var s = "";
		Object.keys(allPvd).forEach(function(pvd) {
			s += "<a href=" + host + "/" + pvd + ">" + pvd + "</a><br>";
		});
		res.end(s + "\n");
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

