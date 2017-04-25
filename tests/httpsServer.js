#!/usr/bin/env node

const https = require('http');
const fs = require('fs');

const options = {
	key: fs.readFileSync('/var/lib/docker-unit-test/integration-cli/fixtures/https/client-key.pem'),
	cert: fs.readFileSync('/var/lib/docker-unit-test/integration-cli/fixtures/https/client-cert.pem')
};

var JSONresponse = {
	"id" : 11,
	"multi" : false,
	"expire" : "today",
	"expireDate" : "2017-04-25T16:46:00Z"
};

https.createServer(function(req, res) {
  // Update the expireDate to always be 2 minutes ahead of now
  // at every request
  var now = new Date(Date.now() + 120 * 1000);
  JSONresponse.expireDate = now.toISOString();
  res.writeHead(200);
  res.end(JSON.stringify(JSONresponse, null, 12) + "\n");
}).listen(8000);

