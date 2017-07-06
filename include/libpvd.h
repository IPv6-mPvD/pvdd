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
#ifndef	LIBPVDID_H
#define	LIBPVDID_H

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

#ifndef	SO_GETRALIST
#define	SO_GETRALIST	58
#endif

#ifndef	SO_CREATEPVD
#define	SO_CREATEPVD	59
#endif

/*
 * Opaque structure carrying a daemon connection
 */
typedef	struct t_pvd_connection	t_pvd_connection;

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
 * Helper functions (related to interaction with the pvdd daemon)
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
extern	int	kernel_get_pvdlist(struct pvd_list *pvl);
extern	int	pvd_get_attributes(char *pvdname, struct net_pvd_attribute *attr);
extern	int	kernel_get_pvd_attributes(char *pvdname, struct net_pvd_attribute *attr);
extern	int	kernel_create_pvd(char *pvdname);
extern	int	kernel_update_pvd_attr(char *pvdname, char *attrName, char *attrValue);

	/*
	 * Cached RAs (API might be obsolated quickly, so don't rely on it)
	 */
extern	struct ra_list	*ralist_alloc(int max_ras);
extern	void	ralist_release(struct ra_list *ral);
extern	int	kernel_get_ralist(struct ra_list *ral);

#endif		/* LIBPVDID_H */

/* ex: set ts=8 noexpandtab wrap: */
