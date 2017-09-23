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
 * pvd-test-saddr : client/server (both parts in the same program) to test
 * the route and source address selection algorithm when a PvD is chosen by
 * an application
 *
 * We are using a set of kvm instances for validation :
 *
 *
 *                      + ----------------------- +
 *                      + host 1 (patched kernel) +
 *                      + ----------------------- +
 *                                  |
 *   ----------------------------------------------------------  net 1
 *                   |                       |
 *         + -------------- +        + -------------- +
 *         + router1 - pvd1 +        + router2 - pvd2 +
 *         + -------------- +        + -------------- +
 *                   |                       |
 *   ----------------------------------------------------------  net 2
 *                                  |
 *                              + ------ +
 *                              + host 2 +
 *                              + ------ +
 *
 * host 1 will connect to host 2 via either pvd1, either pvd2, either no pvd
 * host 2 displays the peer address of the connection
 *
 * router1/2 llas and advertised routes/prefixes will be such that by default
 * (no pvd specified), saddr created from router2 will be chosen for packets
 * going through router1
 */

#define _GNU_SOURCE	// to have in6_pktinfo defined

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <libpvd.h>

#undef	true
#undef	false
#define	true	(1 == 1)
#define	false	(1 == 0)

#define	EQSTR(a, b)	(strcmp((a), (b)) == 0)

#define	DIM(t)	(sizeof(t) / sizeof(t[0]))

#define	PORT	8300

#define	MAXSOCKETS	16

#define	INMSGSIZE	16
#define	PEERNAMESIZE	128

#define	TCPMODE			1
#define	UDPMODE			2
#define	CONNECTEDUDPMODE	3

static	void	usage(FILE *fo)
{
	fprintf(fo, "usage : pvd-test-saddr [-h|--help] [<option>*]\n");
	fprintf(fo, "where option :\n");
	fprintf(fo, "\t-r|--remote <h:o:s:t:-:I:P:v:6> : IPv6 dotted address of the server\n");
	fprintf(fo, "\t-p|--pvd <pvdname> : selected pvd (optional)\n");
	fprintf(fo, "\t-c|--count <#> : loops counts (default 1)\n");
	fprintf(fo, "\t-i|--interval <#> : interval (in ms) between 2 loops (500 ms by default)\n");
	fprintf(fo, "\t-l|--list : print out the current pvd list\n");
	fprintf(fo, "\t-u|--udp : client uses connectionless UDP (TCP default)\n");
	fprintf(fo, "\t-U|--UDP : client uses connected UDP (TCP default)\n");
	fprintf(fo, "\n");
	fprintf(fo, "Open a socket, bind it to a pvd and connect to server, them perform\n");
	fprintf(fo, "a send/receive loop (the server is sending the client's address to the\n");
	fprintf(fo, "client)\n");
	fprintf(fo, "\n");
	fprintf(fo, "Multiple pvd can be specified. In this case, the client opens as many\n");
	fprintf(fo, "connections with the server with the specified pvds. Specifying 'none' as\n");
	fprintf(fo, "a pvd name means that no pvd will be attached to the associated socket\n");
	fprintf(fo, "If no option is specified, act as a server waiting for connection and\n");
	fprintf(fo, "displaying peer's address. Note that the server always listens for TCP\n");
	fprintf(fo, "and UDP connections\n");
	fprintf(fo, "\n");
	fprintf(fo, "'Connected UDP' means that the client is calling 'connect()' on the socket\n");
	fprintf(fo, "and 'send()' instead of 'sendto()' to send the data to the server\n");
	fprintf(fo, "\n");
	fprintf(fo, "Example :\n");
	fprintf(fo, "./pvd-test-saddr -u -r ::1 -p pvd1.my.org -p pvd2.my.org -p none -c 10 -i 1200\n");
	fprintf(fo, "This creates 3 UDP connection with the server (on localhost) and performs 10\n");
	fprintf(fo, "send/receive loops, each separated by 1.2 seconds\n");
}

/*
 * RecvUdpSocket : alternate implementation of recvfrom() to allow the receiver
 * (here the server) to retrieve the destination address of the incoming
 * datagram so that we can create a dedicated UDP socket connected to the client
 * via the proper IPv6 address to send back the response
 */
static	int	RecvUdpSocket(
			int sock,
			char *buf, int size,
			struct sockaddr *sa, socklen_t *len,
			int *ReturnSocket)
{
	struct msghdr		msg;
	struct cmsghdr		*cmsg;
	struct iovec		iov;
	struct in6_pktinfo	*pktinfo;
	char			ancillary[64];	/* to receive the destination address */
	char			DstAddress[64];
	int			n;
	struct sockaddr_in6	sa6;

	iov.iov_base = buf;
	iov.iov_len = size;

	msg.msg_name = sa;
	msg.msg_namelen = *len;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = ancillary;
	msg.msg_controllen = sizeof(ancillary);
	msg.msg_flags = 0;

	if ((n = recvmsg(sock, &msg, 0)) == -1) {
		return(-1);
	}

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level != IPPROTO_IPV6 ||
		    cmsg->cmsg_type != IPV6_PKTINFO) {
			continue;
		}
		if (cmsg->cmsg_len != CMSG_LEN(sizeof(struct in6_pktinfo))) {
			continue;
		}
		pktinfo = (struct in6_pktinfo *) CMSG_DATA(cmsg);

		if (inet_ntop(AF_INET6, 
			      &pktinfo->ipi6_addr,
			      DstAddress, sizeof(DstAddress)) == NULL) {
			continue;
		}
			      
		printf("Receiving interface and addresses : %d, %s\n",
			pktinfo->ipi6_ifindex,
			DstAddress);

		/*
		 * Create and configure the return socket
		 */
		if ((sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
			return(-1);
		}

		sa6.sin6_family = AF_INET6;
		sa6.sin6_addr = pktinfo->ipi6_addr;
		sa6.sin6_port = htons(0);
		if (bind(sock, (struct sockaddr *) &sa6, sizeof(sa6)) == -1) {
			return(-1);
		}
		*ReturnSocket = sock;
	}
	return(n);
}

/*
 * The server part : it listens for TCP and UDP connections.
 * It reads an incoming message, then sends back a message containing
 * the client's IPv6 address prefixed with the incoming message
 */
static	int	RecvFromSocket(int s, int FlagUdp)
{
	struct sockaddr_in6 sa6;
	socklen_t	salen = sizeof(sa6);
	char		PeerName[PEERNAMESIZE];
	char		Msg[INMSGSIZE];
	int		n;
	int		ReturnSock = s;

	if (FlagUdp) {
		n = RecvUdpSocket(
			s,
			Msg, sizeof(Msg),
			(struct sockaddr *) &sa6, &salen,
			&ReturnSock);
	}
	else {
		if (getpeername(s, (struct sockaddr *) &sa6, &salen) == -1) {
			perror("getpeername");
			return(-1);
		}
		n = recv(s, Msg, sizeof(Msg), 0);
	}

	if (n > 0) {
		sprintf(PeerName, "%s : ", Msg);

		if (inet_ntop(AF_INET6, 
			      &sa6.sin6_addr,
			      &PeerName[strlen(PeerName)], sizeof(PeerName)) == NULL) {
			perror("inet_ntop");
			return(-1);
		}
		else {
			printf("%s : %s\n", FlagUdp ? "UDP" : "TCP", PeerName);
			if (sendto(ReturnSock,
				   PeerName, sizeof(PeerName),
				   0, 
				   (struct sockaddr *) &sa6, sizeof(sa6)) == -1) {
				perror("sendto");
				return(-1);
			}
			if (ReturnSock != s) {
				printf("Closing new socket\n");
				shutdown(ReturnSock, SHUT_RDWR);
				close(ReturnSock);
			}
		}
	}
	else {
		if (n < 0) {
			fprintf(stderr,
				"%s : %s\n", 
				FlagUdp ? "UDP" : "TCP",
				strerror(errno));
		}
		return(-1);
	}
	return(0);
}

static	int	Server(void)
{
	int	i, j;
	int	TCPSocket;
	int	UDPSocket;
	int	one = 1;
	struct sockaddr_in6 sa6;
	int	NClients = 0;
	int	ClientSockets[MAXSOCKETS];

	memset((char *) &sa6, 0, sizeof(sa6));

	sa6.sin6_family = AF_INET6;
	sa6.sin6_addr = in6addr_any;
	sa6.sin6_port = htons(PORT);

	/*
	 * Create the TCP socket (we dont mind closing the sockets
	 * since returning error directly leads to exiting)
	 */
	if ((TCPSocket = socket(AF_INET6, SOCK_STREAM, 0)) == -1) {
		perror("TCP socket");
		return(-1);
	}

	if (setsockopt(TCPSocket, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) == -1) {
		perror("SO_REUSEPORT");
		return(-1);
	}

	if (bind(TCPSocket, (struct sockaddr *) &sa6, sizeof(sa6)) < 0) {
		perror("TCP bind");
		return(-1);
	}

	if (listen(TCPSocket, 10) == -1) {
		perror("TCP listen");
		return(-1);
	}

	/*
	 * Idem for the UDP socket. Configure it so that we can retrieve
	 * the destination address of incoming datagrams
	 */
	if ((UDPSocket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		perror("UDP socket");
		return(-1);
	}

	if (setsockopt(UDPSocket, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) == -1) {
		perror("SO_REUSEPORT");
		return(-1);
	}

	if (bind(UDPSocket, (struct sockaddr *) &sa6, sizeof(sa6)) < 0) {
		perror("UDP bind");
		return(-1);
	}
 
	if (setsockopt(UDPSocket, IPPROTO_IPV6, IPV6_RECVPKTINFO, &one, sizeof(one)) == -1) {
		fprintf(stderr, "setsockopt(IPV6_RECVPKTINFO): %s\n", strerror(errno));
		return(-1);
	}

	/*
	 * Main loop : we wait for messages coming on the UDP, TCP server sockets,
	 * as well as on the TCP clients sockets
	 */
	while (1) {
		fd_set		fdsI;
		int		nFds = -1;
		socklen_t	salen = sizeof(sa6);

		FD_ZERO(&fdsI);

		FD_SET(TCPSocket, &fdsI);
		if (TCPSocket > nFds) nFds = TCPSocket;

		FD_SET(UDPSocket, &fdsI);
		if (UDPSocket > nFds) nFds = UDPSocket;

		for (i = 0; i < NClients; i++) {
			FD_SET(ClientSockets[i], &fdsI);
			if (ClientSockets[i] > nFds) nFds = ClientSockets[i];
		}

		if (select(nFds + 1, &fdsI, NULL, NULL, NULL) == -1) {
			perror("server : select");
			exit(1);
		}

		if (FD_ISSET(TCPSocket, &fdsI)) {
			char	PeerName[PEERNAMESIZE];
			int	sc = accept(
					TCPSocket, (struct sockaddr *) &sa6, &salen);

			if (sc == -1) {
				perror("accept");
			} else
			if (NClients >= DIM(ClientSockets)) {
				fprintf(stderr, "Too many TCP clients\n");
				close(sc);
			}
			else {
				ClientSockets[NClients++] = sc;
				if (inet_ntop(AF_INET6, 
					      &sa6.sin6_addr,
					      PeerName, sizeof(PeerName)) == NULL) {
					perror("inet_ntop");
					close(sc);
				}
			}
		}

		if (FD_ISSET(UDPSocket, &fdsI)) {
			RecvFromSocket(UDPSocket, true);
		}

		for (i = 0; i < NClients; i++) {
			if (FD_ISSET(ClientSockets[i], &fdsI)) {
				if (RecvFromSocket(ClientSockets[i], false) == -1) {
					shutdown(ClientSockets[i], SHUT_RDWR);
					close(ClientSockets[i]);
					ClientSockets[i] = -1;
				}
			}
		}

		/*
		 * Compact the clients sockets array in case we have closed
		 * some of them
		 */
		for (i = 0, j = 0; i < NClients; i++) {
			if (ClientSockets[i] != -1) {
				ClientSockets[j] = ClientSockets[i];
				j++;
			}
		}
		NClients = j;
	}
}


static	void	CloseSockets(int Sockets[], int NSockets)
{
	int	i;

	for (i = 0; i < NSockets; i++) {
		shutdown(Sockets[i], SHUT_RDWR);
		close(Sockets[i]);
	}
}

static	int	CreateSocket(
			int ConnectionMode,
			int Sockets[],
			int *NSockets,
			struct sockaddr_in6 *ServerSa6,
			char *ServerName,	/* for logs purpose */
			char *PvdName)
{
	int	s;

	if ((s = socket(AF_INET6,
			ConnectionMode != TCPMODE ? SOCK_DGRAM : SOCK_STREAM,
			ConnectionMode != TCPMODE ? IPPROTO_UDP : 0)) == -1) {
		perror("socket");
		return(-1);
	}

	/*
	 * If a pvd name has been specified, use it
	 */
	if (PvdName != NULL && ! EQSTR(PvdName, "none") &&
	    sock_bind_to_pvd(s, PvdName) == -1) {
		fprintf(stderr,
			"sock_bind_to_pvd(%s) : %s\n",
			PvdName,
			strerror(errno));
		close(s);
		return(-1);
	}

 	/*
	 * Establish a connection with the server in TCP mode or
	 * in UDP mode when the -U flag has been specified
	 */
	if (ConnectionMode != UDPMODE) {
		if (connect(s, (struct sockaddr *) ServerSa6, sizeof(*ServerSa6)) == -1) {
			perror(ServerName);
			close(s);
			return(-1);
		}
		else {
			printf("Connection with pvd %s OK\n", PvdName);
		}
	}
	Sockets[(*NSockets)++] = s;

	return(s);
}

int	main(int argc, char **argv)
{
	int	i;
	int	NSockets = 0;
	int	Sockets[MAXSOCKETS];
	int	NPvd = 0;
	char	*PvdName[MAXSOCKETS];
	char	*RemoteHost = NULL;
	struct in6_addr	sin6;
	struct sockaddr_in6 sa6;
	struct sockaddr_in6 OtherSa6;
	socklen_t	OtherSa6Len;
	char	PeerName[PEERNAMESIZE];
	int	ShowPvdList = false;
	int	Count = 1;
	int	Interval = 500;
	int	ConnectionMode = TCPMODE;
	char	*Syscall;

	char	*pt;

	for (i = 1; i < argc; i++) {
		if (EQSTR(argv[i], "-h") || EQSTR(argv[i], "--help")) {
			usage(stdout);
			return(0);
		}
		if (EQSTR(argv[i], "-p") || EQSTR(argv[i], "--pvd")) {
			if (i < argc - 1) {
				if (NPvd >= DIM(PvdName)) {
					fprintf(stderr,
						"Too many pvd specified (%d maximum)\n",
						(int) DIM(PvdName));
					return(1);
				}
				PvdName[NPvd++] = argv[++i];
			}
			else {
				usage(stderr);
				return(-1);
			}
			continue;
		}
		if (EQSTR(argv[i], "-r") || EQSTR(argv[i], "--remote")) {
			if (i < argc - 1) {
				RemoteHost = argv[++i];
			}
			else {
				usage(stderr);
				return(-1);
			}
			continue;
		}
		if (EQSTR(argv[i], "-i") || EQSTR(argv[i], "--interval")) {
			if (i < argc - 1) {
				Interval = strtol(argv[++i], &pt, 10);
				if (pt == NULL || *pt != '\0' || errno == ERANGE) {
					fprintf(stderr,
						"%s : invalid interval value\n",
						argv[i]);
					usage(stderr);
					return(1);
				}
			}
			else {
				usage(stderr);
				return(-1);
			}
			continue;
		}
		if (EQSTR(argv[i], "-c") || EQSTR(argv[i], "--count")) {
			if (i < argc - 1) {
				Count = strtol(argv[++i], &pt, 10);
				if (pt == NULL || *pt != '\0' || errno == ERANGE) {
					fprintf(stderr,
						"%s : invalid count value\n",
						argv[i]);
					usage(stderr);
					return(1);
				}
			}
			else {
				usage(stderr);
				return(-1);
			}
			continue;
		}
		if (EQSTR(argv[i], "-u") || EQSTR(argv[i], "--udp")) {
			ConnectionMode = UDPMODE;
			continue;
		}
		if (EQSTR(argv[i], "-U") || EQSTR(argv[i], "--UDP")) {
			ConnectionMode = CONNECTEDUDPMODE;
			continue;
		}
		if (EQSTR(argv[i], "-l") || EQSTR(argv[i], "--list")) {
			ShowPvdList = true;
			continue;
		}

		/*
		 * Unknown option
		 */
		usage(stderr);
		return(-1);
	}

	if (Count < 0) {
		Count = 1;
	}

	if (Interval < 100) {
		Interval = 100;
	}

	printf("Starting test with %d loops at interval %d ms\n", Count, Interval);
	printf("%s mode activated\n",
		ConnectionMode == UDPMODE ? "UDP" :
		ConnectionMode == CONNECTEDUDPMODE ? "CONNECTED UDP" : "TCP");

	if (ShowPvdList) {
		struct pvd_list pvl;

		pvl.npvd = MAXPVD;
		if (kernel_get_pvdlist(&pvl) != -1) {
			printf("Pvd list : %d pvds\n", pvl.npvd);
			for (i = 0; i < pvl.npvd; i++) {
				printf("\t%s\n", pvl.pvds[i]);
			}
		}
		else {
			perror("kernel_get_pvdlist");
		}
		return(1);
	}

	if (RemoteHost == NULL) {
		printf("Starting in server mode (port = %d)\n", PORT);
		Server();
		return(0);
	}

	/*
	 * Client part : creates the sockets and performs the requested
	 * number of loops
	 */
	if (inet_pton(AF_INET6, RemoteHost, &sin6) == -1) {
		perror(RemoteHost);
		return(1);
	}

	sa6.sin6_family = AF_INET6;
	sa6.sin6_addr = sin6;
	sa6.sin6_port = htons(PORT);
	sa6.sin6_flowinfo = 0;
	sa6.sin6_scope_id = 0;

	if (NPvd == 0) {
		if (CreateSocket(
				ConnectionMode,
				Sockets, &NSockets,
				&sa6,
				RemoteHost,
				"none") == -1) {
			CloseSockets(Sockets, NSockets);
			return(1);
		}
	}
	else {
		for (i = 0; i < NPvd; i++) {
			if (CreateSocket(
					ConnectionMode,
					Sockets, &NSockets,
					&sa6,
					RemoteHost,
					PvdName[i]) == -1) {
				CloseSockets(Sockets, NSockets);
				return(1);
			}
		}
	}

	/*
	 * Main loop : for each socket, sends data, then, for each socket again,
	 * waits for a message
	 */
	for (; Count > 0; Count--) {
		char	Msg[INMSGSIZE];

		memset(Msg, 0, sizeof(Msg));
		sprintf(Msg, "%d", Count);

		/*
		 * Send messages
		 */
		for (i = 0; i < NSockets; i++) {
			if (ConnectionMode == UDPMODE) {
				if (sendto(
					Sockets[i],
					Msg, sizeof(INMSGSIZE),
					0, 
					(struct sockaddr *) &sa6, sizeof(sa6)) == -1) {

					Syscall = "sendto"; goto QuitAndClose;
				}
			} else
			if (ConnectionMode == CONNECTEDUDPMODE) {
				if (send(Sockets[i],
					 Msg, sizeof(INMSGSIZE), 0) == -1) {

					Syscall = "send"; goto QuitAndClose;
				}
			}
			else {
				if (write(Sockets[i],
					  Msg, strlen(Msg) + 1) == -1) {
					Syscall = "write"; goto QuitAndClose;
				}
			}
		}

		/*
		 * Read answers from the server
		 */
		for (i = 0; i < NSockets; i++) {
			int	n;

			if (ConnectionMode != TCPMODE) {
				if (recvfrom(
					Sockets[i],
					PeerName, sizeof(PeerName),
					0,
					(struct sockaddr *) &OtherSa6, &OtherSa6Len) == -1) {

					perror("recvfrom");
					CloseSockets(Sockets, NSockets);
					return(1);
				}
			}
			else {
				if ((n = read(Sockets[i], PeerName, sizeof(PeerName))) < 0) {
					perror("read");
					CloseSockets(Sockets, NSockets);
					return(1);
				}
			}
			printf("[%d] pvd %s : My IPv6 address : %s\n",
				Count,
				NPvd == 0 ? "none" : PvdName[i],
				PeerName);
		}

		if (Count > 1) {
			usleep(Interval * 1000L);
		}
	}

	CloseSockets(Sockets, NSockets);

	return(0);

QuitAndClose :
	perror(Syscall);
	CloseSockets(Sockets, NSockets);
	return(1);
}

/* ex: set ts=8 noexpandtab wrap: */
