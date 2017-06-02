/*
 * INSERT PROPER HEADER HERE
 */

/*
 * This file provides netlink access, mainly to receive
 * RA notifications via the icmpv6 options
 *
 * Most of its code has been borrowed from radvd and has
 * been simplified to no longer check irrelevant error
 * patterns (mostly consistency checks done by radvd when
 * receiving RAs against what it had advertised)
 */

#define _GNU_SOURCE	// to have in6_pktinfo defined

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/icmp6.h>
#include <netinet/ip6.h>
#include <arpa/inet.h>

#include <net/if.h>

#include "pvdid-defs.h"
#include "pvdid-daemon.h"
#include "pvdid-netlink.h"
#include "pvdid-utils.h"

#define	_DLOG(level, args...)	DLOG(args)

#define	LOG_WARNING	0
#define	LOG_ERR		1
#define	LOG_DEBUG	2
#define	LOG_INFO	3

#define MSG_SIZE_RECV 1500

/* Option types (defined also at least in glibc 2.2's netinet/icmp6.h) */

#ifndef ND_OPT_RTR_ADV_INTERVAL
#define ND_OPT_RTR_ADV_INTERVAL 7
#endif
#ifndef ND_OPT_HOME_AGENT_INFO
#define ND_OPT_HOME_AGENT_INFO 8
#endif

/* de-facto codepoint used by many implementations was '9',
   the official IANA assignment is '24' */
#undef ND_OPT_ROUTE_INFORMATION
#define ND_OPT_ROUTE_INFORMATION 24

/* XXX: some libc's like KAME already had nd_opt_route_info! */
struct nd_opt_route_info_local { /* route information */
	uint8_t nd_opt_ri_type;
	uint8_t nd_opt_ri_len;
	uint8_t nd_opt_ri_prefix_len;
	uint8_t nd_opt_ri_flags_reserved;
	uint32_t nd_opt_ri_lifetime;
	struct in6_addr nd_opt_ri_prefix;
};

/* the reserved field is 8 bits and we're interested of the middle two: 000xx000 */
#define ND_OPT_RI_PRF_SHIFT 3
#define ND_OPT_RI_PRF_MASK (3 << ND_OPT_RI_PRF_SHIFT) /* 00011000 = 0x18 */

#undef ND_OPT_RDNSS_INFORMATION
#define ND_OPT_RDNSS_INFORMATION 25

/* */
struct nd_opt_pvdid {
	uint8_t nd_opt_pvdid_type;
	uint8_t nd_opt_pvdid_len;
	uint8_t nd_opt_pvdid_seq : 4;
	uint8_t nd_opt_pvdid_h : 1;
	uint8_t nd_opt_pvdid_l : 1;
	uint16_t nd_opt_pvdid_reserved : 10;
	uint32_t nd_opt_pvdid_lifetime;
	unsigned char nd_opt_pvdid_name[];
};


struct nd_opt_rdnss_info_local {
	uint8_t nd_opt_rdnssi_type;
	uint8_t nd_opt_rdnssi_len;
	uint16_t nd_opt_rdnssi_pref_flag_reserved;
	uint32_t nd_opt_rdnssi_lifetime;
	struct in6_addr nd_opt_rdnssi_addr1;
	struct in6_addr nd_opt_rdnssi_addr2;
	struct in6_addr nd_opt_rdnssi_addr3;
};
/* pref/flag/reserved field : yyyyx00000000000 (big endian) - 00000000yyyyx000 (little indian); where yyyy = pref, x = flag */
#if BYTE_ORDER == BIG_ENDIAN
#define ND_OPT_RDNSSI_PREF_SHIFT 12
#else
#define ND_OPT_RDNSSI_PREF_SHIFT 4
#endif
#define ND_OPT_RDNSSI_PREF_MASK (0xf << ND_OPT_RDNSSI_PREF_SHIFT)

#undef ND_OPT_DNSSL_INFORMATION
#define ND_OPT_DNSSL_INFORMATION 31

/* */
struct nd_opt_dnssl_info_local {
	uint8_t nd_opt_dnssli_type;
	uint8_t nd_opt_dnssli_len;
	uint16_t nd_opt_dnssli_reserved;
	uint32_t nd_opt_dnssli_lifetime;
	unsigned char nd_opt_dnssli_suffixes[];
};

/* PVDID option */
/*
 * nd_opt_pvdidi_reserved1 : sssshl00
 * nd_opt_pvdidi_reserved2 : 00000000
 */
#ifndef	ND_OPT_PVDID
#define	ND_OPT_PVDID	253	// Waiting for IANA attribution
#endif

struct nd_opt_pvdid_info_local {
	uint8_t nd_opt_pvdidi_type;
	uint8_t nd_opt_pvdidi_len;
	uint8_t nd_opt_pvdidi_reserved1;
	uint8_t nd_opt_pvdidi_reserved2;
	uint32_t nd_opt_pvdidi_lifetime;
	unsigned char nd_opt_pvdidi_suffix[];
};

/* This assumes that str is not null and str_size > 0 */
static void addrtostr(struct in6_addr const *addr, char *str, size_t str_size)
{
	const char *res;

	res = inet_ntop(AF_INET6, (void const *)addr, str, str_size);

	if (res == NULL) {
		_DLOG(LOG_ERR, "addrtostr: inet_ntop: %s\n", strerror(errno));
		strncpy(str, "[invalid address]", str_size);
		str[str_size - 1] = '\0';
	}
}

/*
 * process_ra : we are mostly interested in gathering PVDID related information that
 * might be of interest for clients. We want to assign the whole RA to any PVDID
 * if such PVDID option is found in the RA. Othewise, the RA will be an PVDID orphan !
 *
 * We must take care of RA with nd_ra_router_lifetime == 0 (RA is becoming invalid)
 */
void process_ra(unsigned char *msg,
		int len,
		struct sockaddr_in6 *addr,
		struct in6_addr *sin6_addr,
		char *if_name)
{
	int i;
	char addr_str[INET6_ADDRSTRLEN];
	char pvdId[PVDIDNAMESIZ];
	int pvdIdSeq = -1;
	int pvdIdH = 0;
	int pvdIdL = 0;
	uint32_t pvdIdLifetime = 0;
	t_PvdId *PtPvdId;
	char *TabDNSSL[16];	// More than sufficient ?
	int nDNSSL = 0;
	char *TabRDNSS[8];	// No more than 3 anyway
	int nRDNSS = 0;
	struct {
		char *prefix;	// strduped
		int prefixLen;
	}	TabPrefix[32];
	int nPrefix = 0;

	addrtostr(
		addr != NULL ? &addr->sin6_addr : sin6_addr,
		addr_str, sizeof(addr_str));

	pvdId[0] = '\0';

	// The message begins with a struct nd_router_advert structure
	struct nd_router_advert *radvert = (struct nd_router_advert *)msg;

	len -= sizeof(struct nd_router_advert);

	if (len == 0)
		return;

	uint8_t *opt_str = (uint8_t *)(msg + sizeof(struct nd_router_advert));

	while (len > 0) {
		if (len < 2) {
			_DLOG(LOG_ERR, "trailing garbage in RA from %s\n", addr_str);
			break;
		}

		int optlen = (opt_str[1] << 3);

		// DLOG("Option len = %d, total len = %d\n", optlen, len);

		if (optlen == 0) {
			_DLOG(LOG_ERR, "zero length option in RA from %s\n", addr_str);
			break;
		} else if (optlen > len) {
			_DLOG(LOG_ERR, "option length (%d) greater than total"
				      " length (%d) in RA from %s\n",
			     optlen, len, addr_str);
			break;
		}

		switch (*opt_str) {
		case ND_OPT_MTU: {
			struct nd_opt_mtu *mtu = (struct nd_opt_mtu *)opt_str;
			if (len < sizeof(*mtu))
				return;

			DLOG("ND_OPT_MTU present in RA (%d)\n", ntohl(mtu->nd_opt_mtu_mtu));

			break;
		}
		case ND_OPT_PREFIX_INFORMATION: {
			char prefix_str[INET6_ADDRSTRLEN];
			struct nd_opt_prefix_info *pinfo = (struct nd_opt_prefix_info *)opt_str;
			if (len < sizeof(*pinfo))
				return;

			addrtostr(&pinfo->nd_opt_pi_prefix, prefix_str, sizeof(prefix_str));

			if (nPrefix < DIM(TabPrefix)) {
				TabPrefix[nPrefix].prefix = strdup(prefix_str);
				TabPrefix[nPrefix].prefixLen = pinfo->nd_opt_pi_prefix_len;

				nPrefix++;
			}

			break;
		}
		case ND_OPT_ROUTE_INFORMATION:
			/* not checked: these will very likely vary a lot */
			DLOG("ND_OPT_ROUTE_INFORMATION present in RA\n");
			break;
		case ND_OPT_SOURCE_LINKADDR:
			/* not checked */
			DLOG("ND_OPT_SOURCE_LINKADDR present in RA\n");
			break;
		case ND_OPT_TARGET_LINKADDR:
		case ND_OPT_REDIRECTED_HEADER:
			_DLOG(LOG_ERR, "invalid option %d in RA from %s\n", (int)*opt_str, addr_str);
			break;
		/* Mobile IPv6 extensions */
		case ND_OPT_RTR_ADV_INTERVAL:
		case ND_OPT_HOME_AGENT_INFO:
			/* not checked */
			DLOG("ND_OPT_RTR_ADV_INTERVAL/ND_OPT_HOME_AGENT_INFO present in RA\n");
			break;
		case ND_OPT_RDNSS_INFORMATION: {
			struct nd_opt_rdnss_info_local *rdnssinfo = (struct nd_opt_rdnss_info_local *)opt_str;
			if (len < sizeof(*rdnssinfo))
				return;

			DLOG("ND_OPT_RDNSS_INFORMATION present in RA\n");

			char rdnss_str[INET6_ADDRSTRLEN];
			int count = rdnssinfo->nd_opt_rdnssi_len;
			switch (count) {
			case 7 :
				addrtostr(&rdnssinfo->nd_opt_rdnssi_addr3, rdnss_str, sizeof(rdnss_str));
				TabRDNSS[nRDNSS++] = strdup(rdnss_str);
				/* FALLTHROUGH */
			case 5 :
				addrtostr(&rdnssinfo->nd_opt_rdnssi_addr2, rdnss_str, sizeof(rdnss_str));
				TabRDNSS[nRDNSS++] = strdup(rdnss_str);
				/* FALLTHROUGH */
			case 3 :
				addrtostr(&rdnssinfo->nd_opt_rdnssi_addr1, rdnss_str, sizeof(rdnss_str));
				TabRDNSS[nRDNSS++] = strdup(rdnss_str);
				break;
			}
			break;
		}
		case ND_OPT_DNSSL_INFORMATION: {
			int offset;
			struct nd_opt_dnssl_info_local *dnssl_info = (struct nd_opt_dnssl_info_local *)opt_str;
			char suffix[256] = {""};

			if (len < sizeof(*dnssl_info))
				return;

			for (offset = 0; offset < (dnssl_info->nd_opt_dnssli_len - 1) * 8;) {
				int label_len = dnssl_info->nd_opt_dnssli_suffixes[offset++];

				if (label_len == 0) {
					/*
					 * Ignore empty suffixes. They're
					 * probably just padding...
					 */
					if (suffix[0] == '\0')
						continue;

					if (nDNSSL < DIM(TabDNSSL)) {
						TabDNSSL[nDNSSL++] = strdup(suffix);
					}

					suffix[0] = '\0';
					continue;
				}

				if ((sizeof(suffix) - strlen(suffix)) < (label_len + 2)) {
					DLOG("oversized suffix in DNSSL option from %s\n", addr_str);
					break;
				}

				if (suffix[0] != '\0')
					strcat(suffix, ".");
				strncat(suffix, (char *)&dnssl_info->nd_opt_dnssli_suffixes[offset], label_len);
				offset += label_len;
			}

			DLOG("ND_OPT_DNSSL_INFORMATION : %d DNSSL items (max %d)\n", nDNSSL, (int) DIM(TabDNSSL));
			break;
		}
		case ND_OPT_PVDID: {
			struct nd_opt_pvdid *pvd = (struct nd_opt_pvdid *) opt_str;
			if (len < sizeof(*pvd))
				return;
			DLOG("ND_OPT_PVDID present in RA\n");

			if (pvdId[0] != '\0') {
				DLOG("PVDID option already defined. Ignoring this one\n");
				break;
			}

			pvdIdSeq = pvd->nd_opt_pvdid_seq;
			pvdIdH = pvd->nd_opt_pvdid_h;
			pvdIdL = pvd->nd_opt_pvdid_l;
			pvdIdLifetime = ntohl(pvd->nd_opt_pvdid_lifetime);

			// We will modify in place the buffer to put '.' where
			// needed
			unsigned char *pt = pvd->nd_opt_pvdid_name;
			int labelLen = *pt++;

			if (labelLen == 0) {
				// Ignore empty name
				break;
			}
			while (labelLen != 0) {
				int n;
				if ((n = pt[labelLen]) != 0) {
					pt[labelLen] = '.';
					pt += labelLen + 1;
				}
				labelLen = n;
			}
			// Hopefully ends with a '\0'

			strcpy(pvdId, (char *) &pvd->nd_opt_pvdid_name[1]);

			break;
		}
		default:
			_DLOG(LOG_DEBUG, "unknown option %d in RA from %s\n", (int)*opt_str, addr_str);
			break;
		}

		len -= optlen;
		opt_str += optlen;
	}
	_DLOG(LOG_DEBUG, "processed RA\n");

	// If we have seen a PvdId, we will update some fields of interest
	// However, if the RA is becoming invalid, we must notify that the PVD
	// has disappeared !
	if (pvdId[0] == '\0') {
		// No PvD option defined in this RA
		goto Exit;
	}

	if (radvert->nd_ra_router_lifetime == 0) {
		DLOG("RA becoming invalidated. Unregistering\n");
		UnregisterPvdId(pvdId);
		goto Exit;	// Release allocated structures
	}

	if ((PtPvdId = PvdIdBeginTransaction(pvdId)) == NULL) {
		return;
	}

	PvdIdSetAttr(PtPvdId, "sequenceNumber", GetIntStr(pvdIdSeq));
	PvdIdSetAttr(PtPvdId, "hFlag", GetIntStr(pvdIdH));
	PvdIdSetAttr(PtPvdId, "lFlag", GetIntStr(pvdIdL));
	PvdIdSetAttr(PtPvdId, "lifetime", GetIntStr(pvdIdLifetime));
	PvdIdSetAttr(PtPvdId, "interface", Stringify(JsonString(if_name)));
	PvdIdSetAttr(PtPvdId, "srcAddress", Stringify(addr_str));

	if (nDNSSL > 0) {
		char	*pt = JsonArray(nDNSSL, TabDNSSL);

		if (pt != NULL) {
			PvdIdSetAttr(PtPvdId, "DNSSL", pt);
			free(pt);
		}
	}

	if (nRDNSS > 0) {
		char	*pt = JsonArray(nRDNSS, TabRDNSS);

		if (pt != NULL) {
			PvdIdSetAttr(PtPvdId, "RDNSS", pt);
			free(pt);
		}
	}

	if (nPrefix > 0) {
		t_StringBuffer	SB;

		SBInit(&SB);
		SBAddString(&SB,  "{\n");
		for (i = 0; i < nPrefix; i++) {
			SBAddString(
				&SB,
				"\t\"%s/%d\" : { ",
				TabPrefix[i].prefix,
				TabPrefix[i].prefixLen);
			SBAddString(
				&SB,
				"\"prefix\" : \"%s\", ",
				TabPrefix[i].prefix);
			SBAddString(
				&SB,
				"\"prefixLen\" : \"%d\" }%s\n",
				TabPrefix[i].prefixLen,
				i == nPrefix - 1 ? "" : ",");
		}
		SBAddString(&SB, "}\n");
		PvdIdSetAttr(PtPvdId, "prefixes", SB.String);
		SBUninit(&SB);
	}

	PvdIdEndTransaction(PtPvdId);

Exit :
	for (i = 0; i < nDNSSL; i++) {
		if (TabDNSSL[i] != NULL) {
			free(TabDNSSL[i]);
		}
	}

	for (i = 0; i < nRDNSS; i++) {
		if (TabRDNSS[i] != NULL) {
			free(TabRDNSS[i]);
		}
	}

	for (i = 0; i < nPrefix; i++) {
		free(TabPrefix[i].prefix);
	}

	return;
}

static void process(
		int sock,
		unsigned char *msg, int len,
		struct sockaddr_in6 *addr,
		struct in6_pktinfo *pkt_info)
{
	char if_namebuf[IF_NAMESIZE] = {""};
	char *if_name = if_indextoname(pkt_info->ipi6_ifindex, if_namebuf);
	char addr_str[INET6_ADDRSTRLEN];

	if (!if_name) {
		if_name = "unknown interface";
	}

	addrtostr(&addr->sin6_addr, addr_str, sizeof(addr_str));

	_DLOG(LOG_DEBUG, "%s received a packet on lla %s\n", if_name, addr_str);

	if (len < sizeof(struct icmp6_hdr)) {
		_DLOG(LOG_WARNING, "%s received icmpv6 packet with invalid length (%d) from %s\n",
				if_name, len, addr_str);
		return;
	}

	struct icmp6_hdr *icmph = (struct icmp6_hdr *)msg;

	if (icmph->icmp6_type != ND_ROUTER_ADVERT) {
		_DLOG(LOG_ERR, "%s icmpv6 filter failed (RA expected)\n", if_name);
		return;
	}

	if (icmph->icmp6_code != 0) {
		_DLOG(LOG_WARNING, "%s received icmpv6 RS/RA packet with invalid code (%d) from %s\n",
				if_name, icmph->icmp6_code, addr_str);
		return;
	}

	if (len < sizeof(struct nd_router_advert)) {
		_DLOG(LOG_WARNING, "%s received icmpv6 RA packet with invalid length (%d) from %s\n",
				if_name, len, addr_str);
		return;
	}

	if (!IN6_IS_ADDR_LINKLOCAL(&addr->sin6_addr)) {
		_DLOG(LOG_WARNING, "%s received icmpv6 RA packet with non-linklocal source address from %s\n",
				if_name, addr_str);
		return;
	}

	process_ra(msg, len, addr, NULL, if_name);
}

static int recv_ra(
		int sock,
		unsigned char *msg,
		struct sockaddr_in6 *addr,
		struct in6_pktinfo **pkt_info,
		unsigned char *chdr)
{
	struct iovec iov;
	iov.iov_len = MSG_SIZE_RECV;
	iov.iov_base = (caddr_t)msg;

	struct msghdr mhdr;
	memset(&mhdr, 0, sizeof(mhdr));
	mhdr.msg_name = (caddr_t)addr;
	mhdr.msg_namelen = sizeof(*addr);
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 1;
	mhdr.msg_control = (void *)chdr;
	mhdr.msg_controllen = CMSG_SPACE(sizeof(struct in6_pktinfo)) + CMSG_SPACE(sizeof(int));

	int len = recvmsg(sock, &mhdr, 0);
	struct cmsghdr *cmsg;

	if (len < 0) {
		if (errno != EINTR) {
			_DLOG(LOG_ERR, "recvmsg: %s\n", strerror(errno));
		}
		return len;
	}

	for (cmsg = CMSG_FIRSTHDR(&mhdr); cmsg != NULL; cmsg = CMSG_NXTHDR(&mhdr, cmsg)) {
		if (cmsg->cmsg_level != IPPROTO_IPV6)
			continue;

		switch (cmsg->cmsg_type) {
		case IPV6_PKTINFO:
			if ((cmsg->cmsg_len == CMSG_LEN(sizeof(struct in6_pktinfo))) &&
			    ((struct in6_pktinfo *)CMSG_DATA(cmsg))->ipi6_ifindex) {
				*pkt_info = (struct in6_pktinfo *)CMSG_DATA(cmsg);
			} else {
				_DLOG(LOG_ERR, "received a bogus IPV6_PKTINFO from the kernel! len=%d, index=%d\n",
				     (int)cmsg->cmsg_len, ((struct in6_pktinfo *)CMSG_DATA(cmsg))->ipi6_ifindex);
				return -1;
			}
			break;
		}
	}

	char if_namebuf[IF_NAMESIZE] = {""};
	char *if_name = 0;
	if (pkt_info && *pkt_info) {
		if_name = if_indextoname((*pkt_info)->ipi6_ifindex, if_namebuf);
	}
	if (!if_name) {
		if_name = "unknown interface";
	}
	_DLOG(LOG_DEBUG, "%s recvmsg len=%d\n", if_name, len);

	return len;
}

int	HandleNetlink(int sockIcmpv6)
{
	int     len;
	struct sockaddr_in6 rcv_addr;
	struct in6_pktinfo *pkt_info = NULL;
	unsigned char msg[MSG_SIZE_RECV];
	unsigned char chdr[CMSG_SPACE(sizeof(struct in6_pktinfo)) +
			   CMSG_SPACE(sizeof(int))];

	DLOG("RA received\n");

	len = recv_ra(sockIcmpv6, msg, &rcv_addr, &pkt_info, chdr);

	if (len > 0 && pkt_info)
	{
		process(sockIcmpv6, msg, len, &rcv_addr, pkt_info);
	}
	else if (!pkt_info)
	{
		_DLOG(LOG_INFO, "recv_ra returned null pkt_info\n");
	}
	else if (len <= 0)
	{
		_DLOG(LOG_INFO, "recv_ra returned len <= 0: %d\n", len);
	}
	return(0);
}

int open_icmpv6_socket(void)
{
	int     sock;
	struct icmp6_filter filter;
	int     One = 1;

	if ((sock = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6)) < 0) {
		_DLOG(LOG_ERR, "can't create socket(AF_INET6): %s\n", strerror(errno));
		return (-1);
	}

	if (setsockopt(sock, IPPROTO_IPV6, IPV6_RECVPKTINFO, &One, sizeof(One)) < 0) {
		_DLOG(LOG_ERR, "setsockopt(IPV6_RECVPKTINFO): %s\n", strerror(errno));
		return (-1);
	}

	/* 
	 * setup ICMP filter
	 */
	ICMP6_FILTER_SETBLOCKALL(&filter);
	ICMP6_FILTER_SETPASS(ND_ROUTER_ADVERT, &filter);

	if (setsockopt(sock, IPPROTO_ICMPV6, ICMP6_FILTER, &filter, sizeof(filter)) < 0) {
		_DLOG(LOG_ERR, "setsockopt(ICMPV6_FILTER): %s\n", strerror(errno));
		return (-1);
	}

	return (sock);
}

/* ex: set ts=8 noexpandtab wrap: */
