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
#ifndef	PVDD_RTNETLINK_H
#define	PVDD_RTNETLINK_H

typedef struct t_rtnetlink_cnx t_rtnetlink_cnx;

extern	void rtnetlink_disconnect(t_rtnetlink_cnx *cnx);
extern	t_rtnetlink_cnx *rtnetlink_connect(void);
extern	int rtnetlink_get_fd(t_rtnetlink_cnx *cnx);
extern	void *rtnetlink_recv(t_rtnetlink_cnx *cnx, int *type);

#endif	/* PVDD_RTNETLINK_H */

/* ex: set ts=8 noexpandtab wrap: */
