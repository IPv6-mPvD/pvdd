# pvdd utils

This directory contains some utilities complementing _pvdd_.

## pvd-monitor.js

This (node) script performs PvD discovery and _pvd.json_ file retrieval.

It handles connections with the _pvdd_ daemon, monitors PvD list notifications
and the __hFlag__ for PvDs, and performs https retrieval (_https://\<pvdid\>/pvd.json_)
if needed.

It also performs periodic JSON retrieval based on the *expires* field of the
retrieved JSON object (or, by default, on the *expires* header value of the https
response, if any).

If an attempt to retrieve the file fails, a retry is performed 1 minute later.

It uses the __PVDD\_PORT__ environment variable, and defaults to 10101, to establish
a connection with the local pvd daemon.

Once a JSON file has been retrieved for a given PvD, it requests the pvd daemon to
set/update the __extraInfo__ attribute for the given PvD.

## Launching pvd-monitor

~~~~
pvd-monitor [-h|--help] <option>*
with option :
        -v|--verbose : outputs extra logs during operation
        -d|--debug : run in a simulation environment (local http server)
        --pvd <pvdname>* : list of space separated pvdname FQDN

In addition to the PvD specified on the command line, the script
monitors notifications from the pvd daemon to discover new PvD.

The list can be empty (and, in fact, should be left empty in a non
debug environment).

When running in a simulation environment (aka debug mode), requests to
retrieve the JSON description (pvd.json) are done via the
http://localhost:8000/<pvdname> URL instead of https://<pvdname>/pvd.json
For this to work, a local http server must be started locally of course
~~~~

__pvd-monitor__ is a script shell properly starting the nodejs script
__pvd-monitor.js__ by setting the __NODE\_PATH__ variable to the right
value (see the dependency on the _pvdd_ node package below).


## Dependencies

The following node modules shall be installed :

* net (should be installed by default)
* http (should be installed by default)
* https (should be installed by default)
* node-schedule (shall be installed : npm install node-schedule)
* pvdd : this package is provided by this repository and should not
need to be installed if the nodejs script is started via the shell
script

