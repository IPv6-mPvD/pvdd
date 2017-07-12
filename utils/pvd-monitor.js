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

var pvdd = require("pvdd");

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

function ComplainConnection(msg, err) {
	var Port = parseInt(process.env["PVDID_PORT"]) || 10101;
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
var controlCnx = pvdd.connect({
	autoReconnect : true, 
	controlConnection : true
});

controlCnx.succeeded = true;

controlCnx.on("connect", function() {
	console.log("Control connection established with pvdd");
	controlCnx.succeeded = true;
	DefaultPvd.forEach(controlCnx.createPvd);
});
controlCnx.on("error", function(err) {
	if (controlCnx.succeeded) {
		ComplainConnection("control socket", err);
	}
	controlCnx.succeeded = false;
});


/*
 * Regular connection related functions. The regular connection will be used
 * to send queries (PvD list and attributes) and to receive
 * replies/notifications
 */
var regularCnx = pvdd.connect({ autoReconnect : true });

regularCnx.succeeded = true;

regularCnx.on("connect", function() {
	regularCnx.succeeded = true;
	regularCnx.getList();
	regularCnx.subscribeNotifications();
	regularCnx.subscribeAttribute("*");
	console.log("Regular connection established with pvdd");
});

regularCnx.on("error", function(err) {
	if (regularCnx.succeeded) {
		DelAllPvD();
		ComplainConnection("regular socket", err);
	}
	regularCnx.succeeded = false;
});

regularCnx.on("pvdList", function(pvdList) {
	pvdList.forEach(function(pvdId) {
		if (allPvd[pvdId] == null) {
			/*
			 * New PvD => retrieve its attributes
			 */
			NewPvD(pvdId);
			regularCnx.getAttributes(pvdId);
		}
	});
	dlog("New pvd list : " + JSON.stringify(allPvd, null, 4));
});

regularCnx.on("delPvd", DelPvD);

regularCnx.on("pvdAttributes", function(pvdId, attrs) {
	/*
	 * update the internal attributes structure and restart the PvD
	 * monitoring
	 */
	dlog("Attributes for " + pvdId + " = " + JSON.stringify(attrs, null, 8));

	if (allPvd[pvdId] != null) {
		/*
		 * Check the Seq field for changes
		 */
		var forceNow =
			attrs.sequenceNumber != allPvd[pvdId].attributes.sequenceNumber;

		allPvd[pvdId].attributes = attrs;

		if (attrs.hFlag) {
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
			controlCnx.unsetAttribute(pvdId, "extraInfo");
		}
	}
});


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
	if (j.name == null) {
		ScheduleRetry(pvdId);
		return(null);
	}
	return(j);
}

function RetrievePvdExtraInfo(pvdId) {
	if (allPvd[pvdId] == null) {
		return;
	}

	if (DevelopmentEnvironment) {
		var Url = "http://localhost:8000/" + pvdId;
		var protocol = http;
	}
	else {
		var Url = "https://" + pvdId + "/pvd.json";
		var protocol = https;
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
				controlCnx.setAttribute(pvdId, "extraInfo", J);
				tlog("[" + pvdId + "] expires = " + J.expires);

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

/* ex: set ts=8 noexpandtab wrap: */
