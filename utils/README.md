# pvdid utils

This directory contains some utilities complementing the _pvdid-daemon_.

## pvdid-monitor
This (node) script performs PvD discovery and _pvd.json_ file retrieval.

It handles connections with the pvdid daemon, monitors PvD list notifications
and the *hFlag* for PvDs, and performs https retrieval (_https://<pvdid>/pvd.json_)
if needed.

It also performs periodic JSON retrieval based on the *expireDate* field of the
retrieved JSON object (or, by default, on the *expire* header value of the https
response).

If an attempt to retrieve the file has failed, a retry is performed 1 minute later.

It uses the *PVDID\_PORT* environment variable, and defaults to 10101, to establish
a connection with the local pvdid daemon.

Once a JSON file has been retrieved for a given PvD, it requests the pvdid daemon to
set/update the *extraInfo* attribute for the given PvD.

## Launching pvdid-monitor

~~~~
pvdid-monitor [-h|--help] <option>*
with option :
        -v|--verbose : outputs extra logs during operation
        -d|--debug : run in a simulation environment (local http server)
        --pvd <pvdId>* : list of space separated pvdId FQDN

In addition to the PvD specified on the command line, the script
monitors notifications from the pvdid-daemon to discover new PvD.

The list can be empty (and, in fact, should be left empty in a non
debug environment).

When running in a simulation environment (aka debug mode), requests to
retrieve the JSON description (pvd.json) are done via the
http://localhost:8000/<pvdId> URL instead of https://<pvdId>/pvd.json
For this to work, a local http server must be started locally of course
~~~~


## Dependencies
The following node modules shall be installed :

* Net (should be installed by default)
* http (should be installed by default)
* https (should be installed by default)
* node-schedule (should be installed : npm install node-schedule)


