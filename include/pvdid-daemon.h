/*
 * INSERT PROPER HEADER HERE
 */

#ifndef	PVDID_DAEMON_H
#define	PVDID_DAEMON_H

struct t_PvdId;

typedef	struct t_PvdId t_PvdId;

extern t_PvdId	*PvdIdBeginTransaction(char *pvdId);
extern int	PvdIdSetAttr(t_PvdId *PtPvdId, char *Key, char *Value);
extern void	PvdIdEndTransaction(t_PvdId *PtPvdId);

#endif	/* PVDID_DAEMON_H */
