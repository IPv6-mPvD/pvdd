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

const Net = require("net");
const EventEmitter = require('events').EventEmitter;

function pvddConnnect(params) {
	if (params == undefined) {
		params = {};
	}
	var Port = params.port || parseInt(process.env["PVDID_PORT"]) || 10101;
	var autoReconnect = params.autoReconnect || false;
	var controlCnx = params.controlConnection || false;
	var verbose = params.verbose || false;

	var eventEmitter = new EventEmitter();
	var sock = null;
	var cnxed = true;

	var multiLines = false;
	var fullMsg = null;

	function getJson(J) {
		try {
			return(JSON.parse(J));
		}
		catch (e) {
			console.log(J + " : " + e);
			return(null);
		}
	}

	function HandleMultiLine(msg) {
		if ((r = msg.match(/PVDID_ATTRIBUTES +([^ \n]+)\n([\s\S]+)/i)) != null) {
			if ((attr = getJson(r[2])) != null)
				eventEmitter.emit("pvdAttributes", r[1], attr);
			return;
		}

		if ((r = msg.match(/PVDID_ATTRIBUTE +([^ ]+) +([^ \n]+)\n([\s\S]+)/i)) != null) {
			if ((attr = getJson(r[3])) != null)
				eventEmitter.emit("pvdAttribute", r[1], r[2], attr);
				eventEmitter.emit("on" + r[2], r[1], attr);
			return;
		}
		return;
	}

	function HandleOneLine(msg) {
		/*
		 * We check the beginning of a multi-lines section
		 * before anything else, to reset the buffer in case
		 * a previous multi-lines was improperly closed
		 */
		if (msg == "PVDID_BEGIN_MULTILINE") {
			multiLines = true;
			fullMsg = null;
			return;
		}

		/*
		 * End of a multi-lines section ?
		 */
		if (msg == "PVDID_END_MULTILINE") {
			if (fullMsg != null)
				HandleMultiLine(fullMsg);
			multiLines  = false;
			return;
		}

		/*
		 * Are we in a mult-line section ?
		 */
		if (multiLines) {
			fullMsg = fullMsg == null ? msg : fullMsg + "\n" + msg;
			return;
		}

		/*
		 * Single line messages
		 */
		if ((r = msg.match(/PVDID_LIST+(.*)/i)) != null) {
			if ((newListPvD = r[1].match(/[^ ]+/g)) == null) {
				newListPvD = [];
			}

			eventEmitter.emit("pvdList", newListPvD);
			return;
		}

		if ((r = msg.match(/PVDID_NEW_PVDID +([^ ]+)/i)) != null) {
			eventEmitter.emit("newPvd", r[1]);
			return;
		}

		if ((r = msg.match(/PVDID_DEL_PVDID +([^ ]+)/i)) != null) {
			eventEmitter.emit("delPvd", r[1]);
			return;
		}

		if ((r = msg.match(/PVDID_ATTRIBUTES +([^ ]+) +(.+)/i)) != null) {
			if ((attr = getJson(r[2])) != null)
				eventEmitter.emit("pvdAttributes", r[1], attr);
			return;
		}

		if ((r = msg.match(/PVDID_ATTRIBUTE +([^ ]+) +([^ ]+) +(.+)/i)) != null) {
			if ((attr = getJson(r[3])) != null)
				eventEmitter.emit("pvdAttribute", r[1], r[2], attr);
				eventEmitter.emit("on" + r[2], r[1], attr);
			return;
		}
		return;
	}

	function on(what, closure) {
		eventEmitter.on(what, closure);
	}

	function onAttribute(attrName, closure) {
		eventEmitter.on("on" + attrName, closure);
	}

	function write(msg) {
		if (sock != null) {
			sock.write(msg);
		}
	}

	function createPvd(pvdName) {
		write("PVDID_CREATE_PVDID 0 " + pvdName + "\n");
	}

	function setAttribute(pvdName, attrName, attrValue) {
		write("PVDID_BEGIN_TRANSACTION " + pvdName + "\n" +
		      "PVDID_BEGIN_MULTILINE\n" +
		      "PVDID_SET_ATTRIBUTE " + pvdName + " " +
					attrName + "\n" +
					JSON.stringify(attrValue, null, 12) + "\n" +
		      "PVDID_END_MULTILINE\n" +
		      "PVDID_END_TRANSACTION " + pvdName + "\n");
	}

	function unsetAttribute(pvdName, attrName) {
		write("PVDID_UNSET_ATTRIBUTE " + pvdName + " " + attrName + "\n");
	}

	function getList() {
		write("PVDID_GET_LIST\n");
	}

	function getAttributes(pvdName) {
		write("PVDID_GET_ATTRIBUTES " + pvdName + "\n");
	}

	function getAttribute(pvdName, attrName) {
		write("PVDID_GET_ATTRIBUTE " + pvdName + " " + attrName + "\n");
	}

	function subscribeAttribute(attrName) {
		write("PVDID_SUBSCRIBE " + attrName + "\n");
	}

	function subscribeNotifications() {
		write("PVDID_SUBSCRIBE_NOTIFICATIONS\n");
	}

	function internalConnection() {
		if (sock == null) {
			sock = Net.connect({
					host : "0.0.0.0",
					port : Port
				});
			sock.on("connect", function() {
				cnxed = true;
				if (controlCnx) {
					sock.write("PVDID_CONNECTION_PROMOTE_CONTROL\n");
				}
				eventEmitter.emit("connect");
			});
			sock.on("error", function(err) {
				cnxed = false;
				sock = null;
				eventEmitter.emit("error", err);
			});
			sock.on("data", function(d) {
				d.toString().split("\n").forEach(function(oneLine) {
					eventEmitter.emit("data", oneLine);
					HandleOneLine(oneLine);
				});
			});
		}
		else {
			sock.write("\n");	/* to trigger a connection error */
		}

		if (autoReconnect) {
			setTimeout(internalConnection, 1000);
		}
	}

	internalConnection();

	return({
		on : on,
		onAttribute : onAttribute,
		write : write,
		createPvd : createPvd,
		setAttribute : setAttribute,
		unsetAttribute : unsetAttribute,
		getList : getList,
		subscribeNotifications : subscribeNotifications,
		subscribeAttribute : subscribeAttribute,
		getAttributes : getAttributes,
		getAttribute : getAttribute
	});
}

/*
var cnx = pvddConnnect({ autoReconnect : true, controlConnection : false });

cnx.on("connect", function() {
	console.log("connection established with pvdd");
	cnx.getList();
	cnx.subscribeNotifications();
	cnx.subscribeAttribute("*");
});
cnx.on("error", function(err) {
	console.log("connexion error : " + err);
});
cnx.on("data", function(d) {
	// console.log("data received : " + d);
});
cnx.on("pvdList", function(pvdList) {
	console.log("pvd list : ", pvdList);
	pvdList.forEach(function(pvdName) {
		cnx.getAttributes(pvdName);
		cnx.getAttribute(pvdName, "hFlag");
		cnx.getAttribute(pvdName, "sequenceNumber");
	});
});
cnx.on("pvdAttributes", function(pvdName, attributes) {
	console.log("[" + pvdName + "] Attributes = " + JSON.stringify(attributes, null, 8));
	console.log("[" + pvdName + "] lifetime = " + attributes.lifetime);
});
cnx.on("pvdAttribute", function(pvdName, attrName, attrValue) {
	console.log("[" + pvdName + "] " + attrName + " = " + attrValue);
});

cnx.onAttribute("sequenceNumber", function(pvdName, value) {
	console.log("Sequence number for " + pvdName + " : " + value);
});
*/

exports.connect = pvddConnnect;
