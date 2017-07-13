# pvdd

This daemon is intended to gather various pvd related information and to
serve as a transient repository.

This information is coming from multiple sources and can be queried by clients
in synchronous or asynchronous ways.

The sources are :

* the kernel itself, via notification of received RA (router advertisements)
(for non PvD-aware kernels)
* the kernel via RTNETLINK notifications
* clients with special credentials (allowing them to update the daemon
information)

Clients can query the content of the database using a local socket and a
text based protocol (to help scripted clients to issue requests and parse
answers).

The daemon can also create/update PvD related data (PvD itself, but also
associated attributes) by calling dedicated new kernel functions.

## Starting pvdd

~~~~
pvdd [-h|--help] <option>*
where option :
        -v|--verbose
        -p|--port <#> : port number for clients requests (default 10101)
        -d|--dir <path> : directory in which information is stored (none by default)

Clients using the companion library can set the PVDD_PORT environment
variable to specify another port than the default one
~~~~

Note that the __--dir__ option is of no use for now.

## Kernel interface

### Non PvD-aware kernels

pvdd (the program) opens a netlink socket with the kernel and configures
it to be notified of ICMPV6 options related to IPV6 RA.

When such messages are received, specific fields are extracted and are used to
update the internal database.

For now, the fields of interest are :

* the PvD name itself (FQDN) and its characteristics (sequence number, H and L flags, lifetime)
* the dnssl field (list of search domains)
* the rdnss field (list of recursive domain servers)

When a RA carries a new pvd, an entry for this pvd is created in the database.

Failure to create the netlink socket (because of insufficient rights) does not prevent the daemon to start.
This capacity to start without a netlink socket is obviously mostly useful only in debug mode.

### PvD-aware kernels

pvdd assumes that the kernel it is running on is PvD-aware if calling one of the added PvD kernel functions
does not report any error (the kernel function to retrieve the list of the PvD currently seen by the kernel).

On PvD-aware kernels, the rtnetlink mechanism already provided by the kernel to notify new addresses or routes
has been extended to report creation/update/deprecation of PvD.

The daemon in this case has access to a set of additional kernel API :

* to retrieve the attributes of a PvD :
	* PvD name, sequence number, H, L flags, lifetime
	* associated IPv6 addresses
	* associated IPv6 routes
	* rdnss and dnssl
* to create a new PvD
* to delete a PvD (by setting its lifetime to 0)
* to modify any of its attributes
* to update the list of dnssl and rdnss (not yet implemented)

Modifying the attributes has the effect of the notification mechanism to be triggered, which,
in turn, allows the daemon to update its internal database (to reflect the state of the kernel).

## Clients

The daemon is acting as a server accepting clients connections on a socket bound on the
localhost address (0.0.0.0), port 10101 by default.

Native clients (aka C clients) can make use of a companion library hiding the communication
protocol. This library (more on it in a dedicated document) can specify a different port
using either an application specified port value, either the content of an environment
variable (**PVDD\_PORT**).

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
* script monitoring extra PVD information (https://\<pvdname\>/pvd.json)


## Regular clients
These clients can perform the following operations :

* query the list of currently registered pvds
* query all the attributes for a given pvd
* query a given attribute for a given pvd
* subscribe/unsubscribe to modifications of at least one of the attributes of a given pvd
* subscribe/unsubscribe to the modifications of the pvd list


## Notifications
The daemon will notify clients having subscribed when some relevant changes happen :

* a PvD has appeared or disappeared. In this case, in addition to the specific condition message,
the full resulting list of PvD is also notified
* one or more of the attributes of a given PvD has changed


## Messages format
This section describes the format of the messages. They are usually hidden if the companion
library is used. We however define them to allow implementation of bridges for any language, or
for direct use in scripts.

As said above, the messages sent over the sockets are text based, line oriented. This poses some
issues, especially regarding content extending over multiple lines, but this has been chosen to
make it easier to generate and parse messages from scripting languages (shells, javascript, phython,
lua, etc.).

In fact, messages are not entirely text based : some binary bits can be found in some of them,
as we will see when talking about connection promotion below.

We will describe the messages sent by both clients and the daemon (aka, the server).

All messages must end with a trailing \n. We will omit it in the descriptions below.

PvD are specified by a FQDN (such as pvd.cisco.com for example) \<pvdname\> value.
An id, aka a numerical value, can be associated to a PvD. This is a uniquely generated
value which can be used in certain API (to be defined). It is specified by \<pvdid\>
in the messages descriptions below.

### Clients

#### Promotion messages
A client wishing its connection to be upgraded to a control connection must send the following
message on the socket :

~~~~
PVD_CONNECTION_PROMOTE_CONTROL
~~~~

Once the connection has been promoted, only control messages can be sent over the connection.
Other messages will be ignored. For now, no message except error messages will be sent by
the server to the control clients.

There is another kind of promotion : a general connection can be promoted to a binary connection.

A binary connection has each message (whether one line or multi-lines) being preceded by its
length, in binary format (a integer). This has been introduced to make the life for the synchronous
API of the C companion library easier.

The message to send to the daemon is as follows :

~~~~
PVD_CONNECTION_PROMOTE_BINARY
~~~~

Currently, the C library is making use of this kind of promotion.

#### Query messages
The following messages permit a client querying part of the daemon's database :

~~~~
PVD_GET_LIST
PVD_GET_ATTRIBUTES <pvdname>
PVD_GET_ATTRIBUTE <pvdname> <attributeName>
~~~~

Here, \<pvdname\> is a FQDN PvD name.

**PVD\_GET\_LIST** allows retrieving the list of the currently registered PvD.
**PVD\_GET\_ATTRIBUTES** allows retrieving ALL attributes for a given PvD.

If \<pvdname\> is * (star), the attributes for all currently registered PvD
will be sent back.

#### Subscription messages
By default, regular (aka non control) clients will only receive on their connection replies
to their queries. By this, we mean that they won't receive any notifications.

However, it may be useful for unsollicited notifications to be made available to applications
(especially the ones based on main, event driven, loops).

In this case, clients can specify which kinds of notifications are allowed to be sent to them :

~~~~
PVD_SUBSCRIBE_NOTIFICATIONS
PVD_UNSUBSCRIBE_NOTIFICATIONS
PVD_SUBSCRIBE <pvdname>
PVD_UNSUBSCRIBE <pvdname>
~~~~

The first subscription allows general notifications to be received (ie, notifications not
tied to a specific PvD).

The second subscription message allows notifications to be received for a PvD of interest to
the client. If \<pvdname\> is * (star), the client subscribes for PvD related changes for all
current and future PvD.

The notification messages are described below, in the server's section.

#### Control messages
Control promoted connections can send the following messages to the server :

~~~~
PVD_CREATE_PVD <pvdid> <pvdname>
PVD_REMOVE_PVD <pvdname>
PVD_BEGIN_TRANSACTION <pvdname>
PVD_SET_ATTRIBUTE <pvdname> <attributeName> <attributeValue>
PVD_UNSET_ATTRIBUTE <pvdname> <attributeName>
PVD_END_TRANSACTION <pvdname>
~~~~

**PVD\_CREATE\_PVD** allows registering a new PvD. The \<pvdid\> value is intended for
future use and can be set to 0 for now.

**PVD\_REMOVE\_PVD** unregisters a PvD. Note that clarifications still need to be done
on the meaning of a valid (aka registered) PvD.

Control clients can create/modify attributes for a given PvD. When an attribute has
changed, the set of attributes may be notified to clients having subscribed for this
PvD. Some control clients may want to set multiple attributes. To avoid having
multiple notifications being sent for every modification, control clients must
use the **PVD_BEGIN_TRANSACTION** and **PVD_END_TRANSACTION** messages to indicate
that the notification must only happen once all attributes have been set.

Thus, a typical attribute modification sequence looks like :

~~~~
PVD_BEGIN_TRANSACTION <pvdname>
PVD_SET_ATTRIBUTE <pvdname> <attributeName1> <attributeValue1>
PVD_SET_ATTRIBUTE <pvdname> <attributeName2> <attributeValue2>
...
PVD_END_TRANSACTION <pvdname>
~~~~

**PVD\_SET\_ATTRIBUTE** messages received outside a transaction will be ignored (this implies that
to set only one attribute, one must still enclose the **PVD\_SET\_ATTRIBUTE** request with
**PVD\_BEGIN\_TRANSACTION** and **PVD\_END\_TRANSACTION**).

Messages that extend accross multiple lines must be enclosed with :

~~~~
PVD_BEGIN_MULTILINE
...
PVD_END_MULTILINE
~~~~

For example, to set a JSON string attribute, a control client could use :

~~~~
PVD_BEGIN_MULTILINE
PVD_SET_ATTRIBUTE <pvdname> cost
{
	"currency" : "euro",
	"cost" : 0.01,
	"unitInMB" : 0.5
}
PVD_END_MULTILINE
~~~~

Note that multi-lines sections can not be imbricated.

Unsetting an attribute (**PVD\_UNSET\_ATTRIBUTE**) does not need to be enclosed in a
**PVD\_BEGIN\_TRANSACTION** **PVD\_END\_TRANSACTION** sequence.

### Server
The server generates the following messages :

* Generic (non PvD specific) notifications/replies :

~~~~
PVD_LIST [<pvdname>]* (list of space-separated FQDN)
PVD_NEW_PVD <pvdname>
PVD_DEL_PVD <pvdname>
~~~~

* PvD specific messages :

~~~~
PVD_ATTRIBUTES <pvdname> <attribueValue>
PVD_ATTRIBUTE <pvdname> <attributeName> <attribueValue>
~~~~

or :

~~~~
PVD_BEGIN_MULTILINE
PVD_ATTRIBUTES <pvdname>
....
PVD_END_MULTILINE

PVD_BEGIN_MULTILINE
PVD_ATTRIBUTE <pvdname> <attributeName>
....
PVD_END_MULTILINE
~~~~

**PVD\_LIST** and **PVD\_ATTRIBUTES** can be sent as responses to clients's queries, or in
unsollicitated manner.

**PVD\_ATTRIBUTES** is the JSON object carrying all attributes for the given PvD.

**PVD\_ATTRIBUTE** is a response to a **PVD_GET_ATTRIBUTE** query. For now, it is never
sent in an unsollicitated manner.

**PVD\_NEW\_PVD** is notified when a PvD appears. **PVD\_DEL\_PVD** is notified when a PvD
disappears.

In addition to the **PVD\_NEW\_PVD** and **PVD\_DEL\_PVD**, a notification message **PVD\_LIST**
with the updated list of PvD will be issued.

Example : the pvd.cisco.com PvD is registered, then the pvd.free.fr PvD :

~~~~
PVD_NEW_PVD pvd.cisco.com
PVD_LIST pvd.cisco.com
PVD_NEW_PVD pvd.free.fr
PVD_LIST pvd.free.fr pvd.cisco.com
~~~~

We now remove the pvd.cisco.com PvD :

~~~~
PVD_DEL_PVD pvd.cisco.com
PVD_LIST pvd.free.fr
~~~~


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
The **PVD\_GET\_ATTRIBUTES pvd.cisco.com** query results in the following (totally unconsistent) message (for example) to be received :

~~~~
PVD_BEGIN_MULTILINE
PVD_ATTRIBUTES pvd.cisco.com
{
        "name" : "pvd.cisco.com",
        "id" : 100,
        "sequenceNumber" : 0,
        "hFlag" : 1,
        "lFlag" : 0,
        "rdnss" : ["8.8.8.8", "8.8.4.4", "8.8.2.2"],
        "dnssl" : ["orange.fr", "free.fr"],
        "extraInfo" : {
                "expires" : "2017-04-17T06:00:00Z",
                "name" : "orange.fr"
        }
}
PVD_END_MULTILINE
~~~~

## TODO

* Clarify the notion of PvD still active (since we have multiple sources, kernel ** control clients,
that can register a PvD in a untimely manner, do we need to hide some PvD registration done by
remote clients until the kernel has officially notified that a PvD is alive)
* Handle errors by sending error messages, especially to clients having done some requests (otherwise,
these clients could wait forever, which would be annoying for synchronous calls)

#### Last updated : Fri Jul  7 12:08:54 CEST 2017
