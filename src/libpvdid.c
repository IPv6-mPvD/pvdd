/*
 * INSERT PROPER HEADER HERE
 */

/*
 *
 * Async functions
 * Sync functions. This family will use a per-call connection with the server
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "pvdid-defs.h"
#include "pvdid-utils.h"

#include "libpvdid.h"

#undef	true
#undef	false
#define	true	(1 == 1)
#define	false	(1 == 0)

#define	DIM(t)		(sizeof(t) / sizeof(t[0]))
#define	EQSTR(a,b)	(strcmp((a), (b)) == 0)

// ReadMsg : reads an incoming message on a binary socket. The first
// bytes of the message carry the total length to read
static	int	ReadMsg(int fd, char **String)
{
	int		len;
	char		msg[1024];
	t_StringBuffer	SB;
	int		n;

	fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) & ~O_NONBLOCK);

	SBInit(&SB);

	if (recv(fd, &len, sizeof(len), MSG_WAITALL) != sizeof(len)) {
		return(-1);
	}
	SBAddString(&SB, "");

	while (len > 0) {
		if ((n = read(fd, msg, sizeof(msg) - 1)) <= 0) {
			// Remote disconnected (n == 0) or read error
			SBUninit(&SB);
			return(-1);
		}
		msg[n] = '\0';
		SBAddString(&SB, "%s", msg);
		len -= n;
	}

	*String = SB.String;

	// printf("ReadMsg : msg = %s\n", *String);

	return(0);
}

static	int	SendExact(int fd, char *s)
{
	return(write(fd, s, strlen(s)) == strlen(s) ? 0 : -1);
}

// StripSpaces : remove heading spaces from a string and returns
// the address of the first non space (this includes \n) character
// of the string
static	char	*StripSpaces(char *s)
{
	while (*s != '\0' && *s <= ' ') s++;

	return(s);
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

// pvdid_connect : returns a general connection (socket) with the pvdid
// daemon
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

int	pvdid_get_control_socket(int fd)
{
	int s;

	if ((s = reopen_connection(fd)) != -1) {
		if (SendExact(s, "PVDID_CONNECTION_PROMOTE_CONTROL\n") == -1) {
			close(s);
			return(-1);
		}
	}

	return(s);

}

int	pvdid_get_binary_socket(int fd)
{
	int s;

	if ((s = reopen_connection(fd)) != -1) {
		if (SendExact(s, "PVDID_CONNECTION_PROMOTE_BINARY\n") == -1) {
			close(s);
			return(-1);
		}
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
	int	s;
	int	rc = -1;
	char	*msg;
	char	pvdString[2048];

	if ((s = pvdid_get_binary_socket(fd)) != -1) {
		if (pvdid_get_pvdid_list(s) == 0 && ReadMsg(s, &msg) == 0) {
			if (sscanf(msg, "PVDID_LIST %[^\n]\n", pvdString) == 1) {
				// pvdString contains a list of space separated FQDN pvdIds
				pvdid_parse_pvdid_list(pvdString, pvdIdList);
				rc = 0;
			}
			free(msg);
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
	char	*msg;
	char	Pattern[2048];

	*attributes = NULL;

	if ((s = pvdid_get_binary_socket(fd)) != -1) {
		if (pvdid_get_attributes(s, pvdId) == 0 &&
		    ReadMsg(s, &msg) == 0) {
			sprintf(Pattern, "PVDID_ATTRIBUTES %s", pvdId);

			if (strncmp(msg, Pattern, strlen(Pattern)) == 0) {
				*attributes = strdup(StripSpaces(&msg[strlen(Pattern)]));
			}
			free(msg);
		}
		close(s);
	}
	return(*attributes == NULL ? -1 : 0);
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
	char	*msg;
	char	Pattern[2048];

	*attrValue = NULL;

	if ((s = pvdid_get_binary_socket(fd)) != -1) {
		if (pvdid_get_attribute(s, pvdId, attrName) == 0 &&
		    ReadMsg(s, &msg) == 0) {
			sprintf(Pattern, "PVDID_ATTRIBUTE %s %s", pvdId, attrName);

			if (strncmp(msg, Pattern, strlen(Pattern)) == 0) {
				*attrValue = strdup(StripSpaces(&msg[strlen(Pattern)]));
			}

			free(msg);
		}
		close(s);
	}

	return(*attrValue == NULL ? -1 : 0);
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

// ParseStringArray : given a strings ["...", "...", ...], returns
// the  different ... strings in the given array
// The substrings must not contain ] or "
static	int	ParseStringArray(char *msg, char **Array, int Size)
{
	int	n = 0;
	char	OneString[2048];
	char	*pt = OneString;
	int	InString = false;

	if (msg[0] == '[') {
		msg++;
		while (*msg != '\0' && *msg != ']' && n < Size) {
			if (*msg == '"') {
				if (InString) {
					InString = false;
					*pt = '\0';
					Array[n++] = strdup(OneString);
				}
				else {
					InString = true;
					pt = OneString;
				}
			} else
			if (InString) {
				*pt++ = *msg;
			}

			msg++;
		}
	}
	return(n);
}

// pvdid_parse_rdnss : msq contains a JSON array of strings
// The string can either be alone on the line, either preceded
// by PVDID_ATTRIBUTE <pvdId> RDNSS. These strings are in6 addresses
int	pvdid_parse_rdnss(char *msg, t_pvdid_rdnss *PtRdnss)
{
	char	Rdnss[2048];

	if (sscanf(msg, "PVDID_ATTRIBUTE %*[^ ] RDNSS %[^\n]", Rdnss) == 1) {
		msg = Rdnss;
	}

	PtRdnss->nRdnss = ParseStringArray(msg, PtRdnss->Rdnss, DIM(PtRdnss->Rdnss));

	return(0);
}

void	pvdid_release_rdnss(t_pvdid_rdnss *PtRdnss)
{
	int	i;

	for (i = 0; i < PtRdnss->nRdnss; i++) {
		free(PtRdnss->Rdnss[i]);
	}
	PtRdnss->nRdnss = 0;
}

// pvdid_parse_dnssl : msq contains a JSON array of strings
// The string can either be alone on the line, either preceded
// by PVDID_ATTRIBUTE <pvdId> DNSSL
int	pvdid_parse_dnssl(char *msg, t_pvdid_dnssl *PtDnssl)
{
	char	Dnssl[2048];

	if (sscanf(msg, "PVDID_ATTRIBUTE %*[^ ] DNSSL %[^\n]", Dnssl) == 1) {
		msg = Dnssl;
	}

	PtDnssl->nDnssl = ParseStringArray(msg, PtDnssl->Dnssl, DIM(PtDnssl->Dnssl));

	return(0);
}

void	pvdid_release_dnssl(t_pvdid_dnssl *PtDnssl)
{
	int	i;

	for (i = 0; i < PtDnssl->nDnssl; i++) {
		free(PtDnssl->Dnssl[i]);
	}
	PtDnssl->nDnssl = 0;
}

int	pvdid_get_rdnss(int fd, char *pvdId)
{
	char	s[2048];

	sprintf(s, "PVDID_GET_ATTRIBUTE %s RDNSS\n", pvdId);

	return(SendExact(fd, s));
}

int	pvdid_get_rdnss_sync(int fd, char *pvdId, t_pvdid_rdnss *PtRdnss)
{
	int	s;
	int	rc = -1;
	char	*msg;
	char	Pattern[2048];

	if ((s = pvdid_get_binary_socket(fd)) != -1) {
		if (pvdid_get_rdnss(s, pvdId) == 0 && ReadMsg(s, &msg) == 0) {
			sprintf(Pattern, "PVDID_ATTRIBUTE %s RDNSS\n", pvdId);

			if (strncmp(msg, Pattern, strlen(Pattern)) == 0) {
				rc = pvdid_parse_rdnss(&msg[strlen(Pattern)], PtRdnss);
			}
			free(msg);
		}
		close(s);
	}
	return(rc);
}

int	pvdid_get_dnssl(int fd, char *pvdId)
{
	char	s[2048];

	sprintf(s, "PVDID_GET_ATTRIBUTE %s DNSSL\n", pvdId);

	return(SendExact(fd, s));
}

int	pvdid_get_dnssl_sync(int fd, char *pvdId, t_pvdid_dnssl *PtDnssl)
{
	int	s;
	int	rc = -1;
	char	*msg;
	char	Pattern[2048];

	if ((s = pvdid_get_binary_socket(fd)) != -1) {
		if (pvdid_get_dnssl(s, pvdId) == 0 && ReadMsg(s, &msg) == 0) {
			sprintf(Pattern, "PVDID_ATTRIBUTE %s DNSSL\n", pvdId);

			if (strncmp(msg, Pattern, strlen(Pattern)) == 0) {
				rc = pvdid_parse_dnssl(&msg[strlen(Pattern)], PtDnssl);
			}
			free(msg);
		}
		close(s);
	}
	return(rc);
}

/* ex: set ts=8 noexpandtab wrap: */
