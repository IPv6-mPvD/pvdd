/*
 * INSERT PROPER HEADER HERE
 */

/*
 * This provides access to PVD specific notifications via the
 * RTNETLINK mechanism of the kernel
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "config.h"

#ifdef	HAS_PVDUSER
#include <linux/rtnetlink.h>
#else
#include "linux/rtnetlink.h"	/* local copy of the expected header file */
#endif

#include "pvdid-rtnetlink.h"

#include <linux/netlink.h>

#ifndef SOL_NETLINK
#define SOL_NETLINK 270
#endif

struct t_rtnetlink_cnx {
	struct nlmsghdr	*nlh;
	int		fd;
};

void	rtnetlink_disconnect(t_rtnetlink_cnx *cnx)
{
	if (cnx != NULL) {
		if (cnx->fd != -1) {
			close(cnx->fd);
		}
		if (cnx->nlh != NULL) {
			free(cnx->nlh);
		}
		free(cnx);
	}
}

t_rtnetlink_cnx *rtnetlink_connect(void)
{
	t_rtnetlink_cnx	*cnx;
	struct sockaddr_nl src_addr;
	int groups[] = { RTNLGRP_PVD };
	int i;
	int max_payload = sizeof(struct pvdmsg);

	if (sizeof(struct rdnssmsg) > max_payload) {
		max_payload = sizeof(struct rdnssmsg);
	}

	if (sizeof(struct dnsslmsg) > max_payload) {
		max_payload = sizeof(struct dnsslmsg);
	}

	printf("Using max_payload = %d (pvdmsg %d, rdnssmsg %d, dnsslmsg %d)\n",
		max_payload,
		(int) sizeof(struct pvdmsg),
		(int) sizeof(struct rdnssmsg),
		(int) sizeof(struct dnsslmsg));

	if ((cnx = ((t_rtnetlink_cnx *) malloc(sizeof(t_rtnetlink_cnx)))) == NULL) {
		return(NULL);
	}
	cnx->nlh = NULL;
	cnx->fd = -1;

	if ((cnx->fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE)) < 0) {
		perror("socket(AF_NETLINK)");
		goto out;
	}

	fcntl(cnx->fd, F_SETFL, O_NONBLOCK);

	memset(&src_addr, 0, sizeof(src_addr));
	src_addr.nl_family = AF_NETLINK;
	src_addr.nl_pid = getpid(); /* self pid */

	if (bind(cnx->fd, (struct sockaddr *) &src_addr, sizeof(src_addr)) == -1) {
		perror("bind(AF_NETLINK)");
		goto out;
	}

	for (i = 0; i < (sizeof(groups) / sizeof(groups[0])); i++) {
		if (setsockopt(cnx->fd,
			       SOL_NETLINK,
			       NETLINK_ADD_MEMBERSHIP, 
			       &groups[i], sizeof(groups[i])) == -1) {
			perror("NETLINK_ADD_MEMBERSHIP");
			goto out;
		}
	}

	if ((cnx->nlh = (struct nlmsghdr *) malloc(NLMSG_SPACE(max_payload))) == NULL) {
		perror("malloc");
		goto out;
	}
	memset(cnx->nlh, 0, NLMSG_SPACE(max_payload));
	cnx->nlh->nlmsg_len = NLMSG_SPACE(max_payload);
	cnx->nlh->nlmsg_pid = getpid();
	cnx->nlh->nlmsg_flags = 0;

	return(cnx);

out :
	rtnetlink_disconnect(cnx);

	return(NULL);
}

int	rtnetlink_get_fd(t_rtnetlink_cnx *cnx)
{
	return(cnx->fd);
}

void *rtnetlink_recv(t_rtnetlink_cnx *cnx, int *type)
{
	struct iovec iov;
	struct msghdr msg;
	struct sockaddr_nl dst_addr;
	int n;

	memset(&msg, 0, sizeof(msg));

	iov.iov_base = (void *) cnx->nlh;
	iov.iov_len = cnx->nlh->nlmsg_len;
	msg.msg_name = (void *) &dst_addr;
	msg.msg_namelen = sizeof(dst_addr);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	/* Read message from kernel */
	if ((n = recvmsg(cnx->fd, &msg, 0)) == 0) {
		return(NULL);
	}

	if (n < 0) {
		perror("recvmsg");	/* TODO : only output in debug mode */
		return(NULL);
	}

	*type = cnx->nlh->nlmsg_type;

	return(NLMSG_DATA(cnx->nlh));
}

/* ex: set ts=8 noexpandtab wrap: */
