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
/*
 * Some general purpose definitions, shared between the daemon
 * and the client library
 */

#ifndef	PVD_DEFS_H
#define	PVD_DEFS_H

#include "config.h"

#ifdef	HAS_PVDUSER
#include <linux/pvd-user.h>	/* for PVDNAMSIZ */
#else
#include "linux/pvd-user.h"	/* for PVDNAMSIZ */
#endif

#define	DEFAULT_PVDD_PORT	10101

#define	PVD_MAX_MSG_SIZE	2048

#endif	/* PVD_DEFS_H */

/* ex: set ts=8 noexpandtab wrap: */
