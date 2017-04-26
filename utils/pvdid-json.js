#!/usr/bin/env node

// Node module fetching and monitoring PVDID JSON extra information
// via HTTPS
// It handles either the 'expire' header for the message or the
// expireDate field in the JSON object. This triggers a new request
// to be sent to the https://<pvdid>/pvd.json URI
// Changes are notified to the main daemon
//
// The list of PvD to monitor is either specified on the
// command line, either discovered via the notifications sent by
// the pvdid-daemon

var Net = require("net");
var schedule = require("node-schedule");
var http = require("http");
var https = require("https");

var Verbose = false;
var DebugEnvironment = false;
var DefaultPvd = [];

var allPvd = {};

function dlog(s) {
	if (Verbose) {
		console.log(s);
	}
}

function ComplainConnection(Port, err) {
	console.log(
		"Can not connect to pvdid-daemon on port " +
		Port +
		" (" + err.message + ")");
}

// CancelTimers : cancels all pending timers attached to a pvd
function CancelTimers(pvdId) {
	if (allPvd[pvdId].scheduleJob != null) {
		allPvd[pvdId].scheduleJob.cancel();
		allPvd[pvdId].scheduleJob = null;
	}

	if (allPvd[pvdId].retryTimeout != null) {
		clearTimeout(allPvd[pvdId].retryTimeout);
		allPvd[pvdId].retryTimeout = null;
	}
}

// NewPvD : create a new pvd. If already existing, does nothing
function NewPvD(pvdId) {
	if (allPvd[pvdId] == null) {
		allPvd[pvdId] = {
			scheduleJob : null,
			retryTimeout : null,
			attributes : {},
			monitored : false
		};
	}
}

// DelPvD : delete a pvd. This, for now, cancels any pending timer and
// set its entry to null
function DelPvD(pvdId) {
	if (allPvd[pvdId] != null) {
		allPvd[pvdId].attributes = {};
		allPvd[pvdId].monitored = false;
		CancelTimers(pvdId);
		allPvd[pvdId] = null;
	}
}

function GetJson(s) {
	try {
		return(JSON.parse(s));
	} catch (e) {}
	return(null);
}

function ControlSockInit(sock) {
	sock.write("PVDID_CONNECTION_PROMOTE_CONTROL\n");
	DefaultPvd.forEach(function(pvdId) {
		sock.write("PVDID_CREATE_PVDID 0 " + pvdId + "\n");
	});
}

var controlSock = null;

function createControlConnection(Port) {
	// Perform the initial connection, and automatic reconnection
	// in case we lose connection with it
	if (controlSock == null) {
		controlSock = Net.connect({ host : "0.0.0.0", port : Port });
		controlSock.on("connect", function() {
			ControlSockInit(controlSock);
			console.log("Control connection established with pvdid-daemon");
		});
		controlSock.on("error", function(err) {
			ComplainConnection(Port, err);
			controlSock = null;
		});
	};
	setTimeout(createControlConnection, 1000, Port);
}

// MonitorPvD : given a pvdId, check if it is needed to start/stop retrieving
// its associated JSON extra info
function MonitorPvD(sock, pvdId) {
	if (allPvd[pvdId] == null) {
		// Unknown pvd
		return;
	}

	if (allPvd[pvdId].attributes.hFlag) {
		if (! allPvd[pvdId].monitored) {
			allPvd[pvdId].monitored = true;
			RetrievePvdExtraInfo(pvdId);
		}
	}
	else {
		if (allPvd[pvdId].monitored) {
			// Currently been monitored => we must abort the
			// monitoring (ie, if a https.get is in progress,
			// just let it go, but if a timer is programmed,
			// abort the timer)
			allPvd[pvdId].monitored = false;
			CancelTimers(pvdId);
		}
	}
}

// UpdateAttributes : update the internal attributes structure for a given
// pvdId and restart the PvD monitoring
function UpdateAttribute(sock, pvdId, attributes) {
	if (allPvd[pvdId] != null) {
		var J;
		if ((J = GetJson(attributes)) != null) {
			allPvd[pvdId].attributes = J;
			MonitorPvD(sock, pvdId);
			dlog("Attribute for " + pvdId + 
				" = " + JSON.stringify(J, null, 8));
		}
	}
}


// HandleMultiLine : a multi-line message has been fully read.
// Parse its first line and handles it
function HandleMultiLine(sock, msg) {
	var r;

	dlog("HandleMultiLine : msg = " + msg);

	if ((r = msg.match(/PVDID_ATTRIBUTES +([^ \n]+)\n([\s\S]+)/i)) != null) {
		UpdateAttribute(sock, r[1], r[2]);
		return;
	}
	return;
}

var multiLines = 0;
var fullMsg = "";

// HandleOneLine : one line message handling. The trailing
// \n must have been removed
function HandleOneLine(sock, msg) {
	var r;

	dlog("Handling one line : " + msg + " (multiLines = " + multiLines + ")");

	if (multiLines > 0) {
		fullMsg += msg + "\n";
		if (--multiLines <= 0) {
			HandleMultiLine(sock, fullMsg);
			fullMsg = "";
			multiLines = 0;
		}
		return;
	}

	if ((r = msg.match(/PVDID_MULTILINE +(\d+)/i)) != null) {
		try {
			multiLines = parseInt(r[1]);
		}
		catch (e) {}
		return;
	}

	if ((r = msg.match(/PVDID_LIST +(.*)/i)) != null) {
		var newListPvD = r[1].match(/[^ ]+/g);
		newListPvD.forEach(function(pvdId) {
			if (allPvd[pvdId] == null) {
				// New PvD => retrieve its attributes
				NewPvD(pvdId);
				sock.write("PVDID_GET_ATTRIBUTES " + pvdId + "\n");
			}
		});
		dlog("New pvd list : " + JSON.stringify(allPvd, null, 4));
		return;
	}

	if (msg.match(/PVDID_NEW_PVDID.*/i) != null) {
		// Ignore them (we prefer using PVDID_LIST instead)
		return;
	}

	if ((r = msg.match(/PVDID_DEL_PVDID +([^ ]+)/i)) != null) {
		// We must stop monitoring this PvD and unregister it
		var pvdId = r[1];

		DelPvD(pvdId);
		return;
	}

	if ((r = msg.match(/PVDID_ATTRIBUTES +([^ ]+) +(.+)/i)) != null) {
		UpdateAttribute(sock, r[1], r[2]);
		return;
	}
	return;
}

function HandleMsg(sock, msg) {
	dlog("Received msg " + msg);
	msg.toString().split("\n").forEach(function(oneLine) {
		HandleOneLine(sock, oneLine);
	});
	return;
}

var regularSock = null;

function createRegularConnection(Port) {
	// Perform the initial connection, and automatic reconnection
	// in case we lose connection with it
	if (regularSock == null) {
		regularSock = Net.connect({ host : "0.0.0.0", port : Port });
		regularSock.on("connect", function() {
			regularSock.write("PVDID_GET_LIST\n");
			regularSock.write("PVDID_SUBSCRIBE_NOTIFICATIONS\n");
			regularSock.write("PVDID_SUBSCRIBE *\n");
			console.log("Regular connection established with pvdid-daemon");
		});
		regularSock.on("error", function(err) {
			ComplainConnection(Port, err);
			regularSock = null;
		});
		regularSock.on("data", function(d) {
			HandleMsg(regularSock, d);
		});
	};
	setTimeout(createRegularConnection, 1000, Port);
}

// SetAttribute : send a set attribute query to the daemon
// The attrValue must be a valid JSON object (actually its
// string representation)
function SetAttribute(pvdId, attrName, attrValue) {
	if (controlSock != null) {
		var nLines = attrValue.split("\n").length;
		controlSock.write(
			"PVDID_BEGIN_TRANSACTION " + pvdId + "\n" +
			"PVDID_MULTILINE " + nLines + "\n" +
			"PVDID_SET_ATTRIBUTE " + pvdId + " " +
						attrName + "\n" +
						attrValue + "\n" +
			"PVDID_END_TRANSACTION " + pvdId + "\n");
	}
}

// DateStr : ISO8601 format. It should be recognized by Date.parse()
// We should add a random delay to avoid flooding the https server
// with GET requests at the same time
function ScheduleAt(DateStr, f, args) {
	dlog("ScheduleAt at " + DateStr + " programmed");
	var j = new schedule.scheduleJob(new Date(DateStr), function() { f(args); });
}

function RetrievePvdExtraInfo(pvdId) {
	var Url, protocol;

	if (allPvd[pvdId] == null) {
		return;
	}

	if (DebugEnvironment) {
		Url = "http://localhost:8000/" + pvdId;
		protocol = http;
	}
	else {
		Url = "https://" + pvdId + "/pvd.json";
		protocol = https;
	}

	dlog("Retrieving url " + Url);

	CancelTimers(pvdId);

	protocol.get(
		Url,
		function(res) {
			res.on('data', function(d) {
				dlog("Data received : " + d);
				// Check if we have been aborted in between
				if (allPvd[pvdId] == null || ! allPvd[pvdId].monitored) {
					return;
				}
				if ((J = GetJson(d.toString())) != null) {
					SetAttribute(pvdId, "extraInfo", d.toString());
					dlog("PvdId : " + pvdId + " expireDate = " + J.expireDate);
					if (J.expireDate != null) {
						allPvd[pvdId].scheduleJob =
							ScheduleAt(
								J.expireDate,
								RetrievePvdExtraInfo,
								pvdId);
					} else
					if (res.headers.expire != null) {
						allPvd[pvdId].scheduleJob =
							ScheduleAt(
								res.headers.expire, 
								RetrievePvdExtraInfo,
								pvdId);
					}
				}
			});
		}
	).on('error', function(err) {
		console.log("Can not connect to " + Url + " (" + err.message + ")");
		// We must retry. Schedule a retry in 60 seconds
		allPvd[pvdId].retryTimeout = setTimeout(RetrievePvdExtraInfo, 60 * 1000, pvdId);
	});
}

// Options parsing
var InPvdList = false;
var Help = false;

process.argv.forEach(function(arg) {
	if (arg == "-h" || arg == "--help") {
		Help = true;
	} else
	if (arg == "-v" || arg == "--verbose") {
		Verbose = true;
	} else
	if (arg == "-d" || arg == "--debug") {
		DebugEnvironment = true;
	} else
	if (arg == "--pvd") {
		InPvdList = true;
	} else
	if (InPvdList) {
		DefaultPvd.push(arg);
	}
});

if (Help) {
	console.log("fetch-extra-pvd-info [-h|--help] <option>*");
	console.log("with option :");
	console.log("\t-v|--verbose : outputs extra logs during operation");
	console.log("\t-d|--debug : run in a simulation environment (local http server)");
	console.log("\t--pvd <pvdId>* : list of space separated pvdId FQDN");
	console.log("\nIn addition to the PvD specified on the command line, the script");
	console.log("monitors notifications from the pvdid-daemon to discover new PvD");
	console.log("\nWhen running in a simulation environment (aka debug mode), requests to");
	console.log("retrieve the JSON description (pvd.json) are done via the");
	console.log("http://localhost:8000/<pvdId> URL instead of https://<pvdId>/pvd.json");
	console.log("For this to work, a local http server must be started locally of course");
	process.exit(0);
}

var Port = parseInt(process.env["PVDID_PORT"]) || 10101;

createControlConnection(Port);
createRegularConnection(Port);

