
#ifndef	LIBPVDID_H
#define	LIBPVDID_H

#ifndef	PVDNAMSIZ
#define	PVDNAMSIZ	1024
#endif

/*
 * Opaque structure carrying a daemon connection
 */
typedef	struct t_pvd_connection	t_pvd_connection;

#ifndef	MAXPVD
/*
 * MAXPVD must be a power of 2
 */
#define	MAXPVDSHIFT	10	/* realistic upper bound */
#define	MAXPVD		(1 << MAXPVDSHIFT)

#endif

typedef	struct
{
	int	nPvdId;
	char	*pvdIdList[MAXPVD];
}	t_pvdid_list;

typedef	struct {
	int	nRdnss;
	char	*Rdnss[3];
}	t_pvdid_rdnss;

typedef	struct {
	int	nDnssl;
	char	*Dnssl[8];
}	t_pvdid_dnssl;

#ifndef	SO_BINDTOPVD
#define	SO_BINDTOPVD	55

#define MAXBOUNDPVD     32

#define	PVD_BIND_SCOPE_SOCKET	0
#define	PVD_BIND_SCOPE_THREAD	1
#define	PVD_BIND_SCOPE_PROCESS	2

struct bind_to_pvd {
	int scope;
	int npvd;	/* in/out */
	char pvdnames[MAXBOUNDPVD][PVDNAMSIZ];
};

#endif

#ifndef	SO_GETPVDINFO
#define	SO_GETPVDINFO		56

/*
 * For SO_GETPVDINFO
 */
struct net_pvd_attribute {
	char			name[PVDNAMSIZ];
	int			index;	/* unique number */

	/*
	 * Attributes of the pvd
	 */
	int			sequence_number;
	int			h_flag;
	int			l_flag;

	unsigned long		expires;	/* lifetime field */
};


struct pvd_list {
	int npvd;	/* in/out */
	struct net_pvd_attribute pvds[MAXPVD];
};

#endif	/* SO_GETPVDINFO */

#ifndef	SO_GETRALIST
#define	SO_GETRALIST	57

#include <netinet/ip6.h>

/*
 * For SO_GETRALIST
 */
#define	_RALIST_HEADER \
	int	size;		/* total structure size */\
	int	buffer_size;	/* depends on max_ras */\
	char	*buffer;	/* allocated */\
	int	nra;		/* output */\
	int	max_ras;

struct ra_buffer {
	int		ra_size;
	unsigned char	*ra;
	int		ifindex;
	struct in6_addr	saddr;
};

struct ra_list {
	_RALIST_HEADER
	struct ra_buffer array[0];	/* variable size : [max_ras] */
	/* array will be followed by a buffer */
};

#endif	/* SO_GETRALIST */

/*
 * Return codes
 */
#define	PVD_READ_BUFFER_FULL	-1
#define	PVD_READ_ERROR		-2
#define	PVD_READ_OK		0

#define	PVD_NO_MESSAGE_READ	-1
#define	PVD_MESSAGE_READ	0
#define	PVD_MORE_DATA_AVAILABLE	1

/*
 * Communication with the pvdid-daemon
 * Asynchronous notifications require the application to parse the
 * incoming strings. They are received via calls to recv()/read()
 * on the socket returned by pvdid_connect()
 */
extern t_pvd_connection	*pvdid_connect(int Port);
extern void		pvdid_disconnect(
				t_pvd_connection *conn);
extern t_pvd_connection *pvdid_reconnect(
				t_pvd_connection *conn);
extern t_pvd_connection	*pvdid_get_control_socket(
				t_pvd_connection *conn);
extern t_pvd_connection	*pvdid_get_binary_socket(
				t_pvd_connection *conn);
extern int		pvdid_get_pvdid_list(
				t_pvd_connection *conn);
extern int		pvdid_get_pvdid_list_sync(
				t_pvd_connection *conn,
				t_pvdid_list *pvdIdList);
extern int		pvdid_get_attributes(
				t_pvd_connection *conn,
				char *pvdId);
extern int		pvdid_get_attributes_sync(
				t_pvd_connection *conn,
				char *pvdId,
				char **attributes);
extern int		pvdid_get_attribute(
				t_pvd_connection *conn,
				char *pvdId, 
				char *attrName);
extern int		pvdid_get_attribute_sync(
				t_pvd_connection *conn,
				char *pvdId, 
				char *attrName, 
				char **attrValue);
extern int		pvdid_subscribe_notifications(
				t_pvd_connection *conn);
extern int		pvdid_unsubscribe_notifications(
				t_pvd_connection *conn);
extern int		pvdid_subscribe_pvdid_notifications(
				t_pvd_connection *conn,
				char *pvdId);
extern int		pvdid_unsubscribe_pvdid_notifications(
				t_pvd_connection *conn,
				char *pvdId);
extern int		pvdid_get_rdnss(
				t_pvd_connection *conn,
				char *pvdId);
extern int		pvdid_get_rdnss_sync(
				t_pvd_connection *conn,
				char *pvdId,
				t_pvdid_rdnss *PtRdnss);
extern int		pvdid_get_dnssl(
				t_pvd_connection *conn,
				char *pvdId);
extern int		pvdid_get_dnssl_sync(
				t_pvd_connection *conn,
				char *pvdId, 
				t_pvdid_dnssl *PtDnssl);

/*
 * Accessors
 */
#define	INVALID_CONNECTION	0
#define	REGULAR_CONNECTION	1
#define	CONTROL_CONNECTION	2
#define	BINARY_CONNECTION	3

extern int		pvd_connection_fd(t_pvd_connection *conn);
extern int		pvd_connection_type(t_pvd_connection *conn);

/*
 * Helper functions
 */
extern int		pvdid_parse_pvdid_list(char *msg, t_pvdid_list *pvdIdList);
extern int		pvdid_parse_rdnss(char *msg, t_pvdid_rdnss *PtRdnss);
extern int		pvdid_parse_dnssl(char *msg, t_pvdid_dnssl *PtDnssl);
extern void		pvdid_release_rdnss(t_pvdid_rdnss *PtRdnss);
extern void		pvdid_release_dnssl(t_pvdid_dnssl *PtDnssl);

extern	int		pvdid_read_data(t_pvd_connection *conn);
extern	int		pvdid_get_message(t_pvd_connection *conn, int *multiLines, char **msg);

/*
 * Encapsulation of setsockopt/getsockopt calls (direct kernel communication)
 */
extern	int	sock_bind_to_pvd(int s, char *pvdname);
extern	int	sock_get_bound_pvd(int s, char *pvdname);
extern	int	pvd_get_list(struct pvd_list *pvl);
extern	struct ra_list	*ralist_alloc(int max_ras);
extern	void	ralist_release(struct ra_list *ral);
extern	int	kernel_get_ralist(struct ra_list *ral);

#endif		/* LIBPVDID_H */
