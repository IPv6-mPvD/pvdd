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
 * libpvd.c : provides a set of functions to hide to native (aka C)
 * applications the internal communication with the pvd daemon
 * It also gives access to some pvd related kernel system calls (well,
 * not really system calls)
 *
 * 2 sets of functions :
 * + async functions
 * + sync functions. This family will use a per-call connection with the server
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <malloc.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "pvd-defs.h"
#include "pvd-utils.h"

#include "libpvd.h"

#undef	true
#undef	false
#define	true	(1 == 1)
#define	false	(1 == 0)

#define	DIM(t)		(sizeof(t) / sizeof(t[0]))
#define	EQSTR(a,b)	(strcmp((a), (b)) == 0)

#undef	NEW
#define	NEW(t)	((t *) malloc(sizeof(t)))

struct t_pvd_connection {
	int	fd;
	int	type;		/* xxx_CONNECTION above */
	int	NeedFlush;	/* do we need to release the SB field ? */
	int	MultiLines;	/* for REGULAR_CONNECTION/CONTROL_CONNECTION */
	int	ExpectedBytes;	/* for BINARY_CONNECTION */
	char	ReadBuffer[4096];
	int	InReadBuffer;	/* number of bytes already read in ReadBuffer */
	t_StringBuffer	SB;	/* full lines are accumulated here */
};

// NewConnection : allocate a connection structure
static	t_pvd_connection	*NewConnection(void)
{
	t_pvd_connection	*conn = NEW(t_pvd_connection);

	if (conn != NULL) {
		memset(conn, 0, sizeof(*conn));
		conn->fd = -1;
		conn->type = REGULAR_CONNECTION;	// default
		conn->NeedFlush = false;
		conn->MultiLines = false;
		conn->ReadBuffer[0] = '\0';
		conn->InReadBuffer = 0;
		SBInit(&conn->SB);
	}

	return(conn);
}

// DelConnection : release the connection structure
// The socket must have been closed by the caller
static	void	DelConnection(t_pvd_connection *conn)
{
	if (conn != NULL) {
		SBUninit(&conn->SB);
		free(conn);
	}
}

// Public accessors to the t_pvd_connection structure
int	pvd_connection_fd(t_pvd_connection *conn)
{
	return(conn != NULL ? conn->fd : -1);
}

int	pvd_connection_type(t_pvd_connection *conn)
{
	return(conn != NULL ? conn->type : INVALID_CONNECTION);
}

// ReadMsg : reads an incoming message on a binary socket. The first
// bytes of the message carry the total length to read
static	int	ReadMsg(int fd, char **String)
{
	int		len;
	char		msg[1024];
	t_StringBuffer	SB;
	int		n;

	if (fd == -1) {
		return(-1);
	}

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
	if (fd == -1) {
		return(-1);
	}
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

// pvd_connect : returns a general connection (socket) with the pvdid
// daemon
t_pvd_connection	*pvd_connect(int Port)
{
	int			s;
	struct sockaddr_in 	sa;
	t_pvd_connection	*conn = NULL;

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
		return(NULL);
	}

	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa.sin_port = htons(Port);

	if (connect(s, (struct sockaddr *) &sa, sizeof(sa)) == -1) {
		close(s);
		return(NULL);
	}

	if ((conn = NewConnection()) == NULL) {
		close(s);
	}
	else {
		conn->fd = s;
	}

	return(conn);
}

// reopen_connection : given an initial connection where all parameters have been
// supplied, just reuse the same connection parameters to create a new connection
// with the server
static	int	reopen_connection(int fd)
{
	int s;
	struct sockaddr sa;
	socklen_t salen;

	if (fd == -1) {
		return(-1);
	}

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

t_pvd_connection	*pvd_reconnect(t_pvd_connection *conn)
{
	int			s;
	t_pvd_connection	*newconn = NULL;

	if ((s = reopen_connection(pvd_connection_fd(conn))) != -1) {
		if ((newconn = NewConnection()) == NULL) {
			close(s);
			return(NULL);
		}
		newconn->fd = s;
	}
	return(newconn);
}

void	pvd_disconnect(t_pvd_connection *conn)
{
	if (conn != NULL) {
		close(conn->fd);
		DelConnection(conn);
	}
}

t_pvd_connection	*pvd_get_control_socket(t_pvd_connection *conn)
{
	t_pvd_connection	*newconn = NULL;

	if ((newconn = pvd_reconnect(conn)) != NULL) {
		if (SendExact(newconn->fd, "PVDID_CONNECTION_PROMOTE_CONTROL\n") == -1) {
			pvd_disconnect(newconn);
			return(NULL);
		}
		newconn->type = CONTROL_CONNECTION;
	}

	return(newconn);

}

t_pvd_connection	*pvd_get_binary_socket(t_pvd_connection *conn)
{
	t_pvd_connection	*newconn = NULL;

	if ((newconn = pvd_reconnect(conn)) != NULL) {
		if (SendExact(newconn->fd, "PVDID_CONNECTION_PROMOTE_BINARY\n") == -1) {
			pvd_disconnect(newconn);
			return(NULL);
		}
		newconn->type = BINARY_CONNECTION;
	}

	return(newconn);

}

// pvd_get_pvd_list : send a PVDID_GET_LIST message to the daemon
// It does not wait for a reply
int	pvd_get_pvd_list(t_pvd_connection *conn)
{
	return(SendExact(pvd_connection_fd(conn), "PVDID_GET_LIST\n"));
}

// pvd_parse_pvd_list : fill in the output array with PvD names
// They need to be freed using free() by the caller
int	pvd_parse_pvd_list(char *msg, t_pvd_list *pvdList)
{
	char	*pts = NULL;
	char	*pvdname = NULL;

	pvdList->npvd = 0;

	for (;(pvdname = strtok_r(msg, " ", &pts)) != NULL; msg = NULL) {
		// Ignore empty names (consecutive spaces)
		if (*pvdname == '\0') {
			continue;
		}
		if (pvdList->npvd < DIM(pvdList->pvdnames)) {
			pvdList->pvdnames[pvdList->npvd++] = strdup(pvdname);
		}
	}
	return(0);
}

// pvd_get_pvd_list_sync : send a PVDID_GET_LIST message to the daemon
// It waits for a reply
int	pvd_get_pvd_list_sync(t_pvd_connection *conn, t_pvd_list *pvdList)
{
	t_pvd_connection	*newconn = NULL;
	int			rc = -1;
	char			*msg;
	char			pvdString[2048];

	if ((newconn = pvd_get_binary_socket(conn)) != NULL) {
		if (pvd_get_pvd_list(newconn) == 0 && ReadMsg(newconn->fd, &msg) == 0) {
			if (sscanf(msg, "PVDID_LIST %[^\n]\n", pvdString) == 1) {
				// pvdString contains a list of space separated FQDN pvdIds
				pvd_parse_pvd_list(pvdString, pvdList);
				rc = 0;
			}
			free(msg);
		}
		pvd_disconnect(newconn);
	}
	return(rc);
}

int	pvd_get_attributes(t_pvd_connection *conn, char *pvdname)
{
	char	s[2048];

	snprintf(s, sizeof(s) - 1, "PVDID_GET_ATTRIBUTES %s\n", pvdname);
	s[sizeof(s) - 1] = '\0';

	return(SendExact(pvd_connection_fd(conn), s));
}

// pvd_get_attributes_sync : the attributes output parameter contain the
// JSON string of all attributes. It needs to be freed using free() by the
// caller
int	pvd_get_attributes_sync(
		t_pvd_connection *conn,
		char *pvdname,
		char **attributes)
{
	t_pvd_connection	*newconn = NULL;
	char			*msg;
	char			Pattern[2048];

	*attributes = NULL;

	if ((newconn = pvd_get_binary_socket(conn)) != NULL) {
		if (pvd_get_attributes(newconn, pvdname) == 0 &&
		    ReadMsg(newconn->fd, &msg) == 0) {
			sprintf(Pattern, "PVDID_ATTRIBUTES %s", pvdname);

			if (strncmp(msg, Pattern, strlen(Pattern)) == 0) {
				*attributes = strdup(StripSpaces(&msg[strlen(Pattern)]));
			}
			free(msg);
		}
		pvd_disconnect(newconn);
	}
	return(*attributes == NULL ? -1 : 0);
}

int	pvd_get_attribute(t_pvd_connection *conn, char *pvdname, char *attrName)
{
	char	s[2048];

	snprintf(s, sizeof(s) - 1, "PVDID_GET_ATTRIBUTE %s %s\n", pvdname, attrName);
	s[sizeof(s) - 1] = '\0';

	return(SendExact(pvd_connection_fd(conn), s));
}

int	pvd_get_attribute_sync(
		t_pvd_connection *conn,
		char *pvdname, 
		char *attrName, char **attrValue)
{
	t_pvd_connection	*newconn = NULL;
	char			*msg;
	char			Pattern[2048];

	*attrValue = NULL;

	if ((newconn = pvd_get_binary_socket(conn)) != NULL) {
		if (pvd_get_attribute(newconn, pvdname, attrName) == 0 &&
		    ReadMsg(newconn->fd, &msg) == 0) {
			sprintf(Pattern, "PVDID_ATTRIBUTE %s %s", pvdname, attrName);

			if (strncmp(msg, Pattern, strlen(Pattern)) == 0) {
				*attrValue = strdup(StripSpaces(&msg[strlen(Pattern)]));
			}

			free(msg);
		}
		pvd_disconnect(newconn);
	}

	return(*attrValue == NULL ? -1 : 0);
}

int	pvd_subscribe_notifications(t_pvd_connection *conn)
{
	return(SendExact(pvd_connection_fd(conn), "PVDID_SUBSCRIBE_NOTIFICATIONS\n"));
}

int	pvd_unsubscribe_notifications(t_pvd_connection *conn)
{
	return(SendExact(pvd_connection_fd(conn), "PVDID_UNSUBSCRIBE_NOTIFICATIONS\n"));
}

int	pvd_subscribe_pvd_notifications(t_pvd_connection *conn, char *pvdname)
{
	char	s[2048];

	snprintf(s, sizeof(s) - 1, "PVDID_SUBSCRIBE %s\n", pvdname);
	s[sizeof(s) - 1] = '\0';

	return(SendExact(pvd_connection_fd(conn), s));
}

int	pvd_unsubscribe_pvd_notifications(t_pvd_connection *conn, char *pvdname)
{
	char	s[2048];

	snprintf(s, sizeof(s) - 1, "PVDID_UNSUBSCRIBE %s\n", pvdname);
	s[sizeof(s) - 1] = '\0';

	return(SendExact(pvd_connection_fd(conn), s));
}

// ParseStringArray : given a string ["...", "...", ...], returns
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

// pvd_parse_rdnss : msq contains a JSON array of strings
// The string can either be alone on the line, either preceded
// by PVDID_ATTRIBUTE <pvdname> RDNSS. These strings are in6 addresses
int	pvd_parse_rdnss(char *msg, t_rdnss_list *PtRdnss)
{
	char	rdnss[2048];

	if (sscanf(msg, "PVDID_ATTRIBUTE %*[^ ] RDNSS %[^\n]", rdnss) == 1) {
		msg = rdnss;
	}

	PtRdnss->nrdnss = ParseStringArray(msg, PtRdnss->rdnss, DIM(PtRdnss->rdnss));

	return(0);
}

void	pvd_release_rdnss(t_rdnss_list *PtRdnss)
{
	int	i;

	for (i = 0; i < PtRdnss->nrdnss; i++) {
		free(PtRdnss->rdnss[i]);
	}
	PtRdnss->nrdnss = 0;
}

// pvd_parse_dnssl : msq contains a JSON array of strings
// The string can either be alone on the line, either preceded
// by PVDID_ATTRIBUTE <pvdname> DNSSL
int	pvd_parse_dnssl(char *msg, t_dnssl_list *PtDnssl)
{
	char	dnssl[2048];

	if (sscanf(msg, "PVDID_ATTRIBUTE %*[^ ] DNSSL %[^\n]", dnssl) == 1) {
		msg = dnssl;
	}

	PtDnssl->ndnssl = ParseStringArray(msg, PtDnssl->dnssl, DIM(PtDnssl->dnssl));

	return(0);
}

void	pvd_release_dnssl(t_dnssl_list *PtDnssl)
{
	int	i;

	for (i = 0; i < PtDnssl->ndnssl; i++) {
		free(PtDnssl->dnssl[i]);
	}
	PtDnssl->ndnssl = 0;
}

int	pvd_get_rdnss(t_pvd_connection *conn, char *pvdname)
{
	char	s[2048];

	sprintf(s, "PVDID_GET_ATTRIBUTE %s RDNSS\n", pvdname);

	return(SendExact(pvd_connection_fd(conn), s));
}

int	pvd_get_rdnss_sync(
		t_pvd_connection *conn,
		char *pvdname,
		t_rdnss_list *PtRdnss)
{
	t_pvd_connection	*newconn = NULL;
	int			rc = -1;
	char			*msg;
	char			Pattern[2048];

	if ((newconn = pvd_get_binary_socket(conn)) != NULL) {
		if (pvd_get_rdnss(newconn, pvdname) == 0 &&
		    ReadMsg(newconn->fd, &msg) == 0) {
			sprintf(Pattern, "PVDID_ATTRIBUTE %s RDNSS\n", pvdname);

			if (strncmp(msg, Pattern, strlen(Pattern)) == 0) {
				rc = pvd_parse_rdnss(&msg[strlen(Pattern)], PtRdnss);
			}
			free(msg);
		}
		pvd_disconnect(newconn);
	}
	return(rc);
}

int	pvd_get_dnssl(t_pvd_connection *conn, char *pvdname)
{
	char	s[2048];

	sprintf(s, "PVDID_GET_ATTRIBUTE %s DNSSL\n", pvdname);

	return(SendExact(pvd_connection_fd(conn), s));
}

int	pvd_get_dnssl_sync(
		t_pvd_connection *conn,
		char *pvdname,
		t_dnssl_list *PtDnssl)
{
	t_pvd_connection	*newconn = NULL;
	int			rc = -1;
	char			*msg;
	char			Pattern[2048];

	if ((newconn = pvd_get_binary_socket(conn)) != NULL) {
		if (pvd_get_dnssl(newconn, pvdname) == 0 &&
		    ReadMsg(newconn->fd, &msg) == 0) {
			sprintf(Pattern, "PVDID_ATTRIBUTE %s DNSSL\n", pvdname);

			if (strncmp(msg, Pattern, strlen(Pattern)) == 0) {
				rc = pvd_parse_dnssl(&msg[strlen(Pattern)], PtDnssl);
			}
			free(msg);
		}
		pvd_disconnect(newconn);
	}
	return(rc);
}

/*
 * UpdateReadBuffer : given the start of a new string in the read buffer of a
 * connection, move all the data starting at this byte to the beginning
 * of the read buffer. Basically, what this does is discarding data before
 * this new line
 */
static	void	UpdateReadBuffer(t_pvd_connection *conn, char *StartLine)
{
	char	*EndBuffer = &conn->ReadBuffer[conn->InReadBuffer];
	int	n;

	if ((n = EndBuffer - StartLine) > 0) {
		memmove(conn->ReadBuffer, StartLine, n);
		conn->InReadBuffer = n;
	}
	else {
		conn->InReadBuffer = 0;
	}
}

/*
 * StripTrailingNewLine : remove all trailing new lines from the SB
 * string buffer
 */
static	void	StripTrailingNewLine(t_StringBuffer *SB)
{
#if	0
	int	len = strlen(SB->String);

	while (len > 0 && SB->String[len - 1] == '\n') {
		SB->String[--len] = '\0';
	}
#else
#endif
}

/*
 * Helper function to make it easier for applications to read messages
 *
 * The philosophy is the following :
 * 1) application waits for data to be available for a connection (poll/select)
 * 2) it calls pvd_read_data() (and handles return codes properly)
 * 3) if data has been read, it calls pvd_get_message() to retrieve from
 * the received data one or more messages :
 * 	3.1) if the return code says PVD_MORE_DATA_AVAILABLE, a message
 * 	has been returned and the application MUST call pvd_get_message()
 * 	again (there is pending data still in the buffer, maybe a full
 * 	message, maybe a partial message)
 * 	3.2) if the return code says PVD_MESSAGE_READ, a message has
 * 	been returned and there is no pending data to be parsed
 * 	3.3) if the return code says PVD_NO_MESSAGE_READ, no message
 * 	could be returned (because more data needs to be received
 * 	from the daemon)
 * 4) loop on 1
 */
int	pvd_read_data(t_pvd_connection *conn)
{
	int	n, m;

	/*
	 * Some data is available : read as much as we can. Don't
	 * bother for now with the connection type. It will be handled
	 * when attempting to read a message from the accumulated
	 * buffer
	 */
	if ((m = sizeof(conn->ReadBuffer) - 1 - conn->InReadBuffer) <= 0) {
		// Buffer full : abnormal situation where no complete
		// message could be read
		return(PVD_READ_BUFFER_FULL);
	}
	if ((n = recv(conn->fd,
		      &conn->ReadBuffer[conn->InReadBuffer],
		      m,
		      MSG_DONTWAIT)) <= 0) {
		return(PVD_READ_ERROR);
	}

	conn->InReadBuffer += n;

	/*
	 * Make sure (for line oriented connections) that we have a
	 * terminating \0
	 */
	conn->ReadBuffer[conn->InReadBuffer] = '\0';

	return(PVD_READ_OK);
}

int	pvd_get_message(t_pvd_connection *conn, int *multiLines, char **msg)
{
	char	*pt = conn->ReadBuffer;
	char	*EOL;
	int	len;
	char	c;

	*multiLines = false;

	conn->ReadBuffer[conn->InReadBuffer] = '\0';

	/*
	 * If PVD_COMPLETE_MESSAGE had been returned on the previous call,
	 * the caller is supposed to have grabbed the SB.String buffer
	 * => we want to reset it now
	 */
	if (conn->NeedFlush) {
		SBUninit(&conn->SB);
		SBAddString(&conn->SB, "");
		conn->NeedFlush = false;
	}

	/*
	 * Binary messages consist of a length followed by the payload
	 */
	if (conn->type == BINARY_CONNECTION) {
		if ((len = conn->ExpectedBytes) == -1) {
			// Start of a new message : read the length first if enough
			// bytes in the buffer
			if (conn->InReadBuffer < sizeof(int)) {
				return(PVD_NO_MESSAGE_READ);
			}
			len = conn->ExpectedBytes = * ((int *) pt);
			pt += sizeof(int);
			conn->InReadBuffer -= sizeof(int);
		}
		// Move bytes in the string buffer
		if (len > conn->InReadBuffer) {
			len = conn->InReadBuffer;
		}
		/*
		 * SBAddString() expects a null terminated string : save
		 * first the byte that will be overriden with a \0
		 */
		c = pt[len];				// save
		pt[len] = '\0';				// override
		SBAddString(&conn->SB, "%s", pt);
		pt[len] = c;				// restore
		pt += len;
		conn->InReadBuffer -= len;
		if (conn->InReadBuffer > 0) {
			memmove(conn->ReadBuffer, pt, conn->InReadBuffer);
		}
		conn->ExpectedBytes -= len;
		if (conn->ExpectedBytes == 0) {
			// Complete message read. Strip out the trailing \n
			conn->ExpectedBytes = -1;
			conn->NeedFlush = true;
			StripTrailingNewLine(&conn->SB);
			*msg = conn->SB.String;
			if (conn->InReadBuffer > sizeof(int)) {
				return(PVD_MORE_DATA_AVAILABLE);
			}
			return(PVD_MESSAGE_READ);
		}
		return(PVD_NO_MESSAGE_READ);
	}

	/*
	 * Line oriented connection. We must handle lines one at a time
	 * We will copy lines from the read buffer to the string buffer
	 * As soon as a line has been copied, remaining data in the read
	 * buffer is moved down to the beginning of the read buffer and
	 * the buffer is processed again (if we are in multi lines mode)
	 * This is not efficient, but we dont care for now
	 */
Loop :
	pt = conn->ReadBuffer;
	pt[conn->InReadBuffer] = '\0';

	if ((EOL = strchr(pt, '\n')) == NULL) {
		// No full line in the read buffer
		return(PVD_NO_MESSAGE_READ);
	}

	*EOL++ = '\0';	// \n overriden

	if (EQSTR(pt, "PVDID_BEGIN_MULTILINE")) {
		conn->MultiLines = true;
		SBUninit(&conn->SB);
		SBAddString(&conn->SB, "");
		UpdateReadBuffer(conn, EOL);
		goto Loop;
	}

	if (conn->MultiLines) {
		// Check if the line is PVDID_END_MULTILINE
		if (EQSTR(pt, "PVDID_END_MULTILINE")) {
			conn->MultiLines = false;
			conn->NeedFlush = true;
			UpdateReadBuffer(conn, EOL);
			StripTrailingNewLine(&conn->SB);
			*msg = conn->SB.String;
			*multiLines = true;
			if (conn->InReadBuffer > 0) {
				return(PVD_MORE_DATA_AVAILABLE);
			}
			return(PVD_MESSAGE_READ);
		}
		// Add the string in the string buffer
		SBAddString(&conn->SB, "%s\n", pt);

		UpdateReadBuffer(conn, EOL);
		goto Loop;
	}

	// Add the string in the string buffer
	SBAddString(&conn->SB, "%s\n", pt);
	*msg = conn->SB.String;

	conn->NeedFlush = true;

	UpdateReadBuffer(conn, EOL);

	if (conn->InReadBuffer > 0) {
		return(PVD_MORE_DATA_AVAILABLE);
	}
	return(PVD_MESSAGE_READ);
}

/*
 * Helper functions directly talking to the kernel via sockets options
 * (the functions above were talking to the pvdid daemon)
 */
/*
 * Specialized function encapsulating the SO_BINDTOPVD socket option
 * sock_bind_to_pvd/sock_get_bound_pvd are used to bind a single pvd
 * to a socket, and are mostly useful as an example on how to use the
 * SO_BINDTOPVD option
 *
 * The bind_to_pvd is quite large, and we don't want to pass it to the
 * kernel : this is why the expected argument to SO_BINDTOPVD is the
 * address of a pointer to the effective structure (double reference)
 */
int	sock_bind_to_pvd(int s, char *pvdname)
{
	struct bind_to_pvd	btp, *pbtp = &btp;

	btp.scope = PVD_BIND_SCOPE_SOCKET;
	btp.npvd = 1;
	btp.pvdnames[0][PVDNAMSIZ - 1] = '\0';
	strncpy(btp.pvdnames[0], pvdname, PVDNAMSIZ- 1);

	return(setsockopt(s, SOL_SOCKET, SO_BINDTOPVD, &pbtp, sizeof(pbtp)));
}

int	sock_get_bound_pvd(int s, char *pvdname)
{
	struct bind_to_pvd	btp, *pbtp = &btp;
	socklen_t optlen = sizeof(pbtp);
	int rc;

	btp.scope = PVD_BIND_SCOPE_SOCKET;
	btp.npvd = 1;
	if ((rc = getsockopt(s, SOL_SOCKET, SO_BINDTOPVD, &pbtp, &optlen)) == 0) {
		if (btp.npvd == 1) {
			strncpy(pvdname, btp.pvdnames[0], PVDNAMSIZ - 1);
			pvdname[PVDNAMSIZ - 1] = '\0';
		}
		return(btp.npvd);
	}
	return(rc);
}

int	kernel_get_pvdlist(struct pvd_list *pvl)
{
	int		s;
	int		rc;
	socklen_t	optlen = sizeof(struct pvd_list *);

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		return(-1);
	}

	rc = getsockopt(s, SOL_SOCKET, SO_GETPVDLIST, &pvl, &optlen);

	close(s);

	return(rc);
}

int	kernel_get_pvd_attributes(char *pvdname, struct net_pvd_attribute *attr)
{
	int		s;
	int		rc;
	socklen_t	optlen = sizeof(struct pvd_attr);
	struct pvd_attr	pvdattr;

	memset(attr, 0, sizeof(*attr));

	pvdattr.pvdname = pvdname;
	pvdattr.pvdattr = attr;

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		return(-1);
	}

	rc = getsockopt(s, SOL_SOCKET, SO_GETPVDATTRIBUTES, &pvdattr, &optlen);

	close(s);

	return(rc);
}

int	kernel_create_pvd(char *pvdname)
{
	int			s;
	int			rc;
	struct create_pvd	cpvd;

	memset(&cpvd, 0, sizeof(cpvd));

	strncpy(cpvd.pvdname, pvdname, PVDNAMSIZ - 1);

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		return(-1);
	}

	rc = setsockopt(s, SOL_SOCKET, SO_CREATEPVD, &cpvd, sizeof(cpvd));

	close(s);

	return(rc);
}

int	kernel_update_pvd_attr(char *pvdname, char *attrName, char *attrValue)
{
	int			s;
	int			rc;
	struct create_pvd	cpvd;
	int			value;
	char			*pt;

	memset(&cpvd, 0, sizeof(cpvd));

	strncpy(cpvd.pvdname, pvdname, PVDNAMSIZ - 1);

	errno = 0;
	value = strtol(attrValue, &pt, 10);
	if (errno != 0 || pt == attrValue || *pt != '\0') {
		errno = -EINVAL;
		return(-1);
	}

	if (EQSTR(attrName, "hFlag")) {
		cpvd.flag = PVD_ATTR_HFLAG;
		cpvd.h_flag = value == 0 ? 0 : 1;
	} else
	if (EQSTR(attrName, "lFlag")) {
		cpvd.flag = PVD_ATTR_LFLAG;
		cpvd.l_flag = value == 0 ? 0 : 1;
	} else
	if (EQSTR(attrName, "sequenceNumber")) {
		cpvd.flag = PVD_ATTR_SEQNUMBER;
		cpvd.sequence_number = value % 15;
	} else
	if (EQSTR(attrName, "lifetime")) {
		cpvd.flag = PVD_ATTR_LIFETIME;
		cpvd.lifetime = value;
	}
	else {
		errno = -EINVAL;
		return(-1);
	}

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		return(-1);
	}

	rc = setsockopt(s, SOL_SOCKET, SO_CREATEPVD, &cpvd, sizeof(cpvd));

	close(s);

	return(rc);
}

/*
 * Tricky part to take rid of alignement issues
 * We define a ra_list structure with a well known
 * array size to be able to extrapolate the exact
 * alignment of any sized array
 *
 * For now, we suppose a max MTU of 1500 (to estimate the
 * worst case needed size of the buffer)
 */
struct ra_list16 {
	_RALIST_HEADER
	struct ra_buffer array[16];
	char	_buffer[0];
};

struct ra_list	*ralist_alloc(int max_ras)
{
	struct ra_list16	ref16;
	int	referenceArraySize = ref16._buffer - (char *) ref16.array;
	int	headerSize = (char *) ref16.array - (char *) &ref16;

	int	neededSize =
			headerSize +
			referenceArraySize * max_ras / 16 +
			1500 * max_ras;
	struct ra_list	*ral;

	if ((ral = (struct ra_list *) malloc(neededSize)) == NULL) {
		return(NULL);
	}

	memset(ral, 0, neededSize);

	ral->size = neededSize;
	ral->buffer_size = 1500 * max_ras;
	ral->max_ras = max_ras;
	ral->nra = 0;
	ral->buffer = (char *) ral + headerSize + referenceArraySize * max_ras / 16;

	return(ral);
}

void	ralist_release(struct ra_list *ral)
{
	free(ral);
}

int	kernel_get_ralist(struct ra_list *ral)
{
	int		s;
	int		rc;
	socklen_t	optlen = sizeof(struct ra_list *);

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		return(-1);
	}

	rc = getsockopt(s, SOL_SOCKET, SO_GETRALIST, &ral, &optlen);

	close(s);

	return(rc);
}

/* ex: set ts=8 noexpandtab wrap: */
