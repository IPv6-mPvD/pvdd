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

#include <libpvdid.h>

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

	if (sock_get_bound_pvd(s, BoundPvdName) == -1) {
		fprintf(stderr, "sock_bind_to_pvd(%s) : %s\n", PvdName, strerror(errno));
		close(s);
		return(1);
	}

	printf("Bound PvD : %s\n", BoundPvdName);

	close(s);

	return(0);
}

/* ex: set ts=8 noexpandtab wrap: */
