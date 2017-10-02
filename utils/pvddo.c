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
 * pvddo : utility similar to su
 * Starts a command with a process bound pvd
 *
 * usage : pvddo [-p <pvdname>] command args
 * If pvdname = none, the process is forcibly set to no bound pvd
 * If the -p option is not provided or pvdname = inherit, the process is
 * inheriting its parent's bound pvd (so, basically, it does nothing
 * else than starting the command with its arguments)
 */

#define	_GNU_SOURCE	/* for execvpe */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <libpvd.h>

#undef	true
#undef	false
#define	true	(1 == 1)
#define	false	(1 == 0)

#define	EQSTR(a, b)	(strcmp((a), (b)) == 0)

#define	DIM(t)	(sizeof(t) / sizeof(t[0]))

static	void	usage(FILE *fo)
{
	fprintf(fo, "usage : pvddo [-p <pvdname>] <command> [<arg>*]\n");
	fprintf(fo, "If pvdname = none, the process is forcibly set to no bound pvd\n");
 	fprintf(fo, "If the -p option is not provided or pvdname = inherit, the process is\n");
 	fprintf(fo, "nheriting its parent's bound pvd (so, basically, it does nothing\n");
 	fprintf(fo, "else than starting the command with its arguments)\n");
}

int	main(int argc, char **argv, char **env)
{
	int	i = 1;
	int	j;
	char	*PvdName = NULL;

	if (EQSTR(argv[i], "-h") || EQSTR(argv[i], "--help")) {
		usage(stdout);
		return(0);
	}
	if (EQSTR(argv[i], "-p") || EQSTR(argv[i], "--pvd")) {
		if (++i >= argc) {
			fprintf(stderr, "Missing argument for --pvd\n");
			usage(stderr);
			return(1);
		}
		PvdName = strdup(argv[i++]);
	}

	/*
	 * The command and its arguments start at index i
	 */
	for (j = 0; i < argc; i++, j++) {
		argv[j] = argv[i];
	}
	argv[j++] = NULL;	/* we have room for this */

	if (PvdName != NULL && ! EQSTR(PvdName, "none")) {
		if (proc_bind_to_pvd(PvdName) == -1) {
			perror("proc_bind_to_pvd");
			return(1);
		}
	}

	if (execvpe(argv[0], argv, env) == -1) {
		fprintf(stderr, "execvp(%s) : %s\n", argv[0], strerror(errno));
	}
	return(1);
	
}

/* ex: set ts=8 noexpandtab wrap: */
