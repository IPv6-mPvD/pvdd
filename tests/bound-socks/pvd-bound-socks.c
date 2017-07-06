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
 * pvd-bound-socks : exercise the API to bind sockets to a set of
 * PvDs
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <libpvd.h>

#define	EQSTR(a, b)	(strcasecmp((a), (b)) == 0)

static	void	usage(FILE *fo)
{
	fprintf(fo, "usage : pvd-bound-socks [-h|--help] [<pvdname>]\n");
	fprintf(fo, "\n");
	fprintf(fo, "Open a socket, bind it to a pvd and retrieve the current ");
	fprintf(fo, "bound pvd set\n");
}

int	main(int argc, char **argv)
{
	int	rc;
	int	i;
	int	s;
	char	*PvdName = NULL;
	char	BoundPvdName[PVDNAMSIZ];

	for (i = 1; i < argc; i++) {
		if (EQSTR(argv[i], "-h") || EQSTR(argv[i], "--help")) {
			usage(stdout);
			return(0);
		}
		PvdName = argv[i];
	}

	if (PvdName == NULL) {
		usage(stderr);
		return(1);
	}

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		return(1);
	}

	if (sock_bind_to_pvd(s, PvdName) == -1) {
		fprintf(stderr, "sock_bind_to_pvd(%s) : %s\n", PvdName, strerror(errno));
		close(s);
		return(1);
	}

	rc = sock_get_bound_pvd(s, BoundPvdName);

	if (rc == -1) {
		fprintf(stderr, "sock_bind_to_pvd(%s) : %s\n", PvdName, strerror(errno));
		close(s);
		return(1);
	}

	if (rc > 0) {
		printf("Bound PvD : %s\n", BoundPvdName);
	}

	close(s);

	return(0);
}

/* ex: set ts=8 noexpandtab wrap: */
