#!/usr/bin/env node

// This script starts a server that can be used in simulation mode
// to debug the pvd.json retrieval and monitoring feature

const http = require('http');

var JSONresponse = {
	"id" : 0,
	"metered" : false,
	"characteristics" : {
		"maxThroughput" : { "down" : 2000000 },
		"minLatency" : { "up" : 0.1 }
	},
	"expires" : "2017-04-25T16:46:00Z"
};

http.createServer(function(req, res) {
	res.writeHead(200);
	JSONresponse.name = req.url.slice(1);
	res.end(JSON.stringify(JSONresponse, null, 12) + "\n");
}).listen(8000);

// Update the expiration date and the id every 2 minutes
// To avoid race conditions, we will update the expiration
// date 5 seconds before its actual expiration date (so,
// when clients will request the new JSON, its expires
// date will have been updated 5 seconds before, which
// should be long enough for the http server to have
// updated it)
const PERIOD = 120 * 1000;

function UpdateJson(id) {
	var now = new Date(Date.now() + PERIOD + 5);
	JSONresponse.expires = now.toISOString();
	JSONresponse.id = id;
	setTimeout(UpdateJson, PERIOD, id + 1);
}

UpdateJson(0);
