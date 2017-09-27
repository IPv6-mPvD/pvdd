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

#define	EQSTR(a, b)	(strcmp((a), (b)) == 0)

#undef	true
#undef	false
#define	true	(1 == 1)
#define	false	(1 == 0)

static	void	usage(FILE *fo)
{
	fprintf(fo, "usage : pvd-bound-socks [-h|--help] [<option>*]\n");
	fprintf(fo, "where option :\n");
	fprintf(fo, "\t-l|--list : print out the current pvd list\n");
	fprintf(fo, "\t-P|--process : bind the process to a pvd\n");
	fprintf(fo, "\t<pvdname> : first pvd name to bind to (socket/process scope)\n");
	fprintf(fo, "\t<pvdname2>: 2nd pvd name to bind the socket to in process scope\n");
	fprintf(fo, "\n");
	fprintf(fo, "In socket scope (default) :\n");
	fprintf(fo, "Open a socket, bind it to a pvd and retrieve the current ");
	fprintf(fo, "bound pvd set\n");
	fprintf(fo, "If pvdname == none or unspecified, no binding is done\n");
	fprintf(fo, "\n");
	fprintf(fo, "In process scope (-P option) :\n");
	fprintf(fo, "Bind the process to <pvdname>, retrieve its bound pvd,\n");
	fprintf(fo, "then create a socket and perform various binding operations\n");
	fprintf(fo, "to check how any bound pvd is inherited or not\n");
}

static	int	ShowPvd(int s, char *msg)
{
	char	BoundPvdName[PVDNAMSIZ];
	int	rc;

	if ((rc = sock_get_bound_pvd_relaxed(s, BoundPvdName)) == -1) {
		perror("sock_get_bound_pvd");
		return(-1);
	}

	if (rc == 0) {
		printf("[%s] Socket bound to no pvd\n", msg);
	} else {
		printf("[%s] Socket bound to pvd %s\n", msg, BoundPvdName);
	}
	return(0);
}

int	main(int argc, char **argv)
{
	int	rc;
	int	i;
	int	s;
	int	FlagProcess = false;
	int	FlagListPvd = false;
	int	FlagPvd = true;
	char	*PvdName = NULL;
	char	*PvdName2 = NULL;
	char	BoundPvdName[PVDNAMSIZ];

	for (i = 1; i < argc; i++) {
		if (EQSTR(argv[i], "-h") || EQSTR(argv[i], "--help")) {
			usage(stdout);
			return(0);
		}
		if (EQSTR(argv[i], "-l") || EQSTR(argv[i], "--list")) {
			FlagListPvd = true;
			continue;
		}
		if (EQSTR(argv[i], "-P") || EQSTR(argv[i], "--process")) {
			FlagProcess = true;
			continue;
		}
		if (PvdName == NULL) {
			PvdName = argv[i];
		}
		else {
			PvdName2 = argv[i];
		}
	}

	if (proc_get_bound_pvd(BoundPvdName) == -1 && errno == ENOPROTOOPT) {
		fprintf(stderr, "Kernel is lacking pvd support. Aborting...\n");
		return(1);
	}

	if (FlagListPvd) {
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
		return(0);
	}

	if (PvdName == NULL || EQSTR(PvdName, "none")) {
		FlagPvd = false;
	}
	else {
		FlagPvd = true;
	}

	/*
	 * Check process pvd binding
	 */
	if (FlagProcess) {
		if (FlagPvd && proc_bind_to_pvd(PvdName) == -1) {
			fprintf(stderr, "proc_bind_to_pvd(%s) : %s\n", PvdName, strerror(errno));
			return(1);
		} else
		if (! FlagPvd && proc_bind_to_nopvd() == -1) {
			perror("proc_bind_to_nopvd");
			fprintf(stderr, "Continuing anyway\n");
		}

		/*
		 * Read back the bound pvd
		 */
		if ((rc = proc_get_bound_pvd(BoundPvdName)) == -1) {
			perror("proc_get_bound_pvd");
			return(1);
		}
		if (rc == 0) {
			printf("Process bound to no pvd\n");
		}
		else {
			printf("Process bound to pvd %s\n", BoundPvdName);
		}
		/*
		 * Create a socket, do not bind it, and check for its binding
		 * (should be the process one)
		 */
		if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
			perror("socket");
			return(1);
		}
		if (ShowPvd(s, "Inherit") == -1) {
			return(1);
		}

		/*
		 * Now, force the socket to be not bound
		 */
		if (sock_bind_to_nopvd(s) == -1) {
			perror("sock_bind_to_nopvd");
			return(1);
		}
		if (ShowPvd(s, "Forced to no pvd") == -1) {
			return(1);
		}

		/*
		 * Bind the socket to another pvd (if any)
		 */
		if (PvdName2 != NULL) {
			if (sock_bind_to_pvd(s, PvdName2) == -1) {
				fprintf(stderr, "sock_bind_to_pvd(%s) : %s\n", PvdName2, strerror(errno));
				return(1);
			}
			if (ShowPvd(s, "Force to 2nd pvd") == -1) {
				return(1);
			}
		}

		/*
		 * Lastly, let the socket inherit of any bound pvd
		 */
		if (sock_inherit_bound_pvd(s) == -1) {
			perror("sock_inherit_bound_pvd");
			return(1);
		}
		if (ShowPvd(s, "Inherit") == -1) {
			return(1);
		}
		return(0);
	}

	/*
	 * Check socket pvd binding
	 */
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		return(1);
	}

	if (FlagPvd && sock_bind_to_pvd(s, PvdName) == -1) {
		fprintf(stderr, "sock_bind_to_pvd(%s) : %s\n", PvdName, strerror(errno));
		close(s);
		return(1);
	}
	if (! FlagPvd && sock_bind_to_nopvd(s) == -1) {
		perror("sock_bind_to_nopvd");
		fprintf(stderr, "Continuing anyway\n");
	}

	/*
	 * Read back the bound pvd
	 */
	ShowPvd(s, FlagPvd ? "Forced to 1st pvd" : "Force to no pvd");

	close(s);

	return(0);
}

/* ex: set ts=8 noexpandtab wrap: */
