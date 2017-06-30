/*
 * INSERT PROPER HEADER HERE
 */

/*
 * Some general purpose definitions, shared between the daemon
 * and the client library
 */

#ifndef	PVDID_DEFS_H
#define	PVDID_DEFS_H

#include "config.h"

#ifdef	HAS_PVDUSER
#include <linux/pvd-user.h>	/* for PVDNAMSIZ */
#else
#include "linux/pvd-user.h"	/* for PVDNAMSIZ */
#endif

#define	DEFAULT_PVDID_PORT	10101

#define	PVDID_MAX_MSG_SIZE	2048

#endif	/* PVDID_DEFS_H */

/* ex: set ts=8 noexpandtab wrap: */
