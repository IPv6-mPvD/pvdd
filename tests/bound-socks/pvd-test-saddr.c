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
 *         + -------------- +        + ---------------- +
 *         + router1 - pvd1 +        + router2 - pvd2   +
 *         + -------------- +        + ---------------- +
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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <libpvd.h>

#undef	true
#undef	false
#define	true	(1 == 1)
#define	false	(1 == 0)

#define	EQSTR(a, b)	(strcasecmp((a), (b)) == 0)

#define	PORT	8300

static	void	usage(FILE *fo)
{
	fprintf(fo, "usage : pvd-test-saddr [-h|--help] [<option>*]\n");
	fprintf(fo, "where option :\n");
	fprintf(fo, "\t-r|--remote <h:o:s:t:-:I:P:v:6> : IPv6 dotted address of the server\n");
	fprintf(fo, "\t-p|--pvd <pvdname> : selected pvd (optional)\n");
	fprintf(fo, "\t-l|--list : print out the current pvd list\n");
	fprintf(fo, "\n");
	fprintf(fo, "Open a socket, bind it to a pvd and connect to server\n");
	fprintf(fo, "\n");
	fprintf(fo, "If no option is specified, act as a server waiting for connection and\n");
	fprintf(fo, "displaying peer's address\n");
}

static	int	Server(void)
{
	int	s;
	int	one = 1;
	struct sockaddr_in6 sa6;

	if ((s = socket(AF_INET6, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		return(-1);
	}

	if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) == -1) {
		perror("SO_REUSEPORT");
		close(s);
		return(-1);
	}

	memset((char *) &sa6, 0, sizeof(sa6));

	sa6.sin6_family = AF_INET6;
	sa6.sin6_addr = in6addr_any;
	sa6.sin6_port = htons(PORT);

	if (bind(s, (struct sockaddr *) &sa6, sizeof(sa6)) < 0) {
		perror("bind");
		close(s);
		return(-1);
	}

	if (listen(s, 10) == -1) {
		perror("listen");
		close(s);
		return(-1);
	}

	while (1) {
		socklen_t	salen = sizeof(sa6);
		int		sc = accept(s, (struct sockaddr *) &sa6, &salen);

		if (sc == -1) {
			perror("accept");
		}
		else {
			char PeerName[128];

			if (inet_ntop(AF_INET6, &sa6.sin6_addr, PeerName, sizeof(PeerName) - 1) == NULL) {
				perror("inet_ntop");
			}
			else {
				printf("Remote host connected : %s\n", PeerName);
				(void) write(sc, PeerName, strlen(PeerName) + 1);
			}
			close(sc);
		}
	}
}


int	main(int argc, char **argv)
{
	int	i;
	int	s;
	char	*PvdName = NULL;
	char	*RemoteHost = NULL;
	struct in6_addr	sin6;
	struct sockaddr_in6 sa6;
	char	PeerName[128];
	int	ShowPvdList = false;

	for (i = 1; i < argc; i++) {
		if (EQSTR(argv[i], "-h") || EQSTR(argv[i], "--help")) {
			usage(stdout);
			return(0);
		}
		if (EQSTR(argv[i], "-p") || EQSTR(argv[i], "--pvd")) {
			if (i < argc - 1) {
				PvdName = argv[++i];
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
	 * Client part : establish a socket connection with the IPv6 remote
	 */
	if ((s = socket(AF_INET6, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		return(1);
	}

	/*
	 * If a pvd name has been specified, use it
	 */
	if (PvdName != NULL && sock_bind_to_pvd(s, PvdName) == -1) {
		fprintf(stderr, "sock_bind_to_pvd(%s) : %s\n", PvdName, strerror(errno));
		close(s);
		return(1);
	}

	if (inet_pton(AF_INET6, RemoteHost, &sin6) == -1) {
		perror(RemoteHost);
		return(1);
	}

	sa6.sin6_family = AF_INET6;
	sa6.sin6_addr = sin6;
	sa6.sin6_port = htons(PORT);
	sa6.sin6_flowinfo = 0;
	sa6.sin6_scope_id = 0;

	if (connect(s, (struct sockaddr *) &sa6, sizeof(sa6)) == -1) {
		perror(RemoteHost);
		return(1);
	}

	if (read(s, PeerName, sizeof(PeerName) - 1) >= 0) {
		printf("My IPv6 address : %s\n", PeerName);
	}

	shutdown(s, SHUT_RDWR);

	close(s);

	return(0);
}

/* ex: set ts=8 noexpandtab wrap: */
