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
#ifndef	PVDD_NETLINK_H
#define	PVDD_NETLINK_H

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

#endif	/* PVDD_NETLINK_H */

/* ex: set ts=8 noexpandtab wrap: */
