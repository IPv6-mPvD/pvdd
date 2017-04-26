#!/usr/bin/env node

// This script starts a server that can be used in simulation mode
// to debug the pvd.json retrieval and monitoring feature

const http = require('http');

var JSONresponse = {
	"id" : 0,
	"multi" : false,
	"expire" : "today",
	"expireDate" : "2017-04-25T16:46:00Z"
};

http.createServer(function(req, res) {
	JSONresponse.name = req.url.slice(1);
	res.writeHead(200);
	res.end(JSON.stringify(JSONresponse, null, 12) + "\n");
}).listen(8000);

// Update the expiration date and the id every 2 minutes
var id = 0;
function UpdateJson() {
	var now = new Date(Date.now() + 120 * 1000);
	JSONresponse.expireDate = now.toISOString();
	JSONresponse.id = id++;
	setTimeout(UpdateJson, 120 * 1000);
}

UpdateJson();
