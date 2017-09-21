# Testing the Pvd features

For now, 2 test programs have been written :

+ pvd-bound-socks
+ pvd-test-saddr

## pvd-bound-socks

It simply attempts to bind a socket to a command line specified pvd and
fetch the bound pvd to validate the kernel bindtopvd feature, if present.

It does not go beyond this simple test. The user needs to have a way to
retrieve the pvd list (_cat /proc/net/pvd_ being one).

~~~~
./pvd-bound-socks -h
usage : pvd-bound-socks [-h|--help] [<pvdname>]

Open a socket, bind it to a pvd and retrieve the current bound pvd set
~~~~

## pvd-test-saddr

This one is more complex. A client attempts to establish a IPv6 TCP socket connection
with a remote server using or not a specified pvd. The remote end prints out the
client's source address of the connection and sends it back to the client to check
that the return path is working properly.

Contrary to the _pvd-bound-sock_ program, the _pvd-test-saddr_ test program requires
a networked test infrastructure to work.

The following diagram depicts this infrastructure :

~~~~
                     + ----------------------- +
                     + host 1 (patched kernel) +
                     + ----------------------- +
                                 |
  ----------------------------------------------------------  net 1
                  |                       |
        + -------------- +        + -------------- +
        + router1 - pvd1 +        + router2 - pvd2 +
        + -------------- +        + -------------- +
                  |                       |
  ----------------------------------------------------------  net 2
                                 |
                             + ------ +
                             + host 2 +
                             + ------ +
~~~~

Here, _host1_ is the client. It requires a kernel with pvd-based source address selection
support.

All other machines do not need special kernel support for now.

_host2_ is the server.

The _pvd-test-saddr_ program offers both client and server features.

The test is run using 4 kvm instances. 2 networks have been defined using _virsh_ (_default_ for _eth0_ interfaces
in the kvm instances (_net 1_), and _admin_ for the _eth1_ interfaces (_net 2_)).

_router1_ and _router2_ are each running an instance of _radvd_ with PvD support.

For our test purpose, the following network configuration has been set :

~~~~
router1 :
	on net1 : static IPv6 address : 2001:db8:1::1/64
		  advertises pvd pirl1.cisco.com, prefix 2001:db8:1::/64
	on net2 : static IPv6 address : 2001:db8:cafe:ffff::1/64

router2 :
	on net1 : static IPv6 address : 2001:db8:cafe:1::1/64
		  advertises pvd pirl2.cisco.com, prefix 2001:db8:cafe:1::/64
	on net2 : static IPv6 address : 2001:db8:cafe:ffff::2/64

host2 :
	on net2 : static IPv6 address : 2001:db8:cafe:ffff:babe/64
	on net1 : dhcp configuration (not needed, but we wanted to be able to
		  connect from the outside world to it)

host1 : on net1 : SLAAC

Forwarding is enabled (or disabled at will for testing purpose) on the routers :
	sudo sysctl -w net.ipv6.conf.all.forwarding=1
~~~~

Note that _router1_ and _router2_ only advertise prefixes, not routes. This is sufficient to have _host1_
autoconfiguring its _eth0_ interface with IPv6 addresses (__SLAAC__).

The git module __https://github.com/IPv6-mPvD/radvd.git__ contains (in __test__) configuration files
suitable for this  test.

Usage :

~~~~
$ ./pvd-test-saddr -h
usage : pvd-test-saddr [-h|--help] [<option>*]
where option :
        -r|--remote <h:o:s:t:-:I:P:v:6> : IPv6 dotted address of the server
        -p|--pvd <pvdname> : selected pvd (optional)
        -c|--count <#> : loops counts (default 1)
        -i|--interval <#> : interval (in ms) between 2 loops (500 ms by default)
        -l|--list : print out the current pvd list
        -u|--udp : client connects using UDP (TCP default)

Open a socket, bind it to a pvd and connect to server, them perform
a send/receive loop (the server is sending the client's address to the
client)

Multiple pvd can be specified. In this case, the client opens as many
connections with the server with the specified pvds. Specifying 'none' as
a pvd name means that no pvd will be attached to the associated socket
If no option is specified, act as a server waiting for connection and
displaying peer's address. Note that the server always listens for TCP
and UDP connections

Example :
./pvd-test-saddr -u -r ::1 -p pvd1.my.org -p pvd2.my.org -p none -c 10 -i 1200
This creates 3 UDP connection with the server (on localhost) and performs 10
send/receive loops, each separated by 1.2 seconds
~~~~

Typical startup command, once all kvm machines have been started and radvd launched on the routers :

~~~~
Login on the server (aka host2) :
	ssh host2.local
	cd .../pvdd/tests/bound-socks
	./pvd-test-saddr

Login on the client (aka host1) :
	ssh host1.local
	cd .../pvd/test/bound-socks
	./pvd-test-saddr -l
		=> grab one pvd in the pvd list
	./pvd-test-saddr -r 2001:db8:cafe:ffff:babe
		=> this uses the default source address selection algorithm (aka router2 [longest prefix])
	./pvd-test-saddr -r 2001:db8:cafe:ffff:babe -p <pvd1>
		=> uses pvd1 in the route and source address selection algorithm
	./pvd-test-saddr -r 2001:db8:cafe:ffff:babe -p <pvd2>
		=> uses pvd2 in the route and source address selection algorithm
~~~~

With the above configuration, the client (_host1_) shoud choose an address of the _router2_ advertised prefix
(longest match). If _pirl1.cisco.com_ is specified, the selected address shoud be one of the _router1_
advertised prefix.

