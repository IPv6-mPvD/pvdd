# pvdd.py, pvddsync.py

The two modules pvdd.py and pvddsync.py offer connectivity with the
pvdd daemon, allowing clients written in python 3 to talk to it.

They basically hide the communication details with the daemon. It is
recommended to consult the README.md at the top of the repository to
have a description of what are pvdd clients and what they are allowed/
able to do.

2 flavors of APi are provided :

+ an asynchronous one (to mimic the nodejs/javascript API) [pvdd.py]
+ a synchronous one (to retrieve items) [pvddsync.py]

Note that both flavors can be used on the same object at the same time.

## pvdd.py

A _pvdd_ object must be first created. It follows a simple life cycle :

+ callbacks must be attached to various notifications (see below for a list
of those notifications)
+ connection is initiated (this connects to the _pvdd_ daemon using its
localhost socket interface)
+ requests can be sent (such as subscribing to notifications, retrieving
the pvd list or pvd attributes)
+ disconnection can be requested as well. In this case, a new connection
request is possible
+ the object must be freed

### Object creation

~~~~
import pvdd

pvdd = pvdd.pvdd()
~~~~

This simply creates an object ready to be connected to the pvdd daemon. Once
the connection is succeeding (ie, when the application explicitely calls the
connect() method of the object), a callback is called.

### Object release

Fully releasing the object is important : this aborts some internal threads.
Not doing so will prevent the program to properly exit (or, to simply exit).

~~~~
pvdd.leave()
~~~~

After the call, the _pvdd_ object can be recycled (or assigned __None__ for example).

Calling __leave()__ disconnects the socket with the _pvdd_ daemon.


### Notifications

The application attaches callbacks to signals using the generic __on__ method, and
detaches them using the generic __off__ method :

~~~~
pvdd.on(signalName, callback)
~~~~

__signalName__ is a string. __callback__ is a closure (aka a function/method name).

Attempting to attach the same closure to the same signal multiple times
only attaches it once (ie, the closure will not be called multiple times).

The signature of the callback depends on the signal, however we can already indicate
that they all be passed the _pvdd_ object as their first parameter. This makes it
easier to use the same callback for different pvdd connections.

A few signals are available. They will be triggered in various situations,
explained below. The corresponding signal names are :

+ "connect" : when a connection succeeds. May happen at any time if the connection
is performing some auto-reconnection attempts
+ "error" : certainly not the best chosen signal name. Happens when an error
happens on the connection (either because of a connection establishement failure,
or because something wrong is happening on the connection, or simply because the
connection is closed (either locally or remotely))
+ "pvdList" : called when a list of pvd has been received
+ "pvdAttributes" : called when the attributes for a given pvd have been received
+ "pvdAttribute" : called when a given attribute for a given pvd has been received
+ "newPvd" : called when a pvd has appeared
+ "delPvd" : called when a pvd has disappeared

It is possible to detach a previously attached closure from a signal :

~~~~
pvdd.off(signalName, callback)
~~~~


### Subscriptions

Some notifications can be received following an explicit request. They are :

+ "pvdList" (following _pvdd.getList()_)
+ "pvdAttributes" (following _pvdd.getAttributes(pvd)_)
+ "pvdAttribute" (following _pvdd.getAttribute(pvd, attrName)_)

__pvdList__, __pvdAttributes__ and other notifications may be received at any time
if the application whishes it. To this end, the application must explicitely
subscribe to these notifications, by calling the following functions :

~~~~
pvdd.subscribeNotifications() :
	we want to be notified of every pvd list change
pvdd.subscribeAttribute(pvdname) :
	we want to be notified of changes in the attributes for pvd pvdname
~~~~

Pvd list change generates the following notifications :

+ either "newPvd", either "delPvd" depending on the cause of the change
+ "pvdList" in every case

A change in the attributes for a pvd triggers the "pvdAttributes" notification.

If __pvdname__ (in __subscribeAttribute()__ ) is '*', a change in the attributes for
any pvd will trigger the "pvdAttributes" signal.

Note that these functions must be called after the connection has succeeded. Thus,
it is best to call them in the __"connect"__ callback.

### Connecting

The connection function accepts a few named parameters :

~~~~
pvdd.connect(
	autoReconnect = False,
	verbose = False,
	port = None,
	controlConnection = False
)
~~~~

When __autoReconnect__ is set to _True_, automatic reconnection attempts are
performed every second until the connection succeeds.

If __port__ is set to _None_, the following resolution is done :

+ if an environment variable called __PVDD_PORT__ is present, its value is used
+ otherwise, the default __10101__ value is used

If __controlConnection__ is set to _True_, automatic promotion of the socket is
done as soon as it is connected with the _pvdd_ daemon.

If the connection succeeds, the __"connect"__ signal is triggered.

If the connection fails (or is later closed for some reason), the __"error"__ signal
is triggered.


### Disconnecting

If for some reason, the application wants to temporarily close the connection, it
may disconnect it.

~~~~
pvdd.disconnect()
~~~~

The callbacks attached to the various signals remain attached.

### Requesting the pvd list

~~~~
pvdd.getList()
~~~~

The call simply sends a request on the socket and immediately returns.

The result (aka the pvd list) will be received in the callback attached
to the __"pvdList"__ signal.

### Requesting the attributes for a pvd

~~~~
pvdd.getAttributes(pvd)
~~~~

The call simply sends a request on the socket and immediately returns.

The results (aka the pvd attributes) will be received in the callback
attached to the __"pvdAttributes"__ signal.

Note that, for now, specifying an unknown pvd results in no response.

### Requesting a given attribute for a given pvd

~~~~
pvdd.getAttribute(pvd, attrName)
~~~~

The call simply sends a request on the socket and immediately returns.

The results (aka the pvd attribute) will be received in the callback
attached to the __"pvdAttribute"__ signal.

Note that, for now, specifying an unknown pvd or an attribute not available
for the pvd results in no response.

### Setting an attribute

Control connections can create/update an attribute for a given pvd :

~~~~
pvdd.setAttribute(pvd, attrName, attrValue)
~~~~

__pvd__ and __attrName__ are strings.

__attrValue__ is an object. It will be converted to a string (using JSON.stringify) before
being sent to the _pvdd_ daemon.

### Deleting an attribute

Control connectins can delete an attribute for a given pvd :

~~~~
pvdd.unsetAttribute(pvdn, attrName)
~~~~

Note that modifying the attributes of a pvd will trigger the __"pvdAttributes"__ signal.

### Creating a pvd

Control connections can register a pvd (it may already be existing, in which case nothing
will happen) :

~~~~
pvdd.createPvd(pvd)
~~~~

This may (or may not) trigger the __"newPvd"__ and __"pvdList"__ signals (see below).

### "connect" signal

~~~~
def handleConnect(pvdd):
	print("Connected")
	pvdd.subscribeNotifications()
	pvdd.subscribeAttribute("*")
	pvdd.getList()

pvdd.on("connect", handleConnect)
~~~~

The __pvdd__ parameter is the original object we attached the callback on.

### "error" signal

~~~~
def handleDisconnect(pvdd, msg):
	print("Disconnection : ", msg)

pvdd.on("disconnect", handleDisconnect)
~~~~

The __pvdd__ parameter is the original object we attached the callback on.

### "pvdList" signal

~~~~
def handlePvdList(pvdd, pvdList):
	print("Pvd list :", pvdList)
	for pvd in pvdList:
		pvdd.getPvdAttributes(pvdd, pvd)

pvdd.on("pvdList", handlePvdList)
~~~~

The __pvdd__ parameter is the original object we attached the callback on.


### "newPvd" signal

~~~~
def handeNewPvd(pvdd, pvd):
	print("New pvd :", pvd)

pvdd.on("newPvd", handleNewPvd)
~~~~

The __pvdd__ parameter is the original object we attached the callback on.


### "delPvd" signal

~~~~
def handeDelP vd(pvdd, pvd):
	print("Pvd removed :", pvd)

pvdd.on("delPvd", handleDelPvd)
~~~~

The __pvdd__ parameter is the original object we attached the callback on.


### "pvdAttributes" signal

~~~~
def handleAttributes(pvdd, pvd, attrs):
	print("Attributes for", pvd, ":", attrs)

pvdd.on("pvdAttributes", handleAttributes)
~~~~

The __pvdd__ parameter is the original object we attached the callback on.

The __attrs__ parameter is a JSON object (or, more correctly, is a python object
resulting of the parsing of a JSON string).

### "pvdAttribute" signal

~~~~
def handleAttribute(pvdd, pvd, attrName, attrValue):
	print("Attribute", attrName, "for", pvd, ":", attrValue)

pvdd.on("pvdAttribute", handleAttribute)
~~~~

The __pvdd__ parameter is the original object we attached the callback on.

The __attrValue__ parameter is a JSON object (or, more correctly, is a python object
resulting of the parsing of a JSON string).

### "on"\<attrName\> signal

On-the-fly signals can be triggered. They correspond to a specific attribute
being received following a request or an asynchronous modification.

It always comes in addition to a __"pvdAttribute'__ signal carrying the same
attribute name.

~~~~
def handleHFlag(pvdd, pvd, hFlag):
	print("hFlag for", pvd, ":", hFlag)

def handleExtraInfo(pvdd, pvd, extraInfo):
	print("Extra info for", pvd, ":", extraInfo)

pvdd.on("onhFlag", handleHFlag)
pvdd.on("onextraInfo", handleExtraInfo)
~~~~

The __pvdd__ parameter is the original object we attached the callback on.

This particular form of signal name requires to know the list of attributes.


## pvddsync.py

As you can guess, pvddsync.py is providing a pvddsync class, inherited from
pvdd. The constructor accepts one parameter, a timeout value which will
be applied on all synchronous operations (see below) that do not specify
a different timeout :

~~~~
import pvddsync

pvddCnxWait = pvddsync.pvddsync()	-> TO = None means infinite wait
pvddCnxSmallWait = pvddsync.pvddsync(TO = 0.5)
~~~~

This creates two synchronous pvdd objects, one waiting forever, one waiting at most
halt a second for the synchronous calls.

__TO__ is the timeout value. Passing __None__ means infinite wait, otherwise a
float value representing seconds must be passed. 

In addition to the functions and notification mechanism provided by pvdd,
synchronous functions are also provided :

~~~~
getSyncList(TO = None) -> an array of strings (can be empty) or None
getSyncAttributes(pvd, TO = None) -> a JSON object or None
getSyncAttribute(pvd, attrName, TO = None) -> a JSON object or None
~~~~

__pvd__ and __attrName__ are strings.

Note that specifying a non-existent pvd name or unknown attribute name will
trigger the timeout (or wait forever if TO == None). This may change in the
future.

When the timeout is triggered, a None value is returned.

A synchronous pvdd object also provides the same asynchronous feature as
a simple asynchronous object. Both features can be used at the same time.

For example, the following code is perfectly valid :

~~~~
import pvddsync

def printPvdAttributes(pvddsync, pvd, attrs):
    print("Async", pvd, "attributes :", attrs)

def printPvdList(pvddsync, pvdList):
    print("Async pvd list : ", pvdList)
    for pvd in pvdList:
        print("Attributes for", pvd, ": ", pvddsync.getSyncAttributes(pvd))
        pvddsync.getAttributes(pvd)	# asynchronous

# Create the pvdd connection object
pvdd = pvddsync.pvddsync(TO = 0.5)
pvdd.on("pvdList", printPvdList)
pvdd.on("pvdAttributes", printPvdAttributes)

pvdd.connect(autoReconnect = True, verbose = False)

pvdd.getList()	# asynchronous call

pvdList = pvdd.getSyncList()	# synchronous call

print("Get sync pvd list :", pvdList)

for pvd in pvdList:
    print("Name/sequence number for", pvd, ":",
            pvdd.getSyncAttribute(pvd, "name"), "/",
            pvdd.getSyncAttribute(pvd, "sequenceNumber"))

from time import sleep
sleep(2)

pvdd.leave()
~~~~

It does a mix of asynchronous and synchronous calls. For example, in the
asynchronous callback called when the pvd list is received (which can also
happen asynchronously when a pvd is appearing or disappearing), we are
synchronously retrieving each pvd attributes.

