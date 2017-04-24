#!/usr/bin/env node

// Node module fetching and monitoring PVDID JSON extra information
// via HTTPS
// It handles either the 'expire' header for the message or the
// expireDate field in the JSON object. This triggers a new request
// to be sent to the https://<pvdid>/pvd.json URI
// Changes are notified to the main daemon
//

const https = require('https');

function sendMultiLines(pvdIdHandle, d) {
	if (pvdIdDaemonSock != null) {
		var nLines = countNewLines(d);
		pvdIdDaemonSock.write(
			"PVDID_BEGIN_TRANSACTION\n" +
			"PVDID_MULTILINE " + nLines + "\n" +
			"PVDID_SET_ATTRIBUTE " + pvdIdHandle + "extraInfo\n" +
			d);
	}
}

function retrievePvdExtraInfo(pvdId, pvdIdHandle) {
	https.get(
		'https://' + pvdId + '/pvd.json',
		function(res) {
			console.log('statusCode:', res.statusCode);
			console.log('headers:', res.headers);
			res.on('data', function(d) {
				sendMultiLines(pvdIdHandle, d);
				var J = JSON.parse(d);
				if (J != null) {
					if (J.expireDate != null) {
						scheduleAt(J.expireDate, retrievePvdExtraInfo, pvdId, pvdIdHandle);
					} else
					if (res.headers.expire != null) {
						scheduleAt(res.headers.expire, retrievePvdExtraInfo, pvdId, pvdIdHandle);
					}
				}
			});
		}
	).on('error', function(err) {
	});
}
