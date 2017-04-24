/*
 * INSERT PROPER HEADER HERE
 */

/*
 * pvdid-daemon.c : in a pvdid aware network environment, this daemon is the in-core
 * repository of pvdid related information. This is where applications (ie, other
 * processes) fetch pvdid related information, such as per pvd-DNS settings, extra
 * information (JSON), etc)
 * The daemon comes with a C-companion library to be used by clients. This library
 * is very basic on purpose and is mainly used to encapsulate requests/replies/
 * notifications in wire message format. It does not provide any sort of background
 * behaviour (this would require mult-threading and we don't want all applications
 * to link against the pthread library). Its API is intended to be easily integrated
 * into event based main loops (poll/select, uevent, etc.)
 *
 * The daemon itself can receive additional information by clients and will aggregate
 * this information in its central repository
 *
 * In addition, a persistent state of the repository can be flushed in the file
 * system
 *
 * The daemon creates one listening socket.
 * Clients connecting wishing to update some daemon's content need to promote their
 * socket. This is done by sending a special promotion message
 * Sockets types are :
 * GENERAL (initial state, before promotion)
 * CONTROL
 *
 * We may want to restrict, via credentials, access to the control socket
 *
 * The daemon will also collect information from the kernel via the netlink raw
 * interface
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <malloc.h>
#include <libgen.h>	// basename()
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "pvdid-defs.h"
#include "pvdid-daemon.h"
#include "pvdid-utils.h"
#include "pvdid-netlink.h"

/* constants and macros ------------------------------------------ */

#define	NEW(t)	((t *) malloc(sizeof(t)))

// Clients sockets types. General sockets can be promoted to control sockets
#define	SOCKET_GENERAL		1
#define	SOCKET_CONTROL		2

// Max numbers of items. TODO : replace these hard coded limits by dynamic
// implementation (but, doing this, make sure we avoid DOS)
#define	MAXCLIENTS	1024
#define	MAXATTRIBUTES	128

// Clients can request to be notified on some changes. No notifications by
// default
#define	SUBSCRIPTION_LIST	0x01
#define	SUBSCRIPTION_NEW_PVDID	0x02
#define	SUBSCRIPTION_DEL_PVDID	0x04

/* types definitions --------------------------------------------- */
typedef	struct t_PvdIdNameList
{
	char	*pvdId;	// strduped
	struct t_PvdIdNameList	*next;
}	t_PvdIdNameList;

typedef	struct t_PvdIdClient
{
	int		s;
	int		type;
	t_PvdIdNameList	*Subscription;
	int		SubscriptionMask;
	char		*pvdIdTransaction;	// NULL is no transaction
	int		multiLines;
	t_StringBuffer	SB;
}	t_PvdIdClient;

typedef	struct {
	char	*Key;	// strduped
	char	*Value;	// strduped
}	t_PvdAttribute;

typedef	struct t_PvdId {
	char	*pvdId;	// strduped
	int	pvdIdHandle;
	int	dirty;
	t_PvdAttribute Attributes[MAXATTRIBUTES];
	struct t_PvdId	*next;
}	t_PvdId;

/* variables declarations ---------------------------------------- */
static	int		lNClients = 0;
static	t_PvdIdClient	lTabClients[MAXCLIENTS];

static	t_PvdId	*lFirstPvdId = NULL;

static	char	*lMyName = "";

/* functions definitions ----------------------------------------- */
static	int	RemoveSubscription(int ix, char *pvdId);

static	int	usage(char *s)
{
	FILE	*fo = s == NULL ? stdout : stderr;

	if (s != NULL) {
		fprintf(fo, "%s : %s\n", lMyName, s);
	}
	fprintf(fo, "%s [-h|--help] <option>*\n", lMyName);
	fprintf(fo, "where option :\n");
	fprintf(fo, "\t-v|--verbose\n");
	fprintf(fo,
		"\t-p|--port <#> : port number for clients requests (default %d)\n",
		DEFAULT_PVDID_PORT);
	fprintf(fo,
		"\t-d|--dir <path> : directory in which information is stored (none by default)\n");
	fprintf(fo,
		"\n"
		"Clients using the companion library can set the PVDID_PORT environment\n"
		"variable to specify another port than the default one\n");

	return(s == NULL ? 0 : 1);
}

// CreateServerSocket : create a socket for use by the clients
static	int	CreateServerSocket(int Port)
{
	int s;
	int one = 1;
	struct sockaddr_in sa;

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		return(-1);
	}

	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == -1) {
		DLOG("setsockopt reuseaddr : %s\n", strerror(errno));
	}

	memset((char *) &sa, 0, sizeof(sa));

	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa.sin_port = htons(Port);

	if (bind(s, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
		close(s);
		return(-1);
	}

	if (listen(s, 10) == -1) {
		close(s);
		return(-1);
	}
	return(s);
}

// HandleConnection : a client is connecting. Accept the connection and register
// the new socket
static	void	HandleConnection(int serverSock)
{
	int s;
	struct sockaddr_in sa;

	socklen_t salen = sizeof(sa);

	if ((s = accept(serverSock, (struct sockaddr *) &sa, &salen)) == -1) {
		return;
	}

	if (lNClients < DIM(lTabClients)) {
		t_PvdIdClient	*PtClient = &lTabClients[lNClients++];

		PtClient->s = s;
		PtClient->type = SOCKET_GENERAL;
		PtClient->Subscription = NULL;
		PtClient->SubscriptionMask = 0;
		PtClient->pvdIdTransaction = NULL;
		PtClient->multiLines = 0;
		SBInit(&PtClient->SB);
		DLOG("client connection accepted on socket %d\n", s);
	}
	else {
		close(s);	// this will trigger an error on the client's side
	}
}

// GetPvdId : given a pvdId, return the address of the pvdId structure
static	t_PvdId	*GetPvdId(char *pvdId)
{
	t_PvdId	*PtPvdId;

	if (pvdId == NULL) {
		return(NULL);
	}

	for (PtPvdId = lFirstPvdId; PtPvdId != NULL; PtPvdId = PtPvdId->next) {
		if (EQSTR(PtPvdId->pvdId, pvdId)) {
			return(PtPvdId);
		}
	}

	DLOG("unknown pvdId (%s)\n", pvdId);

	return(NULL);
}

// ReleaseClient : unregister a client (typically when the socket with the client
// is reporting an I/O error)
// ix is supposed to be within range
static	void	ReleaseClient(int ix)
{
	t_PvdIdClient	*pt = &lTabClients[ix];

	DLOG("releasing client %d\n", ix);

	if (pt->pvdIdTransaction != NULL) {
		free(pt->pvdIdTransaction);
		pt->pvdIdTransaction = NULL;
	}
	RemoveSubscription(ix, NULL);
	if (pt->s != -1) {
		close(pt->s);
	}
	pt->s = -1;
}

// AddSubscription : a client has requested to be notified for changes on a
// given pvdid
static	int	AddSubscription(int ix, char *pvdId)
{
	t_PvdIdNameList	*pt = lTabClients[ix].Subscription;

	// Verifiy that the client has not already subscribed to this pvdId
	while (pt != NULL) {
		if (EQSTR(pt->pvdId, pvdId)) {
			return(0);
		}
		pt = pt->next;
	}

	// Add it at the head
	if ((pt = (t_PvdIdNameList *) malloc(sizeof(t_PvdIdNameList))) == NULL) {
		DLOG("AddSubscription : memory overflow\n");
		return(-1);
	}
	if ((pt->pvdId = strdup(pvdId)) == NULL) {
		free(pt);
		DLOG("AddSubscription : memory overflow\n");
		return(-1);
	}
	pt->next = lTabClients[ix].Subscription;
	lTabClients[ix].Subscription = pt;

	return(0);
}

// RemoveSubscription : remove a given pvdId from the list of subscribed pvdId
// for a client. Special case : if the given pvdId is NULL, the whole subscription
// list is released
static	int	RemoveSubscription(int ix, char *pvdId)
{
	t_PvdIdNameList	*pt = lTabClients[ix].Subscription;
	t_PvdIdNameList	*ptNext = NULL;
	t_PvdIdNameList	*ptPrev = NULL;

	// Special case for pvdId == NULL => we will completely
	// release the list
	if (pvdId == NULL) {
		while (pt != NULL) {
			ptNext = pt->next;
			free(pt->pvdId);
			free(pt);
			pt = ptNext;
		}
		return(0);
	}	

	while (pt != NULL) {
		ptNext = pt->next;

		if (EQSTR(pt->pvdId, pvdId)) {
			free(pt->pvdId);
			free(pt);
			if (ptPrev == NULL) {
				lTabClients[ix].Subscription = ptNext;
			}
			else {
				ptPrev->next = ptNext;
			}
			return(0);
		}
		ptPrev = pt;
		pt = ptNext;
	}
	return(0);
}

// WriteString : send a string over a socket (any file descriptor in fact).
// Return true if the whole string could be written, false otherwise
static	int	WriteString(int s, char *str)
{
	int	l = strlen(str);
	return(write(s, str, l) == l);
}

// SendPvdIdList : send the current list of pvdId to a client that
// has requested it
static	int	SendPvdIdList(int s)
{
	char	msg[2048];
	t_PvdId	*PtPvdId;

	// FIXME : check for overflow
	sprintf(msg, "PVDID_LIST");
	for (PtPvdId = lFirstPvdId; PtPvdId != NULL; PtPvdId = PtPvdId->next) {
		sprintf(msg, "%s %s", msg, PtPvdId->pvdId);
	}
	strcat(msg, "\n");
	if (! WriteString(s, msg)) {
		return(-1);
	}
	return(0);
}

// NotifyPvdIdState : send a notification message for this pvdId (NEW/DEL) to
// clients having subscribed to such events
static	void	NotifyPvdIdState(char *pvdId, int Mask)
{
	int		i;
	char		msg[2048];
	t_PvdIdClient	*pt;

	msg[sizeof(msg) - 1] = '\0';

	snprintf(msg, sizeof(msg) - 1,
		"%s %s\n",
		Mask == SUBSCRIPTION_NEW_PVDID ? "PVDID_NEW_PVDID" : "PVDID_DEL_PVDID",
		pvdId);

	for (i = 0, pt = lTabClients; i < lNClients; i++, pt++) {
		if (pt->s == -1) {
			continue;
		}
		if ((pt->SubscriptionMask & Mask) != 0) {
			if (! WriteString(pt->s, msg)) {
				ReleaseClient(i);
			}
		}
	}
}

// NotifyPvdIdList : send the full pvdId list to clients that have subscribed to
// this notification
static	void	NotifyPvdIdList(void)
{
	t_PvdId		*PtPvdId;
	char		msg[2048];
	t_PvdIdClient	*pt;
	int		i;

	msg[sizeof(msg) - 1] = '\0';

	// FIXME : check for overflow
	sprintf(msg, "PVDID_LIST");
	for (PtPvdId = lFirstPvdId; PtPvdId != NULL; PtPvdId = PtPvdId->next) {
		sprintf(msg, "%s %s", msg, PtPvdId->pvdId);
	}
	strcat(msg, "\n");

	for (i = 0, pt = lTabClients; i < lNClients; i++, pt++) {
		if (pt->s == -1 || pt->type == SOCKET_CONTROL) {
			continue;
		}
		if ((pt->SubscriptionMask & SUBSCRIPTION_LIST) != 0) {
			if (! WriteString(pt->s, msg)) {
				ReleaseClient(i);
			}
		}
	}
}

// RegisterPvdId : register a new pvdid. It should normally come from
// the kernel notifications, but, for debug purpose, might also be
// provided by clients on control sockets
// We may want to notify all clients (except control ones) that a
// new pvdid has appeared
static	t_PvdId	*RegisterPvdId(int pvdIdHandle, char *pvdId)
{
	t_PvdId	*PtPvdId;

	for (PtPvdId = lFirstPvdId; PtPvdId != NULL; PtPvdId = PtPvdId->next) {
		if (EQSTR(PtPvdId->pvdId, pvdId)) {
			if (pvdIdHandle != -1) {
				PtPvdId->pvdIdHandle = pvdIdHandle;
			}
			PtPvdId->dirty = true;
			return(PtPvdId);
		}
	}

	if ((PtPvdId = NEW(t_PvdId)) == NULL) {
		DLOG("allocating pvdid : memory overflow\n");
		return(NULL);
	}
	PtPvdId->pvdIdHandle = pvdIdHandle == -1 ? 0 : pvdIdHandle;
	PtPvdId->pvdId = strdup(pvdId);	// TODO : check overflow
	PtPvdId->dirty = false;
	memset(PtPvdId->Attributes, 0, sizeof(PtPvdId->Attributes));
	PvdIdSetAttr(PtPvdId, "sequenceNumber", "0");
	PvdIdSetAttr(PtPvdId, "hFlag", "0");
	PvdIdSetAttr(PtPvdId, "lFlag", "0");

	// Link it at the head of the list
	PtPvdId->next = lFirstPvdId;
	lFirstPvdId = PtPvdId;

	DLOG("pvdid %s/%d registered\n", pvdId, pvdIdHandle);

	NotifyPvdIdState(pvdId, SUBSCRIPTION_NEW_PVDID);
	NotifyPvdIdList();

	return(PtPvdId);
}

// UnregisterPvdId : unregister a pvdid. We won't touch the subscription lists
// (because the pvdId might reappear). We may want to send notification to
// all clients (except control clients)
static	int	UnregisterPvdId(char *pvdId)
{
	int	i;
	t_PvdId	*PtPvdId, *PtPvdIdPrev = NULL;

	for (PtPvdId = lFirstPvdId; PtPvdId != NULL; PtPvdId = PtPvdId->next) {
		if (EQSTR(PtPvdId->pvdId, pvdId)) {
			// Unlink the pvdId and frees all of its fields
			if (PtPvdIdPrev == NULL) {
				lFirstPvdId = PtPvdId->next;
			}
			else {
				PtPvdIdPrev->next = PtPvdId->next;
			}
			for (i = 0; i < DIM(PtPvdId->Attributes); i++) {
				if (PtPvdId->Attributes[i].Key != NULL) {
					free(PtPvdId->Attributes[i].Key);
				}
				if (PtPvdId->Attributes[i].Value != NULL) {
					free(PtPvdId->Attributes[i].Value);
				}
			}
			free(PtPvdId->pvdId);
			free(PtPvdId);
			NotifyPvdIdState(pvdId, SUBSCRIPTION_DEL_PVDID);
			NotifyPvdIdList();
			return(0);
		}
		PtPvdIdPrev = PtPvdId;
	}
	return(0);
}

// UpdateAttribute : update (ie, replace)/create a given attribute for
// a given pvdIdHandle
static	int	UpdateAttribute(t_PvdId *PtPvdId, char *Key, char *Value)
{
	int	i;
	char	*key_;
	char	*value_;
	int	firstAvailable = -1;

	if (PtPvdId == NULL) {
		DLOG("UpdateAttribute : unknown pvdId\n");
		return(0);
	}

	DLOG("UpdateAttribute : pvdId = %s, Key = %s, Value = %s\n",
		PtPvdId->pvdId, Key, Value);

	for (i = 0; i < MAXATTRIBUTES; i++) {
		char	*attrKey = PtPvdId->Attributes[i].Key;
		char	*attrVal = PtPvdId->Attributes[i].Value;

		if (attrKey == NULL && firstAvailable == -1) {
			firstAvailable = i;
		}

		if (attrKey != NULL && EQSTR(attrKey, Key)) {
			if (attrVal != NULL && EQSTR(attrVal, Value)) {
				// Same key/value pair => no change
				return(0);
			}

			if ((value_ = strdup(Value)) != NULL) {
				if (attrVal != NULL) {
					free(attrVal);
				}
				PtPvdId->Attributes[i].Value = value_;
				PtPvdId->dirty = true;
				return(0);
			}
			DLOG("memory overflow allocating attribute %s/%s for %s\n",
				Key, Value, PtPvdId->pvdId);
			return(0);
		}
	}

	if (firstAvailable != -1) {
		i = firstAvailable;
		if ((key_ = strdup(Key)) != NULL) {
			if ((value_ = strdup(Value)) != NULL) {
				PtPvdId->Attributes[i].Key = key_;
				PtPvdId->Attributes[i].Value = value_;
				PtPvdId->dirty = true;
				return(0);
			}
			free(key_);
			DLOG("memory overflow allocating attribute %s/%s for %s\n",
			     Key, Value,
			     PtPvdId->pvdId);
		}
	}
	if (i > MAXATTRIBUTES) {
		DLOG("too many attributes defined for %s\n", PtPvdId->pvdId);
	}
	return(0);
}

// PvdIdAttributes2Json : converts all key/value entries for a given pvdIdHandle
// to a JSON object. The semantic of some fields is well known (hummm, maybe not
// so true)
static	char	*PvdIdAttributes2Json(t_PvdId *PtPvdId)
{
	int		i;
	t_StringBuffer	SB;
	int		pvdIdHandle = PtPvdId->pvdIdHandle;
	char		*pvdId = PtPvdId->pvdId;
	t_PvdAttribute	*Attributes = PtPvdId->Attributes;

	SBInit(&SB);

	// We always have 2 default fields
	SBAddString(&SB, "{\n");
	SBAddString(&SB, "\t\"pvdId\" : \"%s\",\n", JsonString(pvdId));
	SBAddString(&SB, "\t\"pvdIdHandle\" : %d", pvdIdHandle);

	for (i = 0; i < MAXATTRIBUTES; i++) {
		if (Attributes[i].Key != NULL) {
			SBAddString(
				&SB,
				",\n\t\"%s\" : %s",
				JsonString(Attributes[i].Key),
				Attributes[i].Value);
		}
	}

	SBAddString(&SB, "\n}\n");

	DLOG("PvdIdAttributes2Json(%d) : %s\n", pvdIdHandle, SB.String);

	return(SB.String);
}

// SendMultiLines : send a multi-line string to a client. Multi-line messages are :
// PVDID_MULTILINE <number of lines>
// ...
// ...
static	int	SendMultiLines(int s, char *prefix, ...)
{
	int	n = 1;	// 1 because we have at least the 'prefix' string on 1st line
	char	Line[256];
	va_list ap;
	char	*pt;

	va_start(ap, prefix);

	while ((pt = va_arg(ap, char *)) != NULL) {
		while (*pt != '\0') {
			if (*pt == '\n') {
				n++;
			}
			pt++;
		}
	}
	va_end(ap);

	sprintf(Line, "PVDID_MULTILINE %d\n", n);

	if (! WriteString(s, Line)) {
		return(-1);
	}

	if (! WriteString(s, prefix)) {
		return(-1);
	}

	va_start(ap, prefix);

	while ((pt = va_arg(ap, char *)) != NULL) {
		if (! WriteString(s, pt)) {
			return(-1);
		}
	}
	va_end(ap);

	return(0);
}

// NotifyPvdIdAttributes : when one or more attributes for a given pvdIdHandle has/have
// changed, we must notify all clients interested in this pvdIdHandle of the change(s)
// For now, we send all attributes (JSON format) at once
static	int	NotifyPvdIdAttributes(t_PvdId *PtPvdId)
{
	int	i;
	char	*pvdId = PtPvdId->pvdId;
	int	FlagInterested = false;
	char	Prefix[1024];
	char	*JsonString;

	for (i = 0; i < lNClients; i++) {
		t_PvdIdNameList	*pt = lTabClients[i].Subscription;

		if (lTabClients[i].s == -1) {
			continue;
		}
		while (pt != NULL) {
			if (EQSTR(pt->pvdId, pvdId)) {
				FlagInterested = true;
				break;
			}
			pt = pt->next;
		}
		if (FlagInterested) {
			break;
		}
	}

	if (! FlagInterested) {
		return(0);
	}

	if ((JsonString = PvdIdAttributes2Json(PtPvdId)) == NULL) {
		// Don't fail here (this is not the caller's fault)
		return(0);
	}

	sprintf(Prefix, "PVDID_ATTRIBUTES %s\n", pvdId);

	for (i = 0; i < lNClients; i++) {
		int		s = lTabClients[i].s;
		t_PvdIdNameList	*pt = lTabClients[i].Subscription;

		if (s == -1 || lTabClients[i].type == SOCKET_CONTROL) {
			continue;
		}

		while (pt != NULL) {
			if (EQSTR(pt->pvdId, pvdId)) {
				if (SendMultiLines(s, Prefix, JsonString, NULL) == -1) {
					ReleaseClient(i);
				}
				break;
			}
			pt = pt->next;
		}
	}
	free(JsonString);

	return(0);
}

// PvdIdBeginTransaction : called internally before updating a set of attributes
// Must be 'closed' by a call to PvdIdEndTransaction. The given pvdId will be created
// if needed
t_PvdId	*PvdIdBeginTransaction(char *pvdId)
{
	t_PvdId	*PtPvdId;

	if ((PtPvdId = RegisterPvdId(-1, pvdId)) != NULL) {
		PtPvdId->dirty = false;
	}
	return(PtPvdId);
}

// PvdIdSetAttr : update an attribute within a given pvdId
int	PvdIdSetAttr(t_PvdId *PtPvdId, char *Key, char *Value)
{
	return(UpdateAttribute(PtPvdId, Key, Value));
}

// PvdIdEndTransaction : we must notify any changes that might have happen
// during the transaction
void	PvdIdEndTransaction(t_PvdId *PtPvdId)
{
	if (PtPvdId->dirty) {
		NotifyPvdIdAttributes(PtPvdId);
	}
}

// SendAllAttributes : send the attributes for a given pvdIdHandle to a given client
static	int	SendAllAttributes(int s, char *pvdId)
{
	int	rc;
	char	Prefix[1024];
	char	*JsonString;
	t_PvdId	*PtPvdId;

	DLOG("send all attributes for pvdid %s on socket %d\n", pvdId, s);

	if ((PtPvdId = GetPvdId(pvdId)) == NULL) {
		return(0);
	}

	if ((JsonString = PvdIdAttributes2Json(PtPvdId)) == NULL) {
		return(0);
	}

	sprintf(Prefix, "PVDID_ATTRIBUTES %s\n", pvdId);

	rc = SendMultiLines(s, Prefix, JsonString, NULL);

	free(JsonString);

	return(rc);
}

// HandleMultiLinesMessage : in some cases, we may receive multi-lines messages
// We handle them here (it may seem redundant with the one-line version that
// can be handled inline the DispatchMessage() function)
static	int	HandleMultiLinesMessage(int ix)
{
	char	attributeName[1024];
	char	pvdId[PVDIDNAMESIZ];
	char	*pt;
	int	rc;
	t_StringBuffer	*SB = &lTabClients[ix].SB;

	// Isolate the 1st line
	if ((pt = strchr(SB->String, '\n')) != NULL) {
		*pt++ = '\0';
	}
	else {
		pt = "";
	}

	if (sscanf(
		SB->String,
		"PVDID_SET_ATTRIBUTE %[^ ] %[^ ]",
		pvdId,
		attributeName) == 2) {

		if (lTabClients[ix].pvdIdTransaction == NULL ||
		    ! EQSTR(lTabClients[ix].pvdIdTransaction, pvdId)) {
			DLOG("updating attribute for %s outside transaction\n", pvdId);
			return(0);
		}
		// Here, pt points to the 2nd line : it is the attributeValue and
		// is part of the allocated string buffer (be careful to not free
		// this string buffer before we have duplicated the attributeValue)
		rc = UpdateAttribute(GetPvdId(pvdId), attributeName, pt);

		SBUninit(SB);

		return(rc);
	}

	DLOG("Invalid multi-lines message received (%s)\n", SB->String);

	SBUninit(SB);

	return(0);
}

// DispatchMessage : given a message read from a client socket, handle it
// Only certain messages are allowed for a given connection type
// s can also be modified
// type : can be modified if an undefined connection gets promoted
//
// msg : line extracted from a buffer read on the socket, without a
// terminating \n, but with a \0 instead
// Some messages however are multi-line (typically the ones containing
// JSON payload). Multi-line messages are formatted as follows :
// PVDID_XXX ... <number of lines>
// ...
// ...
//
static	int	DispatchMessage(char *msg, int ix)
{
	char pvdId[PVDIDNAMESIZ];	// be careful with overflow
	int pvdIdHandle;
	int s = lTabClients[ix].s;
	int type = lTabClients[ix].type;

	DLOG("handling message %s on socket %d, type %d\n", msg, s, type);

	// Only one kind of promotion for now (more a restriction
	// than a promotion in fact)
	if (EQSTR(msg, "PVDID_CONNECTION_PROMOTE_CONTROL")) {
		// TODO : perform some credentials verifications
		if (lTabClients[ix].type == SOCKET_CONTROL) {
			// Already promoted (no way back)
			return(0);
		}
		if (lTabClients[ix].Subscription != NULL) {
			free(lTabClients[ix].Subscription);
			lTabClients[ix].Subscription = NULL;
		}
		lTabClients[ix].type = SOCKET_CONTROL;

		return(0);
	}

	// Control sockets : typically used by authorized clients to update
	// some pvdid attributes (or trigger maintenance tasks)
	if (type == SOCKET_CONTROL) {
		int	nLines;
		char	attributeName[1024];
		char	attributeValue[4096];

		// If we are reading a multi-lines string, just add it to the
		// current buffer. If this is the last line, process it
		if (lTabClients[ix].multiLines > 0) {
			if (SBAddString(&lTabClients[ix].SB, 
					"%s%s",
					msg,
					lTabClients[ix].multiLines == 1 ? "" : "\n") == -1) {
				// Don't fail : this is not the caller's fault
				return(0);
			}
			if (--lTabClients[ix].multiLines <= 0) {
				// Message fully received
				return(HandleMultiLinesMessage(ix));
			}
			return(0);
		}

		if (sscanf(msg, "PVDID_BEGIN_TRANSACTION %[^\n]", pvdId) == 1) {
			if (lTabClients[ix].pvdIdTransaction != NULL) {
				DLOG("beginning transaction for %s while %s still on-going\n",
				     pvdId,
				     lTabClients[ix].pvdIdTransaction);
				return(0);
			}

			lTabClients[ix].pvdIdTransaction = strdup(pvdId);

			return(0);
		}

		if (sscanf(msg, "PVDID_END_TRANSACTION %[^\n]", pvdId) == 1) {
			t_PvdId	*PtPvdId;

			if (lTabClients[ix].pvdIdTransaction == NULL) {
				DLOG("ending transaction for %s while no transaction on-going\n",
				     pvdId);
				return(-1);
			}
			if (! EQSTR(lTabClients[ix].pvdIdTransaction, pvdId)) {
				DLOG("ending transaction for %s while on-going one is %s\n",
				     pvdId,
				     lTabClients[ix].pvdIdTransaction);
				return(-1);
			}

			free(lTabClients[ix].pvdIdTransaction);
			lTabClients[ix].pvdIdTransaction = NULL;

			if ((PtPvdId = GetPvdId(pvdId)) != NULL) {
				if (PtPvdId->dirty) {
					NotifyPvdIdAttributes(PtPvdId);
					PtPvdId->dirty = false;
				}
			}
			return(0);
			
		}

		if (sscanf(msg, "PVDID_MULTILINE %d", &nLines) == 1) {
			lTabClients[ix].multiLines = nLines;
			SBUninit(&lTabClients[ix].SB);
			SBInit(&lTabClients[ix].SB);
			return(0);
		}

		// PVDID_SET_ATTRIBUTE message are special : either the
		// content fits on the line, either it is part of a
		// multi-lines string. We only handle here the one-line
		// version
		if (sscanf(
			msg,
			"PVDID_SET_ATTRIBUTE %[^ ] %[^ ] %[^\n]",
			pvdId,
			attributeName,
			attributeValue) == 3) {
			if (lTabClients[ix].pvdIdTransaction == NULL ||
			    ! EQSTR(lTabClients[ix].pvdIdTransaction, pvdId)) {
				DLOG("updating attribute for %s outside transaction\n",
				     pvdId);
				return(0);
			}
			return(UpdateAttribute(GetPvdId(pvdId), attributeName, attributeValue));
		}

		if (sscanf(msg, "PVDID_CREATE_PVDID %d %[^\n]", &pvdIdHandle, pvdId) == 2) {
			return(RegisterPvdId(pvdIdHandle, pvdId) != NULL);
		}

		if (sscanf(msg, "PVDID_REMOVE_PVDID %[^\n]", pvdId) == 1) {
			return(UnregisterPvdId(pvdId));
		}

		// Unknown message : don't fail on error
		DLOG("invalid message received (%s) on a control socket\n", msg);

		return(0);
	}

	// Non control clients
	// Beware : PVDID_SUBSCRIBE_NOTIFICATIONS must appear before the
	// sscanf(PVDID_SUBSCRIBE %[^\ ] otherwise the sscanf pattern will
	// catch the string !
	if (EQSTR(msg, "PVDID_SUBSCRIBE_NOTIFICATIONS")) {
		lTabClients[ix].SubscriptionMask = 0xFF;
		return(0);
	}

	if (EQSTR(msg, "PVDID_UNSUBSCRIBE_NOTIFICATIONS")) {
		lTabClients[ix].SubscriptionMask = 0;
		return(0);
	}

	if (sscanf(msg, "PVDID_SUBSCRIBE %[^\n]", pvdId) == 1) {
		AddSubscription(ix, pvdId);
		return(0);
	}
	if (sscanf(msg, "PVDID_UNSUBSCRIBE %[^\n]", pvdId) == 1) {
		RemoveSubscription(ix, pvdId);
		return(0);
	}

	if (EQSTR(msg, "PVDID_GET_LIST")) {
		if (SendPvdIdList(s) == -1) {
			goto BadExit;
		}
		return(0);
	}

	if (sscanf(msg, "PVDID_GET_ATTRIBUTES %[^\n]", pvdId) == 1) {
		// Send to the client all known attributes of the
		// associated pvdIdHandle. The attributes are sent
		// as a JSON object, with embedded \n : multi-lines
		// message
		if (SendAllAttributes(s, pvdId) == -1) {
			goto BadExit;
		}
		return(0);
	}

	// Unknown message : don't fail on error
	DLOG("invalid message received (%s) on a general socket\n", msg);
	return(0);

BadExit :
	ReleaseClient(ix);

	return(-1);
}

// A message has arrived on a socket of a given type (undefined, general, pvdid or
// control). Read it and handle it. We have specified line oriented messages
// A message can be built of multiple lines (FIXME : we only care of full lines,
// not of lines across buffer boundaries)
static	int	HandleMessage(int ix)
{
	static char lMsg[PVDID_MAX_MSG_SIZE];

	int	s = lTabClients[ix].s;
	int	type = lTabClients[ix].type;

	int	n;
	char	*pt = lMsg;
	char	*pt0 = lMsg;

	if ((n = recv(s, lMsg, sizeof(lMsg) - 1, MSG_DONTWAIT)) <= 0) {
		// Client disconnected
		DLOG("client for socket %d (type %d) disconnected (n = %d)\n", s, type, n);
		ReleaseClient(ix);
		return(-1);
	}
	DLOG("client for socket %d : message len = %d\n", s, n);

	lMsg[n] = '\0';

	while (*pt != '\0') {
		if (*pt == '\n') {
			*pt++ = '\0';
			if (DispatchMessage(pt0, ix) == -1) {
				return(-1);
			}
			pt0 = pt;
		}
		else {
			pt++;
		}
	}
	if (pt0 != pt) {
		return(DispatchMessage(pt0, ix));
	}
	return(0);
}

static	int	getint(char *s, int *PtN)
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

int	main(int argc, char **argv)
{
	int	i;
	int	Port = DEFAULT_PVDID_PORT;
	char	*PersistentDir = NULL;
	int	sockIcmpv6;
	int	serverSock;

	lMyName = basename(strdup(argv[0]));	// valgrind : leak on strdup

	for (i = 1; i < argc; i++) {
		if (EQSTR(argv[i], "-h") || EQSTR(argv[i], "--help")) {
			usage(NULL);
			return(0);
		}
		if (EQSTR(argv[i], "-v") || EQSTR(argv[i], "--verbose")) {
			lFlagVerbose = true;
			continue;
		}

		if (EQSTR(argv[i], "-p") || EQSTR(argv[i], "--port")) {
			if (++i < argc) {
				if (getint(argv[i], &Port) == -1) {
					return(usage("invalid client port (-p option)"));
				}
			}
			else {
				return(usage("missing argument for -p option"));
			}
			continue;
		}
		if (EQSTR(argv[i], "-d") || EQSTR(argv[i], "--dir")) {
			if (++i < argc) {
				PersistentDir = argv[i];
			}
			else {
				return(usage("missing argument for -d option"));
			}
			continue;
		}
	}

	if (lFlagVerbose) {
		printf("Server port : %d\n", Port);
		printf("Persistent directory : %s\n",
			PersistentDir == NULL ? "none defined" : PersistentDir);
	}

	/*
	 * Read and parse the existing persistent files and configuration file,
	 * if any
	 */

	/*
	 * Create the netlink raw socket with the kernel (to receive icmpv6
	 * options conveying the pvdid/dns data carried over by router
	 * advertisement messages)
	 */
	if ((sockIcmpv6 = open_icmpv6_socket()) == -1) {
		DLOG("can't create ICMPV6 netlink socket\n");
		// Don't fail for now
		// return(1);
	}

	/*
	 * TODO :
	 * On startup, we must query the kernel for its current
	 * RA tables (at least, for the current pvdId list)
	 */

	/*
	 * Create the listening clients socket
	 */
	if ((serverSock = CreateServerSocket(Port)) == -1) {
		perror("server socket");
		return(1);
	}

	/*
	 * Main loop
	 */
	while (true) {
		fd_set fdsI;
		int nMax = -1;
		int s, n;
		int FlagCompact;

		FD_ZERO(&fdsI);

		if (serverSock != -1) {
			FD_SET(serverSock, &fdsI);
			if (serverSock > nMax) nMax = serverSock;
		}

		if (sockIcmpv6 != -1) {
			FD_SET(sockIcmpv6, &fdsI);
			if (sockIcmpv6 > nMax) nMax = sockIcmpv6;
		}

		for (i = 0; i < lNClients; i++) {
			if ((s = lTabClients[i].s) != -1) {
				FD_SET(s, &fdsI);
				if (s > nMax) nMax = s;
			}
		}

		if (select(nMax + 1, &fdsI, NULL, NULL, NULL) == -1) {
			if (lFlagVerbose) {
				perror("pvdid-daemon select");
			}
			usleep(100000);
			continue;
		}

		if (serverSock != -1 && FD_ISSET(serverSock, &fdsI)) {
			HandleConnection(serverSock);
		}

		if (sockIcmpv6 != -1 && FD_ISSET(sockIcmpv6, &fdsI)) {
			HandleNetlink(sockIcmpv6);
		}

		FlagCompact = false;

		for (i = 0; i < lNClients; i++) {
			s = lTabClients[i].s;

			if (s == -1) {
				FlagCompact = true;
			} else
			if (FD_ISSET(s, &fdsI)) {
				HandleMessage(i);
			}
		}

		// Compact the clients table if necessary (we will be one loop
		// late, because HandleMessage() is the one which may
		// invalidate entries, but that does not matter a lot)
		if (FlagCompact) {
			for (n = 0, i = 0; i < lNClients; i++) {
				if ((s = lTabClients[i].s) != -1) {
					lTabClients[n++] = lTabClients[i];
				}
			}
			lNClients = n;
		}
	}

	return(0);
}

/*
 * Messages structures
 */
// General comment : the clients and the daemon communicate via sockets on 0.0.0.0:<port>
// The clients establish a connection with the daemon and must send an initial
// message intended to assign a 'type' to the connection
//
// Requests
// establish a connection with the daemon and send the following initial message :
//	PVDID_CONNECTION_PROMOTE_GENERAL
//	Note : the socket handle returned must be used for 'on the general connection' messages
//
// retrieve the list of current pvds on the general connection :
//	PVDID_GET_LIST\n
// retrieve a handle on a pvd on the general connection :
// 	PVDID_GET_HANDLE <pvdid>	(FQDN)
//
// establish a connection with a specific pvdid : initial message :
// 	PVDID_CONNECTION_PROMOTE_PVDID <pvdidhandle>
//
// retrieve all characteristics of a given pvdid. The result will always be a JSON
// object (for now)
// 	PVDID_GET_ATTRIBUTES
//
// establish a control connection (this allows filtering 'remotes' based on their
// credentials and reject connection with insufficient credentials). Initial message :
//	PVDID_CONNECTION_PROMOTE_CONTROL
//	Note : credentials will be carried by CAP_NET capabilities/SMACK/SELinux attributes
//
// provide extra information for a given pvdidhandle (control socket)
//	PVDID_SET_ATTRIBUTE <attribute name> <pvdidhandle> <...>\n
//
// Replies
// list of pvds :
// 	PVDID_LIST [ <pvdid>]*	(space separated)
// handle on a pvd
// 	PVDID_HANDLE <pvdid> <pvdidhandle>	(<FQDN> <32 bits integer>)
// attributes JSON formatted (on a pvdid socket) :
// 	PVDID_ATTRIBUTES <JSON string>\n"
//
// Notifications
// list of pvds :
// 	PVDID_LIST [ <pvdid>]*	(space separated)
// a pvdid has disappeared
//	PVDID_DELETE <pvdid>
// a pvdid has appeared (a PVDID_LIST message is also generated)
// 	PVDID_NEW <pvdid>
//
// Note : the pvdids conveyed in the PVDID_LIST messages are FQDN
//

/* ex: set ts=8 noexpandtab wrap: */
