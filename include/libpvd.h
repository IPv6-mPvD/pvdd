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
#ifndef	LIBPVD_H
#define	LIBPVD_H

#include <sys/socket.h>
#include <linux/pvd-user.h>

#ifndef	SO_BINDTOPVD
#define	SO_BINDTOPVD	55
#endif

#ifndef	SO_GETPVDLIST
#define	SO_GETPVDLIST		56
#endif

#ifndef	SO_GETPVDATTRIBUTES
#define	SO_GETPVDATTRIBUTES	57
#endif

#ifndef	SO_CREATEPVD
#define	SO_CREATEPVD	58
#endif

/*
 * Opaque structure carrying a daemon connection
 */
typedef	struct t_pvd_connection	t_pvd_connection;

typedef	struct
{
	int	npvd;
	char	*pvdnames[MAXPVD];
}	t_pvd_list;

typedef	struct {
	int	nrdnss;
	char	*rdnss[3];
}	t_rdnss_list;

typedef	struct {
	int	ndnssl;
	char	*dnssl[8];
}	t_dnssl_list;

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
 * on the socket returned by pvd_connect()
 */
extern t_pvd_connection	*pvd_connect(int Port);
extern void		pvd_disconnect(
				t_pvd_connection *conn);
extern t_pvd_connection *pvd_reconnect(
				t_pvd_connection *conn);
extern t_pvd_connection	*pvd_get_control_socket(
				t_pvd_connection *conn);
extern t_pvd_connection	*pvd_get_binary_socket(
				t_pvd_connection *conn);
extern int		pvd_get_pvd_list(
				t_pvd_connection *conn);
extern int		pvd_get_pvd_list_sync(
				t_pvd_connection *conn,
				t_pvd_list *pvdList);
extern int		pvd_get_attributes(
				t_pvd_connection *conn,
				char *pvdname);
extern int		pvd_get_attributes_sync(
				t_pvd_connection *conn,
				char *pvdname,
				char **attributes);
extern int		pvd_get_attribute(
				t_pvd_connection *conn,
				char *pvdname, 
				char *attrName);
extern int		pvd_get_attribute_sync(
				t_pvd_connection *conn,
				char *pvdname, 
				char *attrName, 
				char **attrValue);
extern int		pvd_subscribe_notifications(
				t_pvd_connection *conn);
extern int		pvd_unsubscribe_notifications(
				t_pvd_connection *conn);
extern int		pvd_subscribe_pvd_notifications(
				t_pvd_connection *conn,
				char *pvdname);
extern int		pvd_unsubscribe_pvd_notifications(
				t_pvd_connection *conn,
				char *pvdname);
extern int		pvd_get_rdnss(
				t_pvd_connection *conn,
				char *pvdname);
extern int		pvd_get_rdnss_sync(
				t_pvd_connection *conn,
				char *pvdname,
				t_rdnss_list *PtRdnss);
extern int		pvd_get_dnssl(
				t_pvd_connection *conn,
				char *pvdname);
extern int		pvd_get_dnssl_sync(
				t_pvd_connection *conn,
				char *pvdname, 
				t_dnssl_list *PtDnssl);

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
 * Helper functions (related to interaction with the pvdd daemon)
 */
extern int		pvd_parse_pvd_list(char *msg, t_pvd_list *pvdList);
extern int		pvd_parse_rdnss(char *msg, t_rdnss_list *PtRdnss);
extern int		pvd_parse_dnssl(char *msg, t_dnssl_list *PtDnssl);
extern void		pvd_release_rdnss(t_rdnss_list *PtRdnss);
extern void		pvd_release_dnssl(t_dnssl_list *PtDnssl);

extern	int		pvd_read_data(t_pvd_connection *conn);
extern	int		pvd_get_message(t_pvd_connection *conn, int *multiLines, char **msg);

/*
 * Encapsulation of setsockopt/getsockopt calls (direct kernel communication)
 */
extern	int	sock_bind_to_pvd(int s, char *pvdname);
extern	int	sock_bind_to_nopvd(int s);
extern	int	sock_inherit_bound_pvd(int s);
extern	int	sock_get_bound_pvd(int s, char *pvdname);
extern	int	sock_get_bound_pvd_relaxed(int s, char *pvdname);

extern	int	proc_bind_to_pvd(char *pvdname);
extern	int	proc_bind_to_nopvd(void);
extern	int	proc_inherit_bound_pvd(void);
extern	int	proc_get_bound_pvd(char *pvdname);

extern	int	thread_bind_to_pvd(char *pvdname);
extern	int	thread_bind_to_nopvd(void);
extern	int	thread_inherit_bound_pvd(void);
extern	int	thread_get_bound_pvd(char *pvdname);

extern	int	kernel_get_pvdlist(struct pvd_list *pvl);
extern	int	kernel_get_pvd_attributes(char *pvdname, struct net_pvd_attribute *attr);
extern	int	kernel_create_pvd(char *pvdname);
extern	int	kernel_update_pvd_attr(char *pvdname, char *attrName, char *attrValue);

#endif		/* LIBPVD_H */

/* ex: set ts=8 noexpandtab wrap: */
