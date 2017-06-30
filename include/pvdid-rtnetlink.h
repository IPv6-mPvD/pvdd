/*
 * INSERT PROPER HEADER HERE
 */

#ifndef	PVDID_RTNETLINK_H
#define	PVDID_RTNETLINK_H

typedef struct t_rtnetlink_cnx t_rtnetlink_cnx;

extern	void rtnetlink_disconnect(t_rtnetlink_cnx *cnx);
extern	t_rtnetlink_cnx *rtnetlink_connect(void);
extern	int rtnetlink_get_fd(t_rtnetlink_cnx *cnx);
extern	void *rtnetlink_recv(t_rtnetlink_cnx *cnx, int *type);

#endif	/* PVDID_RTNETLINK_H */

/* ex: set ts=8 noexpandtab wrap: */
