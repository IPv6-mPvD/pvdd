# pvdid-daemon

This utility is intended to gather various pvd related information and to
server as a transient repository.

This information is coming from multiple sources and can be queried by clients
in synchronous or asynchronous ways.

The sources are :

* the kernel itself, via notification of received RA (router advertisements)
* clients with special credentials (allowing them to update the daemon
information)

Clients can query the content of the database using a local socket and a
text based protocol (to help scripted clients to issue requests and parse
answers).

## Starting pvdid-daemon

~~~~
pvdId-daemon [-h|--help] <option>*
where option :
        -v|--verbose
        -p|--port <#> : port number for clients requests (default 10101)
        -d|--dir <path> : directory in which information is stored (none by default)

Clients using the companion library can set the PVDID_PORT environment
variable to specify another port than the default one
~~~~

## Kernel interface
pvdId-daemon (the program) opens a netlink socket with the kernel and configures
it to be notified of ICMPV6 options related to IPV6 RA.

When such messages are received, specific fields are extracted and are used to
update the internal database.

For now, the fields of interest are :

* the PvD name itself (FQDN) and its characteristics (sequence number, H and L flags)
* the DNSSL field (list of search domains)
* the RDNSS field (list of recursive domain servers)

When a RA carries a new PVDID, an entry for this PVDID is created in the database.

Failure to create the netlink socket (because of insufficient rights) does not prevent the daemon to start.

## Clients
The daemon is acting as a server accepting clients connections on a socket bound on the
localhost address (0.0.0.0), port 10101 by default.

Native clients (aka C clients) can make use of a companion library hiding the communication
protocol. This library (more on it in a dedicated document) can specify a different port
using either an application specified port value, either the content of an environment
variable (PVDID_PORT).

For consistency, it is advised that bridges for different languages/runtimes also use the same
environment variable in their implementation (for example, node.js, lua or python bridges should
use it).

There are 2 kinds of clients :

* general clients, mostly querying information from the daemon, or listening to its
notifications
* control clients, which can update the daemon database.

The daemon will perform some credentials verification at the time a client promotes its
socket connection to a control connection.

## Control clients
Control clients can perform the following operations :

* register a pvd
* unregister a pvd
* update an attribute of a given pvd
* remove an attribute of a given pvd

Control clients will not receive any notification on their control connection.

Example of such control clients can be :

* script triggered by DHCPV6 to provide DNS related information
* script monitoring extra PVD information (https://<pvdId>/pvd.json)

## Regular clients
These clients can perform the following operations :

* query the list of currently registered PVDIDs
* query all the attributes for a given PVDID
* query a given attribute for a given PVDID
* subscribe/unsubscribe to modifications of at least one of the attributes of a given PVDID
* subscribe/unsubscribe to the modifications of the PVDID list

## Notifications
The daemon will notify clients having subscribed when some relevant changes happen :

* a PvD has appeared or disappeared. In this case, in addition to the specific condition message,
the full resulting list of PvD is also notified
* one or more of the attributes of a given PvD has changed

## Messages format
The section here describes the format of the messages. They are usually hidden if the companion
library is used. We however define them to allow implementation of bridges for any language, or
for direct use in scripts.

As said above, the messages sent over the sockets are text based, line oriented. This poses some
issues, especially regarding content extending over multiple lines, but this has been chosen to
make it easier to generate and parse messages from scripting languages (shells, javascript, phython,
lua, etc.).

We will describe the messages sent by both clients and the daemon (aka, the server).

All messages must end with a trailing \n. We will omit it in the descriptions below.o

PvD are specified by a FQDN (such as pvd.cisco.com for example) <pvdId> value.
A handle, aka a numerical value, can be associated to a PvD. This is a uniquely generated
value which can be used in certain API (to be defined). It is specified by <pvdIdHandle>
in the messages descriptions below.

### Clients

#### Promotion messages
A client wishing its connection being upgrade to a control connection must send the following on the
socket :

~~~~
PVDID_CONNECTION_PROMOTE_CONTROL
~~~~

Once the connection has been promoted, only control messages can be sent over the connection.
Other messages will be ignored.

#### Query messages
The following messages permit a client querying part of the daemon's database :

~~~~
PVDID_GET_LIST
PVDID_GET_ATTRIBUTES <pvdId>
~~~~

Here, <pvdId> is a FQDN PvD name.

PVDID_GET_LIST allows retrieving the list of the currently registered PvD.

#### Subscription messages
By default, regular (aka non control) clients will only receive on their connection replies
to their queries. By this, we mean that they won't receive any notifications.

However, it may be useful for unsollicited notifications to be made available to applications
(especially the ones based on main, event driven, loops).

In this case, clients can specify which kinds of notifications are allowed to be sent to them :

~~~~
PVDID_SUBSCRIBE_NOTIFICATIONS
PVDID_UNSUBSCRIBE_NOTIFICATIONS
PVDID_SUBSCRIBE <pvdId>
PVDID_UNSUBSCRIBE <pvdId>
~~~~

The first subscription will allow general notifications to be received (ie, notifications not
tied to a specific PvD).

The second subscription message will allow notifications to be received for a PvD of interest to
the client.

The notification messages are described below, in the server's section.

#### Control messages
Control promoted connections can send the following messages to the server :

~~~~
PVDID_CREATE_PVDID <pvdIdHandle> <pvdId>
PVDID_REMOVE_PVDID <pvdId>
PVDID_BEGIN_TRANSACTION <pvdId>
PVDID_SET_ATTRIBUTE
PVDID_END_TRANSACTION <pvdId>
~~~~

Messages that extend accross multiple lines (says #n line) must be preceded by :

~~~~
PVDID_MULTILINE #n
~~~~

For example, to set a JSON string attribute, a control client could use :

~~~~
PVDID_MULTILINE 6
PVDID_SET_ATTRIBUTE <pvdId> cost
{
	"currency" : "euro",
	"cost" : 0.01,
	"unitInMB" : 0.5
}
~~~~

### Server
The server generates the following messages :

* Generic (non PvD specific) notifications/replies :

~~~~
PVDID_LIST [<pvdId>]* (list of space-separated FQDN)
PVDID_NEW_PVDID <pvdId>
PVDID_DEL_PVDID <pvdI>
~~~~

* PvD specific messages :

~~~~
PVDID_ATTRIBUTES <pvdId> <attributeName> <attribueValue>
~~~~

or :

~~~~
PVDID_MULTILINE #n
PVDID_ATTRIBUTES <pvdId> <attributeName>
....	#N - 1 lines
~~~~

PVDID_LIST and PVDID_ATTRIBUTES can be sent as responses to clients's queries, or in
unsollicitated manner.

PVDID_NEW_PVDID is notified when a PvD appears. PVDID_DEL_PVDID is notified when a PvD
disappears.

In addition to the PVDID_NEW_PVDID and PVDID_DEL_PVDID, a notification message PVDID_LIST
with the updated list of PvD will be issued.


### Attributes
Attributes are pairs of key/value. Internally, the key and value objects are C strings. It is highly
advised that the value be a valid JSON object (ie, it has to be the string representation of a valid
JSON object). That means that integer objects must be provided as the string representation of the
integer, while boolean must be provided as "true"/"false". Arrays must be "[...]" and strings must
be "\"...\"", with the content of the string properly escaped as-per the JSON specification.

Similarily, compound objects must be "{...}", with the keys being enclosed by ".

The attributes are sent back as a stringified JSON object. They can be queried or notified (if the client has
subscribed for this PvD).

Example :
The PVDID_GET_ATTRIBUTES pvd.cisco.com query results in the following message (for example) to be received :

~~~~
PVDID_MULTILINE 14
PVDID_ATTRIBUTES pvd.cisco.com
{
        "pvdId" : "pvd.cisco.com",
        "pvdIdHandle" : 100,
        "sequenceNumber" : 0,
        "hFlag" : 0,
        "lFlag" : 0,
        "RDNSS" : ["8.8.8.8", "8.8.4.4", "8.8.2.2"],
        "DNSSL" : ["orange.fr", "free.fr"],
        "extraJson" : {
                "validUntil" : "2017-04-17T06:00:00Z",
                "name" : "orange.fr"
        }
}
~~~~

## TODO

* Clarify the notion of PvD still active (since we have multiple sources, kernel + control clients,
that can register a PvD in a untimely manner, do we need to hide some PvD registration done by
remote clients until the kernel has officially notified that a PvD is alive)
* Handle errors by sending error messages, especially to clients having done some requests (otherwise,
these clients could wait forever, which would be annoying for synchronous calls)

#### Last updated : Mon Apr 24 15:46:11 CEST 2017
