
#ifndef	LIBPVDID_H
#define	LIBPVDID_H

#ifndef	PVDNAMSIZ
#define	PVDNAMSIZ	1024
#endif

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

struct bind_to_pvd {
	int	npvd;	/* in/out */
	char	pvdnames[MAXBOUNDPVD][PVDNAMSIZ];
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

/*
 * Communication with the pvdid-daemon
 * Asynchronous notifications require the application to parse the
 * incoming strings. They are received via calls to recv()/read()
 * on the socket returned by pvdid_connect()
 */
extern int	pvdid_connect(int Port);
extern void	pvdid_disconnect(int fd);
extern int	pvdid_reconnect(int fd);
extern int	pvdid_get_control_socket(int fd);
extern int	pvdid_get_binary_socket(int fd);
extern int	pvdid_get_pvdid_list(int fd);
extern int	pvdid_parse_pvdid_list(char *msg, t_pvdid_list *pvdIdList);
extern int	pvdid_get_pvdid_list_sync(int fd, t_pvdid_list *pvdIdList);
extern int	pvdid_get_attributes(int fd, char *pvdId);
extern int	pvdid_get_attributes_sync(int fd, char *pvdId, char **attributes);
extern int	pvdid_get_attribute(int fd, char *pvdId, char *attrName);
extern int	pvdid_get_attribute_sync(int fd, char *pvdId, char *attrName, char **attrValue);
extern int	pvdid_subscribe_notifications(int fd);
extern int	pvdid_unsubscribe_notifications(int fd);
extern int	pvdid_subscribe_pvdid_notifications(int fd, char *pvdId);
extern int	pvdid_unsubscribe_pvdid_notifications(int fd, char *pvdId);
extern int	pvdid_parse_rdnss(char *msg, t_pvdid_rdnss *PtRdnss);
extern int	pvdid_parse_dnssl(char *msg, t_pvdid_dnssl *PtDnssl);
extern int	pvdid_get_rdnss(int fd, char *pvdId);
extern int	pvdid_get_rdnss_sync(int fd, char *pvdId, t_pvdid_rdnss *PtRdnss);
extern int	pvdid_get_dnssl(int fd, char *pvdId);
extern int	pvdid_get_dnssl_sync(int fd, char *pvdId, t_pvdid_dnssl *PtDnssl);
extern void	pvdid_release_rdnss(t_pvdid_rdnss *PtRdnss);
extern void	pvdid_release_dnssl(t_pvdid_dnssl *PtDnssl);

/*
 * Encapsulation of setsockopt/getsockopt calls (direct kernel communication)
 */
extern	int	sock_bind_to_pvd(int s, char *pvdname);
extern	int	sock_get_bound_pvd(int s, char *pvdname);
extern	int	pvd_get_list(struct pvd_list *pvl);


#endif		/* LIBPVDID_H */
