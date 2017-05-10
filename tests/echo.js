#!/usr/bin/env node

var net = require("net");

var server = net.createServer(function(c) {
	console.log("client connected");
	c.on("end", function() {
		console.log("client disconnected");
	});
	c.on("data", function(data) {
		c.write("ECHO : " + data);
	});
});

server.on("error", function(err) {
	console.log("Error " + err);
});

server.listen(8124, function() {
	console.log("server bound");
});
