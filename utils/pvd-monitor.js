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
 * Node module fetching and monitoring PVDID JSON extra information
 * via HTTPS
 * It handles either the 'expire' header for the message or the
 * expireDate field in the JSON object. This triggers a new request
 * to be sent to the https://<pvdid>/pvd.json URI
 *
 * Changes are notified to the main daemon
 *
 * The list of PvD to monitor is either specified on the
 * command line, either discovered via the notifications sent by
 * the pvdid-daemon
 */
var Net = require("net");
var schedule = require("node-schedule");
var http = require("http");
var https = require("https");

var Verbose = false;
var DevelopmentEnvironment = false;
var DefaultPvd = [];

var allPvd = {};

function dlog(s) {
	if (Verbose) {
		console.log(s);
	}
}

function tlog(s) {
	if (Verbose) {
		var now = new Date(Date.now());
		console.log(now.toISOString() + " : " + s);
	}
}

function ComplainConnection(Port, msg, err) {
	console.log(msg + "@pvdid-daemon:" + Port + ": " + err.code);
}

/*
 * CancelTimers : cancels all pending timers attached to a pvd
 */
function CancelTimers(pvdId) {
	if (allPvd[pvdId].scheduleJob != null) {
		try {
			allPvd[pvdId].scheduleJob.cancel();
		}
		catch (e) {
		}
		allPvd[pvdId].scheduleJob = null;
	}

	if (allPvd[pvdId].retryTimeout != null) {
		try {
			clearTimeout(allPvd[pvdId].retryTimeout);
		}
		catch (e) {
		}
		allPvd[pvdId].retryTimeout = null;
	}
}

/*
 * NewPvD : registers a new pvd. If already existing, does nothing
 */
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

/*
 * DelPvD : unregisters a pvd. This, for now, cancels any pending timer and
 * set its entry to null
 */
function DelPvD(pvdId) {
	if (allPvd[pvdId] != null) {
		allPvd[pvdId].attributes = {};
		allPvd[pvdId].monitored = false;
		CancelTimers(pvdId);
		allPvd[pvdId] = null;
	}
}

/*
 * DelAllPvD : unregisters all pvd. This is called when the connection is
 * lost with the daemon
 */
function DelAllPvD() {
	for (var key in allPvd) {
		CancelTimers(key);
	}
	allPvd = {};	// Forces deletion of all entries

	dlog("allPvd reset to empty");
}

function GetJson(s) {
	try {
		return(JSON.parse(s));
	} catch (e) {
		console.log("GetJson(" + s + ") : invalid JSON (" + e + ")")
	};
	return(null);
}

/*
 * Control connection related functions. The control connection will be used to
 * send attributes updates
 */
function ControlSockInit(sock) {
	sock.write("PVDID_CONNECTION_PROMOTE_CONTROL\n");
	DefaultPvd.forEach(function(pvdId) {
		sock.write("PVDID_CREATE_PVDID 0 " + pvdId + "\n");
	});
}

var controlSock = null;
var controlSucceeded = true;

function controlConnection(Port) {
	/*
	 * Perform the initial connection, and automatic reconnection
	 * in case we lose connection with it
	 */
	if (controlSock == null) {
		controlSock = Net.connect({ host : "0.0.0.0", port : Port });
		controlSock.on("connect", function() {
			controlSucceeded = true;
			ControlSockInit(controlSock);
			console.log("Control connection established with pvdid-daemon");
		});
		controlSock.on("error", function(err) {
			if (controlSucceeded) {
				ComplainConnection(Port, "control socket", err);
			}
			controlSucceeded = false;
			controlSock = null;
		});
	}
	else {
		controlSock.write("\n");	// ping : may trigger an error
	}
	setTimeout(controlConnection, 1000, Port);
}

/*
 * MonitorPvD : given a pvdId, check if it is needed to start/stop retrieving
 * its associated JSON extra info (that's what we call monitoring a PvD)
 */
function MonitorPvD(sock, pvdId, forceNow) {
	if (allPvd[pvdId] != null) {
		if (allPvd[pvdId].attributes.hFlag) {
			if (forceNow || ! allPvd[pvdId].monitored) {
				allPvd[pvdId].monitored = true;
				RetrievePvdExtraInfo(pvdId);
			}
		}
		else {
			if (allPvd[pvdId].monitored) {
				/*
				 * Currently been monitored => we must abort the
				 * monitoring (ie, if a https.get is in progress,
				 * just let it go, but if a timer is programmed,
				 * abort the timer)
				 */
				allPvd[pvdId].monitored = false;
				CancelTimers(pvdId);
			}
			/*
			 * In any case, we must delete the extraInfo field
			 */
			UnsetAttribute(pvdId, "extraInfo");
		}
	}
}

/*
 * UpdateAttributes : update the internal attributes structure for a given
 * pvdId and restart the PvD monitoring. This function is called when
 * the attributes for the PvD have been received
 */
function UpdateAttribute(sock, pvdId, attributes) {
	dlog("UpdateAttribute : pvdId = " + pvdId + ", attributes = " + attributes);
	if (allPvd[pvdId] != null) {
		var J;
		if ((J = GetJson(attributes)) != null) {
			/*
			 * Check the Seq field for changes
			 */
			var forceNow =
				J.sequenceNumber != allPvd[pvdId].attributes.sequenceNumber;
			allPvd[pvdId].attributes = J;
			MonitorPvD(sock, pvdId, forceNow);
			dlog("Attribute for " + pvdId + 
				" = " + JSON.stringify(J, null, 8));
		}
	}
}

/*
 * HandleMultiLine : a multi-line message has been fully read
 * Parses the message and handles it
 */
function HandleMultiLine(sock, msg) {
	var r;

	dlog("HandleMultiLine : msg = " + msg);

	if ((r = msg.match(/PVDID_ATTRIBUTES +([^ \n]+)\n([\s\S]+)/i)) != null) {
		UpdateAttribute(sock, r[1], r[2]);
		return;
	}
	return;
}

/*
 * HandleOneLine : one line message handling. The trailing
 * \n must have been removed
 */
var multiLines = false;
var fullMsg = "";

function HandleOneLine(sock, msg) {
	var r;

	dlog("Handling one line : " + msg + " (multiLines = " + multiLines + ")");

	/*
	 * We check the beginning of a multi-lines section
	 * before anything else, to reset the buffer in case
	 * a previous multi-lines was improperly closed
	 */
	if (msg == "PVDID_BEGIN_MULTILINE") {
		multiLines = true;
		fullMsg = "";
		return;
	}

	/*
	 * End of a multi-lines section ?
	 */
	if (msg == "PVDID_END_MULTILINE") {
		HandleMultiLine(sock, fullMsg);
		multiLines  = false;
		return;
	}

	/*
	 * Are we in a mult-line section ?
	 */
	if (multiLines) {
		fullMsg += msg + "\n";
		return;
	}

	/*
	 * Single line messages
	 */
	if ((r = msg.match(/PVDID_LIST +(.*)/i)) != null) {
		if ((newListPvD = r[1].match(/[^ ]+/g)) == null) {
			return;
		}
		newListPvD.forEach(function(pvdId) {
			if (allPvd[pvdId] == null) {
				/*
				 * New PvD => retrieve its attributes
				 */
				NewPvD(pvdId);
				sock.write("PVDID_GET_ATTRIBUTES " + pvdId + "\n");
			}
		});
		dlog("New pvd list : " + JSON.stringify(allPvd, null, 4));
		return;
	}

	if (msg.match(/PVDID_NEW_PVDID.*/i) != null) {
		/*
		 * Ignore them (we prefer using PVDID_LIST instead)
		 */
		return;
	}

	if ((r = msg.match(/PVDID_DEL_PVDID +([^ ]+)/i)) != null) {
		/*
		 * We must stop monitoring this PvD and unregister it
		 */
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

/*
 * Regular connection related functions. The regular connection will be used
 * to send queries (PvD list and attributes) and to receive
 * replies/notifications
 */
function regularSockInit(sock) {
	sock.write("PVDID_GET_LIST\n");
	sock.write("PVDID_SUBSCRIBE_NOTIFICATIONS\n");
	sock.write("PVDID_SUBSCRIBE *\n");
}

var regularSock = null;
var regularSucceeded = true;

function regularConnection(Port) {
	/*
	 * Perform the initial connection, and automatic reconnection
	 * in case we lose connection with it
	 */
	if (regularSock == null) {
		regularSock = Net.connect({ host : "0.0.0.0", port : Port });
		regularSock.on("connect", function() {
			regularSucceeded = true;
			regularSockInit(regularSock);
			console.log("Regular connection established with pvdid-daemon");
		});
		regularSock.on("error", function(err) {
			if (regularSucceeded) {
				DelAllPvD();
				ComplainConnection(Port, "regular socket", err);
			}
			regularSucceeded = false;
			regularSock = null;
		});
		regularSock.on("data", function(d) {
			d.toString().split("\n").forEach(function(oneLine) {
				HandleOneLine(regularSock, oneLine);
			});
		});
	}
	else {
		regularSock.write("\n");	// to trigger a connection error
	}
	setTimeout(regularConnection, 1000, Port);
}

/*
 * SetAttribute : send a set attribute query to the daemon
 * The attrValue must be a valid JSON object (actually its
 * string representation)
 */
function SetAttribute(pvdId, attrName, attrValue) {
	if (controlSock != null) {
		dlog("SetAttribute : attrName = " + attrName + ", attrValue = " + attrValue);
		controlSock.write(
			"PVDID_BEGIN_TRANSACTION " + pvdId + "\n" +
			"PVDID_BEGIN_MULTILINE\n" +
			"PVDID_SET_ATTRIBUTE " + pvdId + " " +
						attrName + "\n" +
						attrValue + "\n" +
			"PVDID_END_MULTILINE\n" +
			"PVDID_END_TRANSACTION " + pvdId + "\n");
	}
}

/*
 * UnsetAttribute : send a unset attribute query to the daemon
 */
function UnsetAttribute(pvdId, attrName) {
	if (controlSock != null) {
		controlSock.write(
			"PVDID_UNSET_ATTRIBUTE " + pvdId + " " + attrName + "\n");
	}
}

function ScheduleRetry(pvdId) {
	if (allPvd[pvdId] != null) {
		tlog("Setting timer for http fetch retry for " + pvdId);
		allPvd[pvdId].retryTimeout =
			setTimeout(RetrievePvdExtraInfo, 5 * 1000, pvdId);
	}
}

/*
 * DateStr : ISO8601 format. It should be recognized by Date.parse()
 * We should add a random delay to avoid flooding the https server
 * with GET requests at the same time. We should also make sure to
 * not query the http server at the exact time it says the data is
 * expiring (we might query data before the server actually has
 * updated it [race condition] the expires field for the next
 * expiration date)
 * So, if the expiration date is past or current, we will perform
 * a retry 5 seconds later (when this situation happens, scheduleJob()
 * returns a job == {}, hence j.name == null)
 */
function ScheduleAt(pvdId, DateStr, f, args) {
	tlog("ScheduleAt at " + DateStr + " programmed for " + pvdId);
	var j = new schedule.scheduleJob(new Date(DateStr), function() { f(args); });
	tlog("ScheduleAt : jobs = " + JSON.stringify(j));
	if (j.name == null) {
		ScheduleRetry(pvdId);
		return(null);
	}
	return(j);
}

function RetrievePvdExtraInfo(pvdId) {
	var Url, protocol;

	if (allPvd[pvdId] == null) {
		return;
	}

	if (DevelopmentEnvironment) {
		Url = "http://localhost:8000/" + pvdId;
		protocol = http;
	}
	else {
		Url = "https://" + pvdId + "/pvd.json";
		protocol = https;
	}

	tlog("Retrieving url " + Url);

	CancelTimers(pvdId);

	protocol.get(
		Url,
		function(res) {
			res.on('data', function(d) {
				tlog("Data received : " + d);
				// TODO : check the status (200, 300-400, 40x)

				// Check if we have been aborted in between
				if (allPvd[pvdId] == null || ! allPvd[pvdId].monitored) {
					dlog(pvdId + " : no longer monitored. Ignoring http data");
					return;
				}
				if ((J = GetJson(d.toString())) == null) {
					ScheduleRetry(pvdId);
					return;
				}

				/*
				 * Instead of passing d.toString() below, we pass
				 * the stringified conversion of the JSON object
				 * to send something cleaner to the daemon
				 */
				SetAttribute(pvdId, "extraInfo", JSON.stringify(J, null, 12));
				tlog("PvdId : " + pvdId + " expires = " + J.expires);

				/*
				 * Schedule if needed the retrieval of the JSON based
				 * on the 'expires' field
				 * FIXME : check that the date format of the JSON
				 * and of the HTTP header are both recognized by
				 * the Date() parser of node
				 */
				if ((nextDate = (J.expires || res.headers.expires)) != null) {
					allPvd[pvdId].scheduleJob =
						ScheduleAt(
							pvdId,
							nextDate,
							RetrievePvdExtraInfo,
							pvdId);
				}
			});
		}
	).on('error', function(err) {
		console.log("Can not connect to " + Url + " (" + err.message + ")");
		ScheduleRetry(pvdId);
	});
}

/*
 * Options parsing
 */
var InPvdList = false;
var Help = false;

process.argv.forEach(function(arg) {
	if (arg == "-h" || arg == "--help") {
		Help = true;
	} else
	if (arg == "-v" || arg == "--verbose") {
		Verbose = true;
	} else
	if (arg == "-d" || arg == "--development") {
		DevelopmentEnvironment = true;
	} else
	if (arg == "--pvd") {
		InPvdList = true;
	} else
	if (InPvdList) {
		DefaultPvd.push(arg);
	}
});

if (Help) {
	console.log("pvdid-monitor [-h|--help] <option>*");
	console.log("with option :");
	console.log("\t-v|--verbose : outputs extra logs during operation");
	console.log("\t-d|--development : run in a simulation environment (local http server)");
	console.log("\t--pvd <pvdId>* : list of space separated pvdId FQDN");
	console.log("\nIn addition to the PvD specified on the command line, the script");
	console.log("monitors notifications from the pvdid-daemon to discover new PvD.");
	console.log("\nThe list can be empty (and, in fact, should be left empty in a non");
	console.log("development environment).");
	console.log("\nWhen running in a simulation environment (aka development mode), requests to");
	console.log("retrieve the JSON description (pvd.json) are done via the");
	console.log("http://localhost:8000/<pvdId> URL instead of https://<pvdId>/pvd.json");
	console.log("For this to work, a local http server must be started of course");
	process.exit(0);
}

var Port = parseInt(process.env["PVDID_PORT"]) || 10101;

controlConnection(Port);
regularConnection(Port);

/* ex: set ts=8 noexpandtab wrap: */