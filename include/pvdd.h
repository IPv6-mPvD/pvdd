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
#ifndef	PVDD_H
#define	PVDD_H

struct t_Pvd;

typedef	struct t_Pvd t_Pvd;

extern t_Pvd	*PvdBeginTransaction(char *pvdname);
extern int	PvdSetAttr(t_Pvd *PtPvd, char *Key, char *Value);
extern int	UnregisterPvd(char *pvdname);
extern void	PvdEndTransaction(t_Pvd *PtPvd);

#endif	/* PVDD_H */

/* ex: set ts=8 noexpandtab wrap: */
