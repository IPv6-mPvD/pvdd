/*
 * This header file contains definitions for use when exchanging
 * data with the user space
 *
 * The same file will also be installed for use by user space
 * applications
 *
 * SO_BINDTOPVD and Co are defined in another uapi header file
 * (asm-generic/socket.h)
 */
#ifndef _LINUX_PVD_USER_H
#define _LINUX_PVD_USER_H

#include <linux/in6.h>
#include <net/if.h>
#include <linux/ipv6_route.h>

/*
 * MAXPVD must be a power of 2
 */
#define	MAXPVDSHIFT	10	/* realistic upper bound */
#define	MAXPVD		(1 << MAXPVDSHIFT)

#ifndef	PVDNAMSIZ
#define	PVDNAMSIZ	256
#endif

#ifndef	FQDNSIZ
#define	FQDNSIZ		256
#endif

#ifndef MAXADDRPERPVD
#define	MAXADDRPERPVD	32
#endif

#ifndef MAXROUTESPERPVD
#define	MAXROUTESPERPVD	32
#endif

#ifndef	MAXDNSSLPERPVD
#define	MAXDNSSLPERPVD	4
#endif

#ifndef	MAXRDNSSPERPVD
#define	MAXRDNSSPERPVD	4
#endif

/*
 * For SO_GETPVDATTRIBUTES (which returns the attributes for a pvd)
 *
 * The net_pvd_attribute structure below is a copy of the
 * kernel space pvd structure, with kernel specific items
 * removed
 */
struct net_pvd_route {
	struct in6_addr	dst;
	struct in6_addr gateway;
	char dev_name[IFNAMSIZ];
};

struct net_pvd_attribute {
	char			name[PVDNAMSIZ];
	int			index;	/* unique number */

	/*
	 * Attributes of the pvd
	 */
	int			sequence_number;
	int			h_flag;
	int			l_flag;
	int 		a_flag;  // introduced in 01
	int			implicit_flag;
	struct in6_addr		lla;
	char			dev[IFNAMSIZ];

	/*
	 * Induced attributes
	 */
	int			nroutes;
	struct net_pvd_route	routes[MAXROUTESPERPVD];
	int			naddresses;
	struct in6_addr		addresses[MAXADDRPERPVD];
	int			addr_prefix_len[MAXADDRPERPVD];

	int			ndnssl;
	char			dnssl[MAXDNSSLPERPVD][FQDNSIZ];

	int			nrdnss;
	struct in6_addr		rdnss[MAXRDNSSPERPVD];
};

struct pvd_attr {
	char *pvdname;	/* in */
	struct net_pvd_attribute *pvdattr;	/* out */
};

/*
 * For SO_GETPVDLIST (returns the list of pvds)
 */
struct pvd_list {
	int npvd;	/* in/out */
	char pvds[MAXPVD][PVDNAMSIZ];
};

/*
 * For SO_BINDTOPVD (set and get)
 */
#define	PVD_BIND_SCOPE_SOCKET	0
#define	PVD_BIND_SCOPE_THREAD	1
#define	PVD_BIND_SCOPE_PROCESS	2

#define	PVD_BIND_INHERIT	0
#define	PVD_BIND_NOPVD		1
#define	PVD_BIND_ONEPVD		2

struct bind_to_pvd {
	int scope;	/* PVD_BIND_SCOPE_XXX */
	int bindtype;	/* PVD_BIND_INHERIT, ... */
	char pvdname[PVDNAMSIZ];
};

/*
 * For SO_CREATEPVD
 */
struct create_pvd {
	char pvdname[PVDNAMSIZ];
	int flag;	/* mask : see below PVD_ATTR_XXX */
	int sequence_number;
	int h_flag;
	int l_flag;
	int deprecated;
};

#define	PVD_ATTR_SEQNUMBER	0x01
#define	PVD_ATTR_HFLAG		0x02
#define	PVD_ATTR_LFLAG		0x04
#define	PVD_ATTR_DEPRECATED	0x08

/*
 * RTNETLINK related definitions
 */
/********************************************************************
 *		PvD description
 *		This is used to notify a change in a PvD
 *		Application may then query the attributes for the
 *		notified PvD using an alternate API
 ****/

struct pvdmsg {
	char	pvd_name[PVDNAMSIZ];
	int	pvd_state;	/* NEW, UPDATE, DEL */
};

enum {
	/*
	 * PVD_NEW and PVD_UPDATE will certainly be handled in the same way
	 * by application
	 */
	PVD_NEW,
	PVD_UPDATE,
	PVD_DEL
};

struct rdnssmsg {
	char		pvd_name[PVDNAMSIZ];
	struct in6_addr	rdnss;
	int		rdnss_state;	/* NEW, DEL */
};

enum {
	RDNSS_NEW,
	RDNSS_DEL
};

struct dnsslmsg {
	char	pvd_name[PVDNAMSIZ];
	char	dnssl[FQDNSIZ];
	int	dnssl_state;	/* NEW, DEL */
};

enum {
	DNSSL_NEW,
	DNSSL_DEL
};

#endif		/* _LINUX_PVD_USER_H */
