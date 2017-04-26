/*
 *
 * Async functions
 * Sync functions. This family will use a per-call connection with the server
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "pvdid-defs.h"

#undef	true
#undef	false
#define	true	(1 == 1)
#define	false	(1 == 0)

#define	DIM(t)		(sizeof(t) / sizeof(t[0]))
#define	EQSTR(a,b)	(strcmp((a), (b)) == 0)

#define	MAX_PVDID	1024	// TODO : have it dynamic instead

typedef	struct
{
	int	nPvdId;
	char	*pvdIdList[MAX_PVDID];
}	t_pvdid_list;

// WaitFor : read a buffer (in the case only one line is expected) and attempt
// to match it against the given pattern
static	int	WaitFor(int fd, char *pattern, void *s)
{
	char	msg[4096];
	int	n;

	if ((n = recv(fd, msg, sizeof(msg) - 1, MSG_DONTWAIT)) <= 0) {
		// Remote disconnected (n == 0) or read error
		return(-1);
	}
	msg[n] = '\0';
	return(sscanf(msg, pattern, s));
}

static	int	SendExact(int fd, char *s)
{
	return(write(fd, s, strlen(s)) == strlen(s) ? 0 : -1);
}

static	int	GetInt(char *s, int *PtN)
{
	int	n = 0;
	char	*pt;

	errno = 0;

	n = strtol(s, &pt, 10);

	if (errno == 0 && pt != s && *pt == '\0') {
		*PtN = n;
		return(0);
	}
	return(-1);
}

int	pvdid_connect(int Port)
{
	int s;
	struct sockaddr_in sa;

	if (Port == -1) {
		char	*EnvClientPort = NULL;

		if ((EnvClientPort = getenv("PVDID_PORT")) == NULL) {
			Port = DEFAULT_PVDID_PORT;
		}
		else {
			if (GetInt(EnvClientPort, &Port) == -1) {
				Port = DEFAULT_PVDID_PORT;
			}
		}
	}

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		return(-1);
	}

	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa.sin_port = htons(Port);

	if (connect(s, (struct sockaddr *) &sa, sizeof(sa)) == -1) {
		close(s);
		return(-1);
	}
	return(s);
}

// reopen_connection : given an initial connection where all parameters have been
// supplied, just reuse the same connection parameters to create a new connection
// with the server
static	int	reopen_connection(int fd)
{
	int s;
	struct sockaddr sa;
	socklen_t salen;

	// We want to use the same connection parameters as the general connection
	salen = sizeof(sa);

	if (getpeername(fd, &sa, &salen) == -1) {
		return(-1);
	}

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		return(-1);
	}

	if (connect(s, &sa, salen) == -1) {
		close(s);
		return(-1);
	}
	return(s);
}

int	pvdid_promote_control(int fd)
{
	int s;

	if ((s = reopen_connection(fd)) != -1) {
		if (SendExact(s, "PVDID_CONNECTION_PROMOTE_CONTROL\n") == -1) {
			close(s);
			return(-1);
		}

		// Credential validation will be performed by the server
	}

	return(s);

}

int	pvdid_get_pvdid_list(int fd)
{
	return(SendExact(fd, "PVDID_GET_LIST\n"));
}

// pvdid_parse_pvdid_list : fill in the output array with PvD names
// They need to be freed using free() by the caller
int	pvdid_parse_pvdid_list(char *msg, t_pvdid_list *pvdIdList)
{
	char	*pts = NULL;
	char	*pvdId = NULL;

	pvdIdList->nPvdId = 0;

	for (;(pvdId = strtok_r(msg, " ", &pts)) != NULL; msg = NULL) {
		// Ignore empty names (consecutive spaces)
		if (*pvdId == '\0') {
			continue;
		}
		if (pvdIdList->nPvdId < DIM(pvdIdList->pvdIdList)) {
			pvdIdList->pvdIdList[pvdIdList->nPvdId++] = strdup(pvdId);
		}
	}
	return(0);
}

int	pvdid_get_pvdid_list_sync(int fd, t_pvdid_list *pvdIdList)
{
	int s;
	int rc = -1;

	if ((s = reopen_connection(fd)) != -1) {
		if (pvdid_get_pvdid_list(s) == 0) {
			char msg[2048];

			if (WaitFor(s, "PVDID_LIST %[^\n]\n", msg) == 1) {
				// msg contains a list of space separated FQDN pvdIds
				pvdid_parse_pvdid_list(msg, pvdIdList);
				rc = 0;
			}
		}
		close(s);
	}
	return(rc);
}

int	pvdid_get_attributes(int fd, char *pvdId)
{
	char	s[2048];

	snprintf(s, sizeof(s) - 1, "PVDID_GET_ATTRIBUTES %s\n", pvdId);
	s[sizeof(s) - 1] = '\0';

	return(SendExact(fd, s));
}

// pvdid_get_attributes_sync : the attributes output parameter contain the
// JSON string of all attributes. It needs to be freed using free() by the
// caller
int	pvdid_get_attributes_sync(int fd, char *pvdId, char **attributes)
{
	int	s;
	int	rc = -1;

	if ((s = reopen_connection(fd)) != -1) {
		if (pvdid_get_attributes(s, pvdId) == 0) {
			;
		}
	}
	return(rc);
}

int	pvdid_get_attribute(int fd, char *pvdId, char *attrName)
{
	char	s[2048];

	snprintf(s, sizeof(s) - 1, "PVDID_GET_ATTRIBUTE %s %s\n", pvdId, attrName);
	s[sizeof(s) - 1] = '\0';

	return(SendExact(fd, s));
}

int	pvdid_get_attribute_sync(int fd, char *pvdId, char *attrName, char **attrValue)
{
	int	s;
	int	rc = -1;

	if ((s = reopen_connection(fd)) != -1) {
		if (pvdid_get_attribute(s, pvdId, attrName) == 0) {
			;
		}
	}
	return(rc);
}

int	pvdid_subscribe_notifications(int fd)
{
	return(SendExact(fd, "PVDID_SUBSCRIBE_NOTIFICATIONS\n"));
}

int	pvdid_unsubscribe_notifications(int fd)
{
	return(SendExact(fd, "PVDID_UNSUBSCRIBE_NOTIFICATIONS\n"));
}

int	pvdid_subscribe_pvdid_notifications(int fd, char *pvdId)
{
	char	s[2048];

	snprintf(s, sizeof(s) - 1, "PVDID_SUBSCRIBE %s\n", pvdId);
	s[sizeof(s) - 1] = '\0';

	return(SendExact(fd, s));
}

int	pvdid_unsubscribe_pvdid_notifications(int fd, char *pvdId)
{
	char	s[2048];

	snprintf(s, sizeof(s) - 1, "PVDID_UNSUBSCRIBE %s\n", pvdId);
	s[sizeof(s) - 1] = '\0';

	return(SendExact(fd, s));
}

/* ex: set ts=8 noexpandtab wrap: */
