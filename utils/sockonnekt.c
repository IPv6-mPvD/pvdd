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
 * sockonnekt.c : establish a connection with a server
 * and redirect incoming data to stdin of a forked
 * command and outcoming data from stdout to the server
 *
 * It performs automatic reconnection if needed with
 * the server
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <memory.h>
#include <signal.h>
#include <string.h>
#include <netdb.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>

#define	EQSTR(a, b)	(strcmp((a), (b)) == 0)

#undef	true
#undef	false
#define	true	(1 == 1)
#define	false	(1 == 0)

static	volatile int	lChildPid = -1;
static	volatile int	lFlagExit = false;

static	void	TrapChild(int sig)
{
	pid_t	pid;

	pid = wait(NULL);

	if (pid == lChildPid) {
		lFlagExit = true;
		lChildPid = -1;
	}
	signal(SIGCHLD, TrapChild);
}

// ConnectToServer : create a connection with the specified server
static int	ConnectToServer(char *Server, int Port)
{
	int on = 1;
	int s;
	struct sockaddr_in sa;
	struct hostent *hp;

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		return(-1);
	}

        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on));

	if ((hp = gethostbyname(Server)) == NULL) {
		return(-1);
	}
	sa.sin_family = hp->h_addrtype;
	memcpy(&sa.sin_addr.s_addr, hp->h_addr, hp->h_length);
	sa.sin_port = htons(Port);

	if (connect(s, (struct sockaddr *) &sa, sizeof(sa)) == -1) {
		// perror("connect");
		close(s);
		return(-1);
	}
	return(s);
}

static	void	usage(FILE *fp)
{
	fprintf(fp, "sockonnekt [-h|--help] [option]*\n");
	fprintf(fp, "with option :\n");
	fprintf(fp, "\t-p|--port <port>\n");
	fprintf(fp, "\t-s|--server <server name>\n");
	fprintf(fp, "\t-r|--reconnect\n");
	fprintf(fp, "\t-- <command string> : the command will be run via a shell\n");
}

int	main(int argc, char **argv)
{
	int	i;
	int	n;
	int	Reconnect = false;
	int	Port = -1;
	char	*Server = NULL;
	char	**ExecParams = alloca(sizeof(char *) * (argc + 4));
	int	fds1[2];
	int	fds2[2];
	int	ServerSock = -1;

	for (i = 1; i < argc; i++) {
		if (EQSTR(argv[i], "-h") || EQSTR(argv[i], "--help")) {
			usage(stdout);
			return(0);
		}
		if (EQSTR(argv[i], "-p") || EQSTR(argv[i], "--port")) {
			if (++i >= argc) {
				usage(stderr);
				return(1);
			}
			Port = atoi(argv[i]);
			continue;

		}
		if (EQSTR(argv[i], "-s") || EQSTR(argv[i], "--server")) {
			if (++i >= argc) {
				usage(stderr);
				return(1);
			}
			Server = argv[i];
			continue;
		}
		if (EQSTR(argv[i], "-r") || EQSTR(argv[i], "--reconnect")) {
			Reconnect = true;
			continue;
		}
		if (EQSTR(argv[i], "--")) {
			i++;
			break;
		}
	}

	if (Port == -1) {
		fprintf(stderr, "sockonnekt : missing port number\n");
		usage(stderr);
		return(1);
	}

	if (Server == NULL) {
		fprintf(stderr, "sockonnekt : missing server name\n");
		usage(stderr);
		return(1);
	}

	// Build the exec parameter
	n = 0;
	ExecParams[n++] = "/bin/sh";
	ExecParams[n++] = "-c";
	for (; i < argc; i++) {
		ExecParams[n++] = argv[i];
	}
	ExecParams[n++] = NULL;

	// Start the command
	if (pipe(fds1) == -1) {
		perror("pipe");
		return(1);
	}

	if (pipe(fds2) == -1) {
		perror("pipe");
		return(1);
	}

	signal(SIGCHLD, TrapChild);

	lChildPid = fork();

	if (lChildPid == -1) {
		perror("fork");
		return(1);
	}

	if (lChildPid == 0) {
		// Child => connect stdin/stdout to the
		// pipes created above. stderr remains
		// connected to the parent's stderr
		close(0); close(1);
		dup2(fds1[0], 0);
		dup2(fds2[1], 1);
		close(fds1[1]);
		close(fds2[0]);

		if (execv("/bin/sh", ExecParams) == -1) {
			perror("execv");
			return(1);
		}
		return(0);	// should not be reached
	}

	// Parent : monitor pipes/establishes connection with the server & Co
	lFlagExit = false;

	close(fds1[0]);
	close(fds2[1]);

	ServerSock = ConnectToServer(Server, Port);

	while (! lFlagExit) {
		fd_set	fdsI;
		int	n;
		char	msg[4096];
		struct timeval	tv;
		struct timeval	*PtTv;

		if (ServerSock == -1) {
			if (! Reconnect) {
				break;
			}
			ServerSock = ConnectToServer(Server, Port);
		}

		FD_ZERO(&fdsI);

		n = -1;
		if (ServerSock != -1) {
			FD_SET(ServerSock, &fdsI);
			if (ServerSock > n) n = ServerSock;
			PtTv = NULL;
		}
		else {
			tv.tv_sec = 1; tv.tv_usec = 0;
			PtTv = &tv;
		}

		if (fds2[0] != -1) {
			FD_SET(fds2[0], &fdsI);
			if (fds2[0] > n) n = fds2[0];
		}

		if (select(n + 1, &fdsI, NULL, NULL, PtTv) == -1) {
			usleep(10000);
			continue;
		}

		if (ServerSock != -1 && FD_ISSET(ServerSock, &fdsI)) {
			if ((n = recv(ServerSock, msg, sizeof(msg) - 1, MSG_DONTWAIT)) <= 0) {
				// Server disconnected
				close(ServerSock);
				ServerSock = -1;
			}
			else {
				write(fds1[1], msg, n);
			}
		}

		if (fds2[0] != -1 && FD_ISSET(fds2[0], &fdsI)) {
			if ((n = read(fds2[0], msg, sizeof(msg) - 1)) > 0) {
				msg[n] = '\0';
				// fprintf(stderr, "Reading %s from client\n", msg);
				if (ServerSock != -1 && write(ServerSock, msg, n) != n) {
					// Server error (or not ready)
					perror("write server");
					close(ServerSock);
					ServerSock = -1;
				}
			}
		}
	}

	printf("Exiting...\n");

	close(ServerSock);
	close(fds1[1]);
	close(fds2[0]);

	if (lChildPid != -1) {
		kill(lChildPid, SIGTERM);
		usleep(10000);
	}

	if (lChildPid != -1) {
		kill(lChildPid, SIGTERM);
		usleep(10000);
	}

	if (lChildPid != -1) {
		kill(lChildPid, SIGQUIT);
		usleep(10000);
	}

	if (lChildPid != -1) {
		kill(lChildPid, SIGKILL);
		usleep(10000);
	}

	return(0);
}

/* ex: set ts=8 noexpandtab wrap: */
