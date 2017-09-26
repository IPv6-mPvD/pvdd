/*
 * This header file contains definitions for use when exchanging
 * data with the user space
 *
 * The same file will also be installed for use by user space
 * applications
 *
 * SO_GETPVDINFO, SO_BINDTOPVD and SO_GETRALIST are defined in
 * another uapi header file (asm-generic/socket.h)
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
 * For SO_GETPVDINFO (which returns in one call all pvd and
 * their attributes) or SO_GETPVDATTRIBUTES (which returns
 * the attributes for a given PvD)
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

struct pvd_list {
	int npvd;	/* in/out */
	char pvds[MAXPVD][PVDNAMSIZ];
};

struct pvd_attr {
	char *pvdname;	/* in */
	struct net_pvd_attribute *pvdattr;	/* out */
};

/*
 * For SO_BINDTOPVD (set and get)
 */
#define	PVD_BIND_SCOPE_SOCKET	0
#define	PVD_BIND_SCOPE_THREAD	1
#define	PVD_BIND_SCOPE_PROCESS	2

struct bind_to_pvd {
	int scope;
	/*
	 * npvd : in
	 * -1 : inherit
	 *  0 : forcibly unset
	 *  1 : forcibly set
	 */
	int npvd;
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
 * For SO_RT6PVD
 */
struct in6_rt_pvdmsg {
	char pvdname[PVDNAMSIZ];
	struct in6_rtmsg rtmsg;
};

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
	char	pvd_name[PVDNAMSIZ];
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
