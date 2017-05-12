# pvdid library

A native (aka C) library is provided intended to make it easier for native
application to manipulate/discover PvD related information. It is a light
wrapper on the pvdid-daemon connection and messages formatting as described
in the top-level README.md.

The library also encapsulates some direct calls to the kernel (via _setsockopt_/
_getsockopt_ calls).

## Principles

Interaction with the pvdid daemon is done via messages sent/received on a socket.

Interacting with the daemon means :

+ sending requests
+ receiving replies to requests
+ receiving notifications

The socket is exposed to the application, which makes it easy to integrate interaction
within any event based mechanism (main loop).

It is also possible to use a synchronous style of interaction with the daemon without
introducing any unwanted misbehaviour with an existing main loop in the same client.


The pvdid daemon is only accepting connection on 0.0.0.0. The port it listens on
however can have been specified when the daemon has been started. In this case,
the corresponding port must also be provided by the application to the library.

If not, an environment variable will be used.

The basic way of interacting with the daemon is :

+ establish a connection with the daemon (this gives a socket descriptor)
+ eventually create additional connections given this initial connection (for example
create control or binary sockets)
+ perform interaction with the daemon by sending requests
+ listen for/read incoming messages from the daemon on any of the connections
+ when done, close the connections


For now, applications must handle disconnection with the daemon (in case the latter
is stopped and restarted) to properly close and recreate connections if needed.

Each PvD is identified by a name (the _pvdname_ or _pvdId_) and has a set of attributes.

These attributes are made available to the client applications as a JSON object.

There are some well known attributes. In this case, dedicated functions permit
retrieving them. Otherwise, a generic function allows retrieving any specific
attribute, given that the application knows its name.

## API

Access to the prototypes and types definitions is done by including \<libpvdid.h\>

~~~~
#include <libpvdid.h>
~~~~

### Types
The following types are defined. They can be returned by various calls (listed in
the next sections).

#### pvd list

~~~~
typedef struct
{
	int	nPvdId;
	char	*pvdIdList[MAXPVD];
} t_pvdid_list;
~~~~

#### List of RDNSS

~~~~
typedef struct {
	int	nRdnss;
	char	*Rdnss[3];
} t_pvdid_rdnss;
~~~~

#### List of DNSSL

~~~~
typedef struct {
	int	nDnssl;
	char	*Dnssl[8];
} t_pvdid_dnssl;
~~~~

#### Binding a socket to a set of PvDs

~~~~
struct bind_to_pvd {
	int	npvd;	/* in/out */
	char	pvdnames[MAXBOUNDPVD][PVDNAMSIZ];
};
~~~~

This structure can be used to specify the preferred set of PvD a socket must be bound
to, but also to retrieve the current set of bound PvD for a given socket.

#### Retrieving the list of PvD known by the kernel

~~~~
struct net_pvd_attribute {
	char			name[PVDNAMSIZ];
	int			index;	/* unique number */

	/*
	 * Attributes of the pvd
	 */
	int			sequence_number;
	int			h_flag;
	int			l_flag;

	unsigned long		expires;	/* lifetime field */
};

struct pvd_list {
	int npvd;	/* in/out */
	struct net_pvd_attribute pvds[MAXPVD];
};
~~~~

Although this information can be obtained from the daemon, it can also be retrieved
from the kernel.

The information from the kernel however may contains less attributes than that
from the daemon, just because there can be some purely user defined attributes
(such as the extra JSON information not transmitted via the kernel mechanisms).

Direct call to the kernel to retrieve the list of PvDs is usually not done
by the applications. The pvdid daemon however will perform such a call when
starting.

### Establishing/closing a connection

There are 2 ways of creating a connection with the daemon :

+ by providing the connection port
+ by providing an existing connection socket

Disconnection is done by providing a connection socket.

The prototypes are as follows :

~~~~
extern int	pvdid_connect(int Port);
extern int	pvdid_reconnect(int fd);
extern int	pvdid_get_control_socket(int fd);
extern int	pvdid_get_binary_socket(int fd);

extern void	pvdid_disconnect(int fd);
~~~~

If _Port_ is not -1, its value is used to connect to the daemon.

If _Port_ is _-1_, the function will check if the **PVDID\_PORT** environment variable
exists. If it does, its value is used to connect to the daemon.

Otherwise, the default port value (10101) is used.

If _fd_ is provided, it must have been obtained via a previous call of one of the
connection routines. Its characteristics are reused (connection port mainly).

Connection channels do not interfere, which allows implementing localized
interaction with the daemon without disturbing existing connections.

### Synchronous calls

Synchronous calls are used when an application wants to send a request to the daemon
and to wait for a response before returning to the caller.

All the functions return 0 on success and -1 on error. For now, there is not much
information on the nature of the error, if any.

Here are they :

~~~~
extern int	pvdid_get_pvdid_list_sync(int fd, t_pvdid_list *pvdIdList);
extern int	pvdid_get_attributes_sync(int fd, char *pvdId, char **attributes);
extern int	pvdid_get_attribute_sync(int fd, char *pvdId, char *attrName, char **attrValue);
extern int	pvdid_get_rdnss_sync(int fd, char *pvdId, t_pvdid_rdnss *PtRdnss);
extern int	pvdid_get_dnssl_sync(int fd, char *pvdId, t_pvdid_dnssl *PtDnssl);
~~~~

The _pvdid\_get\_attributes\_sync_ function returns all the attributes collected
by the daemon for a given PvD. The _attributes_ output parameters must be freed
by the caller using __free__.

Similarily, the _pvdid\_get\_attribute\_sync_ function returns a string (_attrValue_)
that must freed using __free__.

The attributes or the attribute value for a given attribute are a valid stringified
representation of a JSON object.

The _pvdid\_get\_rdnss\_sync_ and _pvdid\_get\_dnssl\_sync_ functions are specialized
functions retrieving well known attributes (__RDNSS__ and __DNSSL__) and parsing the
returned string to build the _t\_pvdid\_rdnss_ and _t\_pvdid\_dnssl_ structures.

The _t\_pvdid\_rdnss_ and _t\_pvdid\_dnssl_ structures above contain strings that must
be freed by calling :

~~~~
extern void	pvdid_release_rdnss(t_pvdid_rdnss *PtRdnss);
extern void	pvdid_release_dnssl(t_pvdid_dnssl *PtDnssl);
~~~~

From an implementation point of view, the _fd_ parameter is simply cloned to create
a new connection channel just for the sake of sending the requests and receiving
the reply. It is then closed.

Note that these functions hide the hurdle of handling multi-lines messages.

### Asynchronous calls

The asynchronous calls are equivalent to the synchronous routines above except that they
simply send the message corresponding to the request and return.

The replies are expected to be read at a later stage. This allows sending multiple
requests at once. The caller however will have to handle multi-lines messages.

In any case, once a reply has been read, it still needs to be parsed using whichever
mechanism the application feels comfortable with (sscanf(), strcmp(), etc.).

The functions are :

~~~~
extern int	pvdid_get_pvdid_list(int fd);
extern int	pvdid_get_attributes(int fd, char *pvdId);
extern int	pvdid_get_attribute(int fd, char *pvdId, char *attrName);
extern int	pvdid_subscribe_notifications(int fd);
extern int	pvdid_unsubscribe_notifications(int fd);
extern int	pvdid_subscribe_pvdid_notifications(int fd, char *pvdId);
extern int	pvdid_unsubscribe_pvdid_notifications(int fd, char *pvdId);
extern int	pvdid_get_rdnss(int fd, char *pvdId);
extern int	pvdid_get_dnssl(int fd, char *pvdId);
~~~~

Following requests, the daemon should send replies. The format of these replies
is specified on the top-level README.md.

__Note__ : we will certainly provide an easy way for applications to collect multi-lines
messages received via multiple read()/recv() calls.

### Helpers

~~~~
extern int	pvdid_parse_pvdid_list(char *msg, t_pvdid_list *pvdIdList);
extern int	pvdid_parse_rdnss(char *msg, t_pvdid_rdnss *PtRdnss);
extern int	pvdid_parse_dnssl(char *msg, t_pvdid_dnssl *PtDnssl);
extern void	pvdid_release_rdnss(t_pvdid_rdnss *PtRdnss);
extern void	pvdid_release_dnssl(t_pvdid_dnssl *PtDnssl);
~~~~

### Kernel interfaces
There are a few functions directly talking to the kernel (in contrast to the other ones
talking to the daemon).

~~~~
extern	int	sock_bind_to_pvd(int s, char *pvdname);
extern	int	sock_get_bound_pvd(int s, char *pvdname);
extern	int	pvd_get_list(struct pvd_list *pvl);
~~~~

_sock\_bind\_to\_pvd()_ and _sock\_get\_bound\_pvd()_ might be used if an application wishes to
bind a socket to a single PvD. Here, _s_ is a socket, and not a connection socket with
the daemon.

The general case being that a socket can be bound to a multitude of PvDs, these
functions are of limited use and merely serve (for those having access to the source
code of the library) as an example.

The _pvdname_ parameter of _sock\_get\_bound\_pvd()_ is the address of an array of sufficient size
(aka PVDNAMSIZ).

The _pvl_ parameter of the _pvd\_get\_list()_ function must be initialized by the
caller : the __npvd__ field of the structure must contain the dimension of the
__pvds__ field. It is safe for the application to use the _struct pvd\_list_ structure
defined in the header file and to initiailze the _npvd_ field to __MAXPVD__.

That being said, _pvd\_get\_list()_ will primilarily be used by the pvdid daemon and not
by the applications (although nothing prevents them to call it).

## Well known attributes names
Some attributes are well known. They can be found in the JSON returned structure.

They are :

+ __pvdId__ : the PvD name (a string)
+ __pvdIdHandle__ : a unique number allocated by the kernel to this PvD instance
+ __sequenceNumber__ : the PvD sequence number (an integer between 0 and 15 [4 bits])
+ __hFlag__ : the h flag of the PvD (0 or 1)
+ __lFlag__ : the l flag of the PvD (0 or 1)
+ __lifetime__ : the expire value of the PvD (an integer)
+ __RDNSS__ : the list of DNS recursive servers associated to this PvD (array of strings)
+ __DNSSL__ : the list of DNS lookup domains (array of strings)
+ __extraInfo__ : the JSON structure retrieved from https://\<pvdid\>/pvd.json


## TODO
There is a lack of consistency in the various namings or in the way items should be freed/released.

Add a mechanism to allow an application to properly gather multi-lines messages.

We need to decide whether to prefix things with __pvd__ or with __pvdid__.

Thinking at it, it will be probably be better for the connection functions to return
an allocated structure containing, other things, the socket and provide accessors for
it.

