/*
 * INSERT PROPER HEADER HERE
 */

#ifndef	PVDID_NETLINK_H
#define	PVDID_NETLINK_H

extern int open_icmpv6_socket(void);
extern int HandleNetlink(int sock);
extern void process_ra(
		unsigned char *msg,
		int len,
		struct sockaddr_in6 *addr,
		struct in6_addr *sin6_addr,
		char *if_name);
extern char *addrtostr(
		struct in6_addr const *addr, 
		char *str,
		size_t str_size);

#endif	/* PVDID_NETLINK_H */

/* ex: set ts=8 noexpandtab wrap: */
