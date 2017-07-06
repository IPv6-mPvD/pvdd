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
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <malloc.h>
#include <libgen.h>	// basename()
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>

#include "config.h"

#ifdef	HAS_PVDUSER
#include <linux/rtnetlink.h>
#else
#include "linux/rtnetlink.h"
#endif

#include "pvdid-defs.h"
#include "pvdid-daemon.h"
#include "pvdid-utils.h"
#include "pvdid-netlink.h"
#include "pvdid-rtnetlink.h"

#include "libpvdid.h"

/* constants and macros ------------------------------------------ */

#define	NEW(t)	((t *) malloc(sizeof(t)))

// Clients sockets types. General sockets can be promoted to control sockets
#define	SOCKET_GENERAL		1
#define	SOCKET_BINARY		2
#define	SOCKET_CONTROL		3

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

	/*
	 * Special case for the RDNSS/DNSSL options :
	 * they can be provided by 2 different channels and must
	 * be consolidated (user and kernel channels) to create
	 * the rdnss and dnssl attributes
	 */
	int	nKernelRdnss;
	struct in6_addr	KernelRdnss[MAXRDNSSPERPVD];

	int	nUserRdnss;
	struct in6_addr	UserRdnss[MAXRDNSSPERPVD];

	int	nKernelDnssl;
	char	*KernelDnssl[MAXDNSSLPERPVD];

	int	nUserDnssl;
	char	*UserDnssl[MAXDNSSLPERPVD];

	struct t_PvdId	*next;
}	t_PvdId;

/* variables declarations ---------------------------------------- */
static	int		lNClients = 0;
static	t_PvdIdClient	lTabClients[MAXCLIENTS];

static	t_PvdId	*lFirstPvdId = NULL;

static	char	*lMyName = "";

static	int	lKernelHasPvdSupport = false;

/* functions definitions ----------------------------------------- */
static	int	NotifyPvdIdAttributes(t_PvdId *PtPvdId);
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
	fprintf(fo, "\t-r|--use-cached-ra : retrieve the initial PvD list via a kernel cache\n");
	fprintf(fo,
		"\t-p|--port <#> : port number for clients requests (default %d)\n",
		DEFAULT_PVDID_PORT);
	fprintf(fo,
		"\t-d|--dir <path> : directory in which information is stored (none by default)\n");
	fprintf(fo,
		"\n"
		"Clients using the companion library can set the PVDID_PORT environment\n"
		"variable to specify another port than the default one\n");
	fprintf(fo,
		"\n"
		"Note that the kernel mechanism which caches the latest received RAs is not an\n"
		"accurate way of having an exact picture of the current addresses/routes/user\n"
		"options\n");


	return(s == NULL ? 0 : 1);
}

// PvdIdRdnssToJsonArray : aggregate the kernel and user RDNSS fields
// and buid a JSON string for this array of in6_addr values
// The returned string must be released by calling free()
char	*PvdIdRdnssToJsonArray(t_PvdId *PtPvdId)
{
	int 		nAddr;
	struct in6_addr Addresses[MAXRDNSSPERPVD * 2];
	int		i, j;
	t_StringBuffer	SB;
	char		sAddr[INET6_ADDRSTRLEN];

	nAddr = 0;
	for (i = 0; i < PtPvdId->nKernelRdnss; i++) {
		for (j = 0; j < nAddr; j++) {
			if (memcmp(
				&Addresses[j],
				&PtPvdId->KernelRdnss[i],
				sizeof(Addresses[j])) == 0) {
				break;
			}
		}
		if (j >= nAddr && nAddr < DIM(Addresses)) {
			Addresses[nAddr++] = PtPvdId->KernelRdnss[i];
		}
	}

	for (i = 0; i < PtPvdId->nUserRdnss; i++) {
		for (j = 0; j < nAddr; j++) {
			if (memcmp(
				&Addresses[j],
				&PtPvdId->UserRdnss[i],
				sizeof(Addresses[j])) == 0) {
				break;
			}
		}
		if (j >= nAddr && nAddr < DIM(Addresses)) {
			Addresses[nAddr++] = PtPvdId->UserRdnss[i];
		}
	}

	SBInit(&SB);

	SBAddString(&SB, "[");
	for (i = 0; i < nAddr; i++) {
		SBAddString(
			&SB,
			"\"%s\"",
			addrtostr(&Addresses[i], sAddr, sizeof(sAddr)));
		SBAddString(&SB, i == nAddr - 1 ? "" : ", ");
	}
	SBAddString(&SB, "]");

	return(SB.String);
}

// PvdIdDnsslToJsonArray : aggregate the kernel and user RDNSS fields
// and buid a JSON string for this array of in6_addr values
// The returned string must be released by calling free()
char	*PvdIdDnsslToJsonArray(t_PvdId *PtPvdId)
{
	int 		nDnssl;
	char		*Dnssl[MAXDNSSLPERPVD * 2];
	int		i, j;

	nDnssl = 0;
	for (i = 0; i < PtPvdId->nKernelDnssl; i++) {
		for (j = 0; j < nDnssl; j++) {
			if (strcmp(Dnssl[j], PtPvdId->KernelDnssl[i]) == 0) {
				break;
			}
		}
		if (j >= nDnssl && nDnssl < DIM(Dnssl)) {
			Dnssl[nDnssl++] = PtPvdId->KernelDnssl[i];
		}
	}

	for (i = 0; i < PtPvdId->nUserDnssl; i++) {
		for (j = 0; j < nDnssl; j++) {
			if (strcmp(Dnssl[j], PtPvdId->UserDnssl[i]) == 0) {
				break;
			}
		}
		if (j >= nDnssl && nDnssl < DIM(Dnssl)) {
			Dnssl[nDnssl++] = PtPvdId->UserDnssl[i];
		}
	}

	return(JsonArray(nDnssl, Dnssl));
}

// In6AddrToJsonArray : convert an array of in6_addr into its JSON string representation
// The returned string must be released by calling free()
char	*In6AddrToJsonArray(int nAddr, struct in6_addr *Addresses, int *PrefixesLen)
{
	int		i;
	t_StringBuffer	SB;
	char		sAddr[INET6_ADDRSTRLEN];

	SBInit(&SB);

	SBAddString(&SB, "[");
	for (i = 0; i < nAddr; i++) {
		SBAddString(&SB, "\n\t{");
		SBAddString(
			&SB,
			"\"address\" : \"%s\", ",
			addrtostr(&Addresses[i], sAddr, sizeof(sAddr)));
		SBAddString(&SB, "\"length\" : %d }", PrefixesLen[i]);
		SBAddString(&SB, i == nAddr - 1 ? "\n" : ",");
	}
	SBAddString(&SB, "]");

	return(SB.String);
}

// In6RoutesToJsonArray : convert an array of in6_addr into its JSON string representation
// The returned string must be released by calling free()
char	*In6RoutesToJsonArray(int nRoutes, struct net_pvd_route *Routes)
{
	int		i;
	t_StringBuffer	SB;
	char		sAddr[INET6_ADDRSTRLEN];

	SBInit(&SB);

	SBAddString(&SB, "[");
	for (i = 0; i < nRoutes; i++) {
		struct net_pvd_route *rt = &Routes[i];

		SBAddString(&SB, "\n\t{");
		SBAddString(
			&SB,
			"\"dst\" : \"%s\", ",
			addrtostr(&rt->dst, sAddr, sizeof(sAddr)));
		SBAddString(
			&SB,
			"\"gateway\" : \"%s\", ",
			addrtostr(&rt->gateway, sAddr, sizeof(sAddr)));
		SBAddString(&SB, "\"dev\" : \"%s\" }", JsonString(rt->dev_name));
		SBAddString(&SB, i == nRoutes - 1 ? "\n" : ",");
	}
	SBAddString(&SB, "]");

	return(SB.String);
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

// RemoveSubscription : remove a given pvdId from the list of subscribed pvdId
// for a client. Special case : if the given pvdId is NULL, the whole subscription
// list is released
static	int	RemoveSubscription(int ix, char *pvdId)
{
	t_PvdIdNameList	*pt = lTabClients[ix].Subscription;
	t_PvdIdNameList	*ptNext = NULL;
	t_PvdIdNameList	*ptPrev = NULL;

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

// ReleaseSubscriptionsList : release the list of subscriptions for a given
// client
static	void	ReleaseSubscriptionsList(int ix)
{
	t_PvdIdNameList *pt = lTabClients[ix].Subscription;
	t_PvdIdNameList *ptNext = NULL;

	while (pt != NULL) {
		ptNext = pt->next;
		free(pt->pvdId);
		free(pt);
		pt = ptNext;
	}
	lTabClients[ix].Subscription = NULL;
	return;
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
	ReleaseSubscriptionsList(ix);
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

// WriteString : send a string over a socket (any file descriptor in fact).
// Return true if the whole string could be written, false otherwise
static	int	WriteString(int s, char *str, int binary)
{
	int	l = strlen(str);
	if (binary) {
		return(write(s, &l, sizeof(l)) == sizeof(l) &&
		       write(s, str, l) == l);
	}
	return(write(s, str, l) == l);
}

// SendPvdIdList : send the current list of pvdId to a client that
// has requested it
static	int	SendPvdIdList(int s, int binary)
{
	char	msg[2048];
	t_PvdId	*PtPvdId;

	// FIXME : check for overflow
	sprintf(msg, "PVDID_LIST");
	for (PtPvdId = lFirstPvdId; PtPvdId != NULL; PtPvdId = PtPvdId->next) {
		strcat(msg, " ");
		strcat(msg, PtPvdId->pvdId);
	}
	strcat(msg, "\n");
	if (! WriteString(s, msg, binary)) {
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
			DLOG("NotifyPvdIdState : sending on socket %d msg %s", pt->s, msg);
			if (! WriteString(pt->s, msg, pt->type == SOCKET_BINARY)) {
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
	sprintf(msg, "PVDID_LIST ");	// Important : there must always be a ' '
	for (PtPvdId = lFirstPvdId; PtPvdId != NULL; PtPvdId = PtPvdId->next) {
		strcat(msg, PtPvdId->pvdId);
		if (PtPvdId->next != NULL) {
			strcat(msg, " ");
		}
	}
	strcat(msg, "\n");

	for (i = 0, pt = lTabClients; i < lNClients; i++, pt++) {
		if (pt->s == -1 || pt->type == SOCKET_CONTROL) {
			continue;
		}
		if ((pt->SubscriptionMask & SUBSCRIPTION_LIST) != 0) {
			DLOG("NotifyPvdIdList : sending on socket %d msg %s", pt->s, msg);
			if (! WriteString(pt->s, msg, pt->type == SOCKET_BINARY)) {
				ReleaseClient(i);
			}
		}
	}
}

/*
 * GetPvdIdByName : given a pvd name, retrieve its t_PvdId
 */
static	t_PvdId *GetPvdIdByName(char *pvdId)
{
	t_PvdId	*PtPvdId;

	for (PtPvdId = lFirstPvdId; PtPvdId != NULL; PtPvdId = PtPvdId->next) {
		if (EQSTR(PtPvdId->pvdId, pvdId)) {
			return(PtPvdId);
		}
	}
	return(NULL);
}

// RegisterPvdId : register a new pvdid. It should normally come from
// the kernel notifications, but, for debug purpose, might also be
// provided by clients on control sockets
// We may want to notify all clients (except control ones) that a
// new pvdid has appeared
static	t_PvdId	*RegisterPvdId(int pvdIdHandle, char *pvdId)
{
	t_PvdId	*PtPvdId;
	char	*tmpStr;

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
	memset(PtPvdId, 0, sizeof(*PtPvdId));

	if ((tmpStr = alloca(strlen(pvdId) * 3 + 2)) == NULL) {
		free(PtPvdId);
		DLOG("allocating pvdid : memory overflow\n");
		return(NULL);
	}

	PtPvdId->pvdIdHandle = pvdIdHandle == -1 ? 0 : pvdIdHandle;
	PtPvdId->pvdId = strdup(pvdId);	// TODO : check overflow
	PtPvdId->dirty = false;
	memset(PtPvdId->Attributes, 0, sizeof(PtPvdId->Attributes));

	/*
	 * Create the set of well known attributes (representing the
	 * various fields of the IETF definition of a PvD
	 */
	sprintf(tmpStr, "\"%s\"", JsonString(pvdId));
	PvdIdSetAttr(PtPvdId, "pvdId", tmpStr);
	sprintf(tmpStr, "%d", PtPvdId->pvdIdHandle);
	PvdIdSetAttr(PtPvdId, "pvdIdHandle", tmpStr);
	PvdIdSetAttr(PtPvdId, "sequenceNumber", "0");
	PvdIdSetAttr(PtPvdId, "hFlag", "0");	// by default
	PvdIdSetAttr(PtPvdId, "lFlag", "0");
	PvdIdSetAttr(PtPvdId, "lifetime", GetIntStr(0xffffffff));

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
int	UnregisterPvdId(char *pvdId)
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

// DeleteAttribute : delete a given attribute for a given pvd
static	int	DeleteAttribute(t_PvdId *PtPvdId, char *Key)
{
	int	i;

	if (PtPvdId == NULL) {
		DLOG("DeleteAttribute : unknown pvdId\n");
		return(0);
	}

	DLOG("DeleteAttribute : pvdId = %s, Key = %s\n", PtPvdId->pvdId, Key);

	for (i = 0; i < MAXATTRIBUTES; i++) {
		char	*attrKey = PtPvdId->Attributes[i].Key;
		char	*attrVal = PtPvdId->Attributes[i].Value;

		if (attrKey != NULL && EQSTR(attrKey, Key)) {
			if (attrVal != NULL) {
				free(attrVal);
				PtPvdId->Attributes[i].Value = NULL;
			}
			free(attrKey);
			PtPvdId->Attributes[i].Key = NULL;
			NotifyPvdIdAttributes(PtPvdId);
			break;
		}
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
	t_PvdAttribute	*Attributes = PtPvdId->Attributes;

	SBInit(&SB);

	SBAddString(&SB, "{");

	for (i = 0; i < MAXATTRIBUTES; i++) {
		if (Attributes[i].Key != NULL) {
			SBAddString(
				&SB,
				"%s\n\t\"%s\" : %s",
				i == 0 ? "" : ",",
				JsonString(Attributes[i].Key),
				Attributes[i].Value);
		}
	}

	SBAddString(&SB, "\n}\n");

	DLOG("PvdIdAttributes2Json(%s) : %s\n", PtPvdId->pvdId, SB.String);

	return(SB.String);
}

// SendMultiLines : send a multi-line string to a client. Multi-line messages are :
// PVDID_BEGIN_MULTILINE
// ...
// ...
// PVDID_END_MULTILINE
// In case of a binary promoted connection, there is no such MULTILINE header
// because binary connections messages are made of a length + data payload
static	int	SendMultiLines(int s, int binary, char *Prefix, ...)
{
	va_list ap;
	char	*pt;

	/*
	 * Header of the message :
	 * + length in case of binary connection
	 * + PVDID_BEGIN_MULTILINE string otherwise
	 */
	if (binary) {
		int	len = strlen(Prefix);

		// Computes the length of the payload
		va_start(ap, Prefix);
		while ((pt = va_arg(ap, char *)) != NULL) {
			len += strlen(pt);
		}
		va_end(ap);
		// Writes the length first
		if (write(s, &len, sizeof(len)) != sizeof(len)) {
			return(-1);
		}
	}
	else {
		if (! WriteString(s, "PVDID_BEGIN_MULTILINE\n", false)) {
			return(-1);
		}
	}

	/*
	 * The payload itself
	 */
	if (! WriteString(s, Prefix, false)) {
		return(-1);
	}

	va_start(ap, Prefix);
	while ((pt = va_arg(ap, char *)) != NULL) {
		if (! WriteString(s, pt, false)) {
			return(-1);
		}
	}
	va_end(ap);

	/*
	 * The trailer of the message :
	 * + nothing in case of binary connection
	 * + PVDID_END_MULTILINE string otherwise
	 */
	if (! binary) {
		if (! WriteString(s, "PVDID_END_MULTILINE\n", false)) {
			return(-1);
		}
	}

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
			if (EQSTR(pt->pvdId, pvdId) || EQSTR(pt->pvdId, "*")) {
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
			if (EQSTR(pt->pvdId, pvdId) || EQSTR(pt->pvdId, "*")) {
				if (SendMultiLines(
						s,
						lTabClients[i].type == SOCKET_BINARY,
						Prefix,
						JsonString,
						NULL) == -1) {
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

// SendOneAttribute : send a given attributes for a given pvdIdHandle to a given client
static	int	SendOneAttribute(int s, int binary, char *pvdId, char *attrName)
{
	int		i;
	char		Prefix[1024];
	t_PvdId		*PtPvdId;
	t_PvdAttribute	*Attributes;

	DLOG("send attribute %s for pvdid %s on socket %d\n", attrName, pvdId, s);

	if ((PtPvdId = GetPvdId(pvdId)) == NULL) {
		DLOG("%s : unknown PvD\n", pvdId);
		return(0);
	}

	Attributes = PtPvdId->Attributes;

	sprintf(Prefix, "PVDID_ATTRIBUTE %s %s\n", pvdId, attrName);

	for (i = 0; i < MAXATTRIBUTES; i++) {
		if (Attributes[i].Value != NULL &&
		    Attributes[i].Key != NULL &&
		    EQSTR(Attributes[i].Key, attrName)) {
			return(SendMultiLines(s, binary, Prefix, Attributes[i].Value, "\n", NULL));
		}
	}
	// Not found : send something to the client to avoid having it
	// waiting forever (in case of binary clients mostly)
	return(SendMultiLines(s, binary, Prefix, "null", "\n", NULL));
}

// SendAllAttributes : send the attributes for a given pvdIdHandle to a given client
static	int	SendAllAttributes(int s, int binary, char *pvdId)
{
	int	rc;
	char	Prefix[1024];
	char	*JsonString;
	t_PvdId	*PtPvdId;

	DLOG("send all attributes for pvdid %s on socket %d\n", pvdId, s);

	// Recursive call in case the client wants to receive the
	// attributes for all currently registered PvD
	if (EQSTR(pvdId, "*")) {
		t_PvdId	*PtPvdId;

		for (PtPvdId = lFirstPvdId; PtPvdId != NULL; PtPvdId = PtPvdId->next) {
			if ((rc = SendAllAttributes(s, binary, PtPvdId->pvdId)) != 0) {
				return(rc);
			}
		}
		return(0);
	}

	// Nominal case
	if ((PtPvdId = GetPvdId(pvdId)) == NULL) {
		return(0);
	}

	if ((JsonString = PvdIdAttributes2Json(PtPvdId)) == NULL) {
		return(0);
	}

	sprintf(Prefix, "PVDID_ATTRIBUTES %s\n", pvdId);

	rc = SendMultiLines(s, binary, Prefix, JsonString, NULL);

	free(JsonString);

	return(rc);
}

// HandleMultiLinesMessage : in some cases, we may receive multi-lines messages
// We handle them here (it may seem redundant with the one-line version that
// can be handled inline the DispatchMessage() function)
static	int	HandleMultiLinesMessage(int ix)
{
	char	attributeName[1024];
	char	pvdId[PVDNAMSIZ];
	char	*pt;
	int	rc;
	int	l;
	t_StringBuffer	*SB = &lTabClients[ix].SB;

	// Isolate the 1st line
	if ((pt = strchr(SB->String, '\n')) != NULL) {
		*pt++ = '\0';
	}
	else {
		pt = "";
	}

	// Remove the last \n if any
	l = strlen(pt);
	while (l > 0 && pt[l - 1] == '\n') {
		pt[--l] = '\0';
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
	char	attributeName[1024];
	char	attributeValue[4096];
	char	pvdId[PVDNAMSIZ];	// be careful with overflow
	int	pvdIdHandle;
	int	s = lTabClients[ix].s;
	int	type = lTabClients[ix].type;
	int	binary = type == SOCKET_BINARY;

	if (msg[0] != '\0') {
		DLOG("handling message %s on socket %d, type %d\n", msg, s, type);
	}

	// Only one kind of promotion for now (more a restriction
	// than a promotion in fact)
	if (EQSTR(msg, "PVDID_CONNECTION_PROMOTE_CONTROL")) {
		// TODO : perform some credentials verifications
		if (lTabClients[ix].type == SOCKET_CONTROL) {
			// Already promoted (no way back to regular connection)
			return(0);
		}
		if (lTabClients[ix].Subscription != NULL) {
			ReleaseSubscriptionsList(ix);
		}
		lTabClients[ix].type = SOCKET_CONTROL;

		return(0);
	}

	if (EQSTR(msg, "PVDID_CONNECTION_PROMOTE_BINARY")) {
		lTabClients[ix].type = SOCKET_BINARY;
		return(0);
	}

	// Control sockets : typically used by authorized clients to update
	// some pvdid attributes (or trigger maintenance tasks)
	if (type == SOCKET_CONTROL) {

		// Beginning of a multi-lines section ? We want to
		// test here (ie, before END_MULTILINE & Co) to have
		// the chance to reset the buffers in case we have missed
		// a previous multi-lines section END message
		if (EQSTR(msg, "PVDID_BEGIN_MULTILINE")) {
			lTabClients[ix].multiLines = true;
			SBUninit(&lTabClients[ix].SB);
			SBInit(&lTabClients[ix].SB);
			return(0);
		}

		// Are we at the end of a multi-lines section ?
		if (EQSTR(msg, "PVDID_END_MULTILINE")) {
			lTabClients[ix].multiLines = false;
			return(HandleMultiLinesMessage(ix));
		}

		// Are we inside a multi-lines section ? If yes just add it to
		// the current buffer
		if (lTabClients[ix].multiLines) {
			SBAddString(&lTabClients[ix].SB, "%s\n", msg);
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

		if (sscanf(
			msg,
			"PVDID_UNSET_ATTRIBUTE %[^ ] %[^\n]",
			pvdId,
			attributeName) == 2) {
			return(DeleteAttribute(GetPvdId(pvdId), attributeName));
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
			if (lKernelHasPvdSupport &&
			    (EQSTR(attributeName, "hFlag") ||
			     EQSTR(attributeName, "lFlag") ||
			     EQSTR(attributeName, "sequenceNumber") ||
			     EQSTR(attributeName, "lifetime"))) {
				if (kernel_update_pvd_attr(
						pvdId,
						attributeName,
						attributeValue) == -1) {
					perror("kernel_update_pvd_attr");
				}
				return(0);
			}
			return(UpdateAttribute(GetPvdId(pvdId), attributeName, attributeValue));
		}

		if (sscanf(msg, "PVDID_CREATE_PVDID %d %[^\n]", &pvdIdHandle, pvdId) == 2) {
			if (lKernelHasPvdSupport) {
				if (kernel_create_pvd(pvdId) == -1) {
					perror("kernel_create_pvd");
				}
				return(0);
			}
			return(RegisterPvdId(pvdIdHandle, pvdId) != NULL);
		}

		if (sscanf(msg, "PVDID_REMOVE_PVDID %[^\n]", pvdId) == 1) {
			if (lKernelHasPvdSupport) {
				if (kernel_update_pvd_attr(
						pvdId, "lifetime", "0") == -1) {
					perror("kernel_update_pvd_attr");
				}
				return(0);
			}
			return(UnregisterPvdId(pvdId));
		}

		// Unknown message : don't fail on error
		if (msg[0] != '\0') {
			DLOG("invalid message received (%s) on a control socket\n", msg);
		}

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
		if (SendPvdIdList(s, binary) == -1) {
			goto BadExit;
		}
		return(0);
	}

	// Once again, PVDID_GET_ATTRIBUTES must come BEFORE PVDID_GET_ATTRIBUTE
	if (sscanf(msg, "PVDID_GET_ATTRIBUTES %[^\n]", pvdId) == 1) {
		// Send to the client all known attributes of the
		// associated pvdIdHandle. The attributes are sent
		// as a JSON object, with embedded \n : multi-lines
		// message
		if (SendAllAttributes(s, binary, pvdId) == -1) {
			goto BadExit;
		}
		return(0);
	}

	if (sscanf(msg, "PVDID_GET_ATTRIBUTE %[^ ] %[^\n]", pvdId, attributeName) == 2) {
		if (SendOneAttribute(s, binary, pvdId, attributeName) == -1) {
			goto BadExit;
		}
		return(0);
	}

	// Unknown message : don't fail on error
	if (msg[0] != '\0') {
		DLOG("invalid message received (%s) on a general socket\n", msg);
	}
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

	if (n != 1) {
		DLOG("client for socket %d : message len = %d\n", s, n);
	}

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

static	int	RegisterPvdAttributes(struct net_pvd_attribute *pa)
{
	int i;
	char *pt;
	t_PvdId	*PtPvdId = RegisterPvdId(pa->index, pa->name);

	if (PtPvdId == NULL) {
		// Fatal error
		fprintf(stderr, "Can not register pvd %s\n", pa->name);
		return(-1);
	}
	PvdIdBeginTransaction(pa->name);
	PvdIdSetAttr(
		PtPvdId,
		"sequenceNumber",
		GetIntStr(pa->sequence_number));
	PvdIdSetAttr(PtPvdId, "hFlag", GetIntStr(pa->h_flag));
	PvdIdSetAttr(PtPvdId, "lFlag", GetIntStr(pa->l_flag));
	PvdIdSetAttr(PtPvdId, "lifetime", GetIntStr(pa->expires));

	PvdIdSetAttr(
		PtPvdId,
		"addresses",
		pt = In6AddrToJsonArray(
			pa->naddresses,
			pa->addresses,
			pa->addr_prefix_len));
	free(pt);

	PvdIdSetAttr(
		PtPvdId,
		"routes",
		pt = In6RoutesToJsonArray(pa->nroutes, pa->routes));
	free(pt);

	/*
	 * User options now : RDNSS/DNSSL
	 */
	for (i = 0; i < pa->nrdnss; i++) {
		PtPvdId->KernelRdnss[i] = pa->rdnss[i];
	}
	PtPvdId->nKernelRdnss = pa->nrdnss;

	for (i = 0; i < PtPvdId->nKernelDnssl; i++) {
		free(PtPvdId->KernelDnssl[i]);
	}
	for (i = 0; i < pa->ndnssl; i++) {
		PtPvdId->KernelDnssl[i] = strdup(pa->dnssl[i]);
	}
	PtPvdId->nKernelDnssl = pa->ndnssl;

	PvdIdSetAttr(
		PtPvdId,
		"rdnss",
		pt = PvdIdRdnssToJsonArray(PtPvdId));
	free(pt);

	PvdIdSetAttr(
		PtPvdId,
		"dnssl",
		pt = PvdIdDnsslToJsonArray(PtPvdId));
	free(pt);

	PvdIdEndTransaction(PtPvdId);

	return(0);
}

static	int	DeleteRdnss(int *nrdnss, struct in6_addr *rdnss, struct in6_addr *oneRdnss)
{
	int	i, j;

	for (i = 0; i < *nrdnss; i++) {
		if (memcmp(&rdnss[i], oneRdnss, sizeof(*oneRdnss)) == 0) {
			for (j = i + 1; j < *nrdnss; j++) {
				rdnss[j - 1] = rdnss[j];
			}
			(*nrdnss)--;
			return(1);
		}
	}
	return(0);
}

static	int	DeleteDnssl(int *ndnssl, char **dnssl, char *oneDnssl)
{
	int	i, j;

	for (i = 0; i < *ndnssl; i++) {
		if (EQSTR(dnssl[i], oneDnssl)) {
			free(dnssl[i]);
			for (j = i + 1; j < *ndnssl; j++) {
				dnssl[j - 1] = dnssl[j];
			}
			(*ndnssl)--;
			return(1);
		}
	}
	return(0);
}

static	void	HandleRtNetlink(t_rtnetlink_cnx *cnx)
{
	void *vmsg;
	int type;
	int rc;
	t_PvdId *PtPvdId;

	if ((vmsg = rtnetlink_recv(cnx, &type)) == NULL) {
		return;
	}

	if (type == RTM_PVDSTATUS) {
		struct pvdmsg *pvdmsg = vmsg;
		struct net_pvd_attribute attr;

		printf("HandleRtNetlink : RTM_PVDSTATUS received\n");

		if (pvdmsg->pvd_state == PVD_NEW || pvdmsg->pvd_state == PVD_UPDATE) {
			if (kernel_get_pvd_attributes(pvdmsg->pvd_name, &attr) == 0) {
				RegisterPvdAttributes(&attr);
			}
			else {
				perror("kernel_get_pvd_attribute");
			}
			return;
		}

		if (pvdmsg->pvd_state == PVD_DEL) {
			UnregisterPvdId(pvdmsg->pvd_name);
			return;
		}
		return;
	}

	if (type == RTM_RDNSS) {
		struct rdnssmsg *rdnssmsg = vmsg;

		DLOG("HandleRtNetlink : RTM_RDNSS received (state = %s)\n",
			rdnssmsg->rdnss_state == RDNSS_DEL ?
				"RDNSS_DEL" : "RDNSS_NEW");

		/*
		 * RDNSS_DEL can reference a user defined RDNSS
		 */
		if (rdnssmsg->rdnss_state == RDNSS_DEL) {
			if ((PtPvdId = GetPvdIdByName(rdnssmsg->pvd_name)) != NULL) {
				rc = DeleteRdnss(
					&PtPvdId->nKernelRdnss,
					PtPvdId->KernelRdnss,
					&rdnssmsg->rdnss);
				rc += DeleteRdnss(
					&PtPvdId->nUserRdnss,
					PtPvdId->UserRdnss,
					&rdnssmsg->rdnss);
				if (rc != 0) {
					NotifyPvdIdAttributes(PtPvdId);
				}
			}
		}
		return;
	}

	if (type == RTM_DNSSL) {
		struct dnsslmsg *dnsslmsg = vmsg;

		DLOG("HandleRtNetlink : RTM_DNSSL received (state = %s)\n",
			dnsslmsg->dnssl_state == DNSSL_DEL ?
				"DNSSL_DEL" :"DNSSL_NEW");

		/*
		 * DNSSL_DEL can reference a user defined DNSSL
		 */
		if (dnsslmsg->dnssl_state == DNSSL_DEL) {
			if ((PtPvdId = GetPvdIdByName(dnsslmsg->pvd_name)) != NULL) {
				rc = DeleteDnssl(
					&PtPvdId->nKernelDnssl,
					PtPvdId->KernelDnssl,
					dnsslmsg->dnssl);
				rc += DeleteDnssl(
					&PtPvdId->nUserDnssl,
					PtPvdId->UserDnssl,
					dnsslmsg->dnssl);
				if (rc != 0) {
					NotifyPvdIdAttributes(PtPvdId);
				}
			}
		}
		return;
	}
}

int	main(int argc, char **argv)
{
	int		i;
	int		Port = DEFAULT_PVDID_PORT;
	char		*PersistentDir = NULL;
	int		sockIcmpv6 = -1;
	int		serverSock;
	struct pvd_list	pvl;	/* careful : this can be quite big */
	struct ra_list	*ral;
	int		FlagUseCachedRa = false;
	t_rtnetlink_cnx	*RtnlCnx = NULL;
	int		sockRtnlink = -1;

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
		if (EQSTR(argv[i], "-r") || EQSTR(argv[i], "--use-cached-ra")) {
			FlagUseCachedRa = true;
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
		printf("sizeof net_pvd_attribute = %lu\n",
			(unsigned long) sizeof(struct net_pvd_attribute));
		printf("sizeof pvd_list = %lu\n",
			(unsigned long) sizeof(struct pvd_list));
		printf("PVDNAMSIZ = %d\n", PVDNAMSIZ);
	}

	signal(SIGPIPE, SIG_IGN);

	/*
	 * Read and parse the existing persistent files and configuration file,
	 * if any
	 */

	/*
	 * Here, we need to decide how to retrieve PvD information :
	 * + on kernels unaware of PvD, the applications may still receive
	 *   RAs via a icmpv6_socket. This is not entirely accurate as some
	 *   RAs may have been received before the application has started
	 *   (or be restarted in case of crash). In addition, this requires
	 *   the application to also handle lifetimes of the various fields
	 *   More than that, it will be more complicated for the application
	 *   to associate PvD and induced addresses and routes
	 *
	 * + on PvD aware kernels, a new rtnetlink group should be present
	 *   to notify
	 *
	 * We determine in which case we are by calling the pvd_get_list()
	 * function (which ends up calling a socket() specific function,
	 * returning ENOPROTOOPT eventually). If this call is supported
	 * by the kernel, we assume that the whole PvD functionality is
	 * supported by it
	 *
	 * Ultimately, support for the 1st case will be dropped
	 */
	/*
	 * On startup, we must query the kernel for its current
	 * RA tables (at least, for the current pvdId list).
	 * An error can occur if the kernel is not recognizing
	 * the command (ENOPROTOOPT)
	 */
	if (FlagUseCachedRa) {
		goto get_cached_ra;
	}

	pvl.npvd = MAXPVD;
	if (kernel_get_pvdlist(&pvl) != -1) {
		struct net_pvd_attribute attr;

		lKernelHasPvdSupport = true;

		DLOG("%d pvd retrieved from kernel\n", pvl.npvd);

		for (i = 0; i < pvl.npvd; i++) {
			if (kernel_get_pvd_attributes(pvl.pvds[i],  &attr) == 0) {
				RegisterPvdAttributes(&attr);
			}
			else {
				perror("kernel_get_pvd_attribute");
			}
		}
	}
	else {
		if (lFlagVerbose) {
			perror("kernel_get_pvdlist");
			if (errno == ENOPROTOOPT) {
				fprintf(stderr,
					"++++ Assuming kernel not PvD aware\n");
			}
		}
	}
	goto skip_cached_ra;

get_cached_ra :
	lKernelHasPvdSupport = true;	/* user's responsibility */

	if ((ral = ralist_alloc(16)) != NULL) {
		if (kernel_get_ralist(ral) != -1) {
			for (i = 0; i < ral->nra; i++) {
				char if_namebuf[IF_NAMESIZE] = {""};
				process_ra(
					ral->array[i].ra, 
					ral->array[i].ra_size,
					NULL,
					&ral->array[i].saddr,
					if_indextoname(ral->array[i].ifindex, if_namebuf));
			}
		}
		else {
			perror("kernel_get_ralist");
		}
		ralist_release(ral);
	}

skip_cached_ra :
	if (lFlagVerbose) {
		fprintf(stderr,
			"+++ Kernel %s PvD support\n",
			lKernelHasPvdSupport ? "has" : " does not have");
	}

	/*
	 * Create the netlink raw socket with the kernel (to receive icmpv6
	 * options conveying the pvdid/dns data carried over by router
	 * advertisement messages)
	 */
	if (! lKernelHasPvdSupport && (sockIcmpv6 = open_icmpv6_socket()) == -1) {
		DLOG("can't create ICMPV6 netlink socket\n");
		// Don't fail for now
		// return(1);
	}

	if (lKernelHasPvdSupport) {
		if ((RtnlCnx = rtnetlink_connect()) != NULL) {
			sockRtnlink = rtnetlink_get_fd(RtnlCnx);
			if (lFlagVerbose) {
				fprintf(stderr,
					"Monitoring rtnetlink (fd = %d)\n",
					sockRtnlink);
			}
		}
	}

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

		if (sockRtnlink != -1) {
			FD_SET(sockRtnlink, &fdsI);
			if (sockRtnlink > nMax) nMax = sockRtnlink;
		}

		for (i = 0; i < lNClients; i++) {
			if ((s = lTabClients[i].s) != -1) {
				FD_SET(s, &fdsI);
				if (s > nMax) nMax = s;
			}
		}

		if (select(nMax + 1, &fdsI, NULL, NULL, NULL) == -1) {
			if (lFlagVerbose) {
				perror("pvdd select");
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

		if (sockRtnlink != -1 && FD_ISSET(sockRtnlink, &fdsI)) {
			HandleRtNetlink(RtnlCnx);
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

/* ex: set ts=8 noexpandtab wrap: */
