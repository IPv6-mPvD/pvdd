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
#include <malloc.h>
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

// pvdid_connect : returns a general connection (socket) with the pvdid
// daemon
t_pvd_connection	*pvdid_connect(int Port)
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

t_pvd_connection	*pvdid_reconnect(t_pvd_connection *conn)
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

void	pvdid_disconnect(t_pvd_connection *conn)
{
	if (conn != NULL) {
		close(conn->fd);
		DelConnection(conn);
	}
}

t_pvd_connection	*pvdid_get_control_socket(t_pvd_connection *conn)
{
	t_pvd_connection	*newconn = NULL;

	if ((newconn = pvdid_reconnect(conn)) != NULL) {
		if (SendExact(newconn->fd, "PVDID_CONNECTION_PROMOTE_CONTROL\n") == -1) {
			pvdid_disconnect(newconn);
			return(NULL);
		}
		newconn->type = CONTROL_CONNECTION;
	}

	return(newconn);

}

t_pvd_connection	*pvdid_get_binary_socket(t_pvd_connection *conn)
{
	t_pvd_connection	*newconn = NULL;

	if ((newconn = pvdid_reconnect(conn)) != NULL) {
		if (SendExact(newconn->fd, "PVDID_CONNECTION_PROMOTE_BINARY\n") == -1) {
			pvdid_disconnect(newconn);
			return(NULL);
		}
		newconn->type = BINARY_CONNECTION;
	}

	return(newconn);

}

// pvdid_get_pvdid_list : send a PVDID_GET_LIST message to the daemon
// It does not wait for a reply
int	pvdid_get_pvdid_list(t_pvd_connection *conn)
{
	return(SendExact(pvd_connection_fd(conn), "PVDID_GET_LIST\n"));
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

// pvdid_get_pvdid_list : send a PVDID_GET_LIST message to the daemon
// It waits for a reply
int	pvdid_get_pvdid_list_sync(t_pvd_connection *conn, t_pvdid_list *pvdIdList)
{
	t_pvd_connection	*newconn = NULL;
	int			rc = -1;
	char			*msg;
	char			pvdString[2048];

	if ((newconn = pvdid_get_binary_socket(conn)) != NULL) {
		if (pvdid_get_pvdid_list(newconn) == 0 && ReadMsg(newconn->fd, &msg) == 0) {
			if (sscanf(msg, "PVDID_LIST %[^\n]\n", pvdString) == 1) {
				// pvdString contains a list of space separated FQDN pvdIds
				pvdid_parse_pvdid_list(pvdString, pvdIdList);
				rc = 0;
			}
			free(msg);
		}
		pvdid_disconnect(newconn);
	}
	return(rc);
}

int	pvdid_get_attributes(t_pvd_connection *conn, char *pvdId)
{
	char	s[2048];

	snprintf(s, sizeof(s) - 1, "PVDID_GET_ATTRIBUTES %s\n", pvdId);
	s[sizeof(s) - 1] = '\0';

	return(SendExact(pvd_connection_fd(conn), s));
}

// pvdid_get_attributes_sync : the attributes output parameter contain the
// JSON string of all attributes. It needs to be freed using free() by the
// caller
int	pvdid_get_attributes_sync(
		t_pvd_connection *conn,
		char *pvdId,
		char **attributes)
{
	t_pvd_connection	*newconn = NULL;
	char			*msg;
	char			Pattern[2048];

	*attributes = NULL;

	if ((newconn = pvdid_get_binary_socket(conn)) != NULL) {
		if (pvdid_get_attributes(newconn, pvdId) == 0 &&
		    ReadMsg(newconn->fd, &msg) == 0) {
			sprintf(Pattern, "PVDID_ATTRIBUTES %s", pvdId);

			if (strncmp(msg, Pattern, strlen(Pattern)) == 0) {
				*attributes = strdup(StripSpaces(&msg[strlen(Pattern)]));
			}
			free(msg);
		}
		pvdid_disconnect(newconn);
	}
	return(*attributes == NULL ? -1 : 0);
}

int	pvdid_get_attribute(t_pvd_connection *conn, char *pvdId, char *attrName)
{
	char	s[2048];

	snprintf(s, sizeof(s) - 1, "PVDID_GET_ATTRIBUTE %s %s\n", pvdId, attrName);
	s[sizeof(s) - 1] = '\0';

	return(SendExact(pvd_connection_fd(conn), s));
}

int	pvdid_get_attribute_sync(
		t_pvd_connection *conn,
		char *pvdId, 
		char *attrName, char **attrValue)
{
	t_pvd_connection	*newconn = NULL;
	char			*msg;
	char			Pattern[2048];

	*attrValue = NULL;

	if ((newconn = pvdid_get_binary_socket(conn)) != NULL) {
		if (pvdid_get_attribute(newconn, pvdId, attrName) == 0 &&
		    ReadMsg(newconn->fd, &msg) == 0) {
			sprintf(Pattern, "PVDID_ATTRIBUTE %s %s", pvdId, attrName);

			if (strncmp(msg, Pattern, strlen(Pattern)) == 0) {
				*attrValue = strdup(StripSpaces(&msg[strlen(Pattern)]));
			}

			free(msg);
		}
		pvdid_disconnect(newconn);
	}

	return(*attrValue == NULL ? -1 : 0);
}

int	pvdid_subscribe_notifications(t_pvd_connection *conn)
{
	return(SendExact(pvd_connection_fd(conn), "PVDID_SUBSCRIBE_NOTIFICATIONS\n"));
}

int	pvdid_unsubscribe_notifications(t_pvd_connection *conn)
{
	return(SendExact(pvd_connection_fd(conn), "PVDID_UNSUBSCRIBE_NOTIFICATIONS\n"));
}

int	pvdid_subscribe_pvdid_notifications(t_pvd_connection *conn, char *pvdId)
{
	char	s[2048];

	snprintf(s, sizeof(s) - 1, "PVDID_SUBSCRIBE %s\n", pvdId);
	s[sizeof(s) - 1] = '\0';

	return(SendExact(pvd_connection_fd(conn), s));
}

int	pvdid_unsubscribe_pvdid_notifications(t_pvd_connection *conn, char *pvdId)
{
	char	s[2048];

	snprintf(s, sizeof(s) - 1, "PVDID_UNSUBSCRIBE %s\n", pvdId);
	s[sizeof(s) - 1] = '\0';

	return(SendExact(pvd_connection_fd(conn), s));
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

int	pvdid_get_rdnss(t_pvd_connection *conn, char *pvdId)
{
	char	s[2048];

	sprintf(s, "PVDID_GET_ATTRIBUTE %s RDNSS\n", pvdId);

	return(SendExact(pvd_connection_fd(conn), s));
}

int	pvdid_get_rdnss_sync(
		t_pvd_connection *conn,
		char *pvdId,
		t_pvdid_rdnss *PtRdnss)
{
	t_pvd_connection	*newconn = NULL;
	int			rc = -1;
	char			*msg;
	char			Pattern[2048];

	if ((newconn = pvdid_get_binary_socket(conn)) != NULL) {
		if (pvdid_get_rdnss(newconn, pvdId) == 0 &&
		    ReadMsg(newconn->fd, &msg) == 0) {
			sprintf(Pattern, "PVDID_ATTRIBUTE %s RDNSS\n", pvdId);

			if (strncmp(msg, Pattern, strlen(Pattern)) == 0) {
				rc = pvdid_parse_rdnss(&msg[strlen(Pattern)], PtRdnss);
			}
			free(msg);
		}
		pvdid_disconnect(newconn);
	}
	return(rc);
}

int	pvdid_get_dnssl(t_pvd_connection *conn, char *pvdId)
{
	char	s[2048];

	sprintf(s, "PVDID_GET_ATTRIBUTE %s DNSSL\n", pvdId);

	return(SendExact(pvd_connection_fd(conn), s));
}

int	pvdid_get_dnssl_sync(
		t_pvd_connection *conn,
		char *pvdId,
		t_pvdid_dnssl *PtDnssl)
{
	t_pvd_connection	*newconn = NULL;
	int			rc = -1;
	char			*msg;
	char			Pattern[2048];

	if ((newconn = pvdid_get_binary_socket(conn)) != NULL) {
		if (pvdid_get_dnssl(newconn, pvdId) == 0 &&
		    ReadMsg(newconn->fd, &msg) == 0) {
			sprintf(Pattern, "PVDID_ATTRIBUTE %s DNSSL\n", pvdId);

			if (strncmp(msg, Pattern, strlen(Pattern)) == 0) {
				rc = pvdid_parse_dnssl(&msg[strlen(Pattern)], PtDnssl);
			}
			free(msg);
		}
		pvdid_disconnect(newconn);
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
 * 2) it calls pvdid_read_data() (and handles return codes properly)
 * 3) if data has been read, it calls pvdid_get_message() to retrieve from
 * the received data one or more messages :
 * 	3.1) if the return code says PVD_MORE_DATA_AVAILABLE, a message
 * 	has been returned and the application MUST call pvdid_get_message()
 * 	again (there is pending data still in the buffer, maybe a full
 * 	message, maybe a partial message)
 * 	3.2) if the return code says PVD_MESSAGE_READ, a message has
 * 	been returned and there is no pending data to be parsed
 * 	3.3) if the return code says PVD_NO_MESSAGE_READ, no message
 * 	could be returned (because more data needs to be received
 * 	from the daemon)
 * 4) loop on 1
 */
int	pvdid_read_data(t_pvd_connection *conn)
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

int	pvdid_get_message(t_pvd_connection *conn, int *multiLines, char **msg)
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

	btp.npvd = 1;
	btp.pvdnames[0][PVDIDNAMESIZ - 1] = '\0';
	strncpy(btp.pvdnames[0], pvdname, PVDIDNAMESIZ- 1);

	return(setsockopt(s, SOL_SOCKET, SO_BINDTOPVD, &pbtp, sizeof(pbtp)));
}

int	sock_get_bound_pvd(int s, char *pvdname)
{
	struct bind_to_pvd	btp, *pbtp = &btp;
	socklen_t optlen = sizeof(pbtp);
	int rc;

	btp.npvd = 1;
	if ((rc = getsockopt(s, SOL_SOCKET, SO_BINDTOPVD, &pbtp, &optlen)) == 0) {
		if (btp.npvd == 1) {
			strncpy(pvdname, btp.pvdnames[0], PVDIDNAMESIZ - 1);
			pvdname[PVDIDNAMESIZ - 1] = '\0';
		}
		return(btp.npvd);
	}
	return(rc);
}

int	pvd_get_list(struct pvd_list *pvl)
{
	int		s;
	int		rc;
	socklen_t	optlen = sizeof(struct pvd_list *);

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		return(-1);
	}

	rc = getsockopt(s, SOL_SOCKET, SO_GETPVDINFO, &pvl, &optlen);

	close(s);

	return(rc);
}

/* ex: set ts=8 noexpandtab wrap: */
