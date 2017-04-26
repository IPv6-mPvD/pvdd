
#ifndef	LIBPVDID_H
#define	LIBPVDID_H

#define	MAX_PVDID	1024	// TODO : have it dynamic instead

typedef	struct
{
	int	nPvdId;
	char	*pvdIdList[MAX_PVDID];
}	t_pvdid_list;

typedef	struct {
	int	nRdnss;
	char	*Rdnss[3];
}	t_pvdid_rdnss;

typedef	struct {
	int	nDnssl;
	char	*Dnssl[8];
}	t_pvdid_dnssl;

extern int	pvdid_connect(int Port);
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

#endif		/* LIBPVDID_H */
