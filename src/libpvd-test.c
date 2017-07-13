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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "libpvd.h"

static	void	GetDnssl(t_pvd_connection *conn, char *pvdname)
{
	int		i;
	t_dnssl_list	dnssl;

	if (pvd_get_dnssl_sync(conn, "pvd.cisco.com", &dnssl) == 0) {
		printf("DNSSL %s :", pvdname);
		for (i = 0; i < dnssl.ndnssl; i++) {
			printf(" %s", dnssl.dnssl[i]);
		}
		printf("\n");
		pvd_release_dnssl(&dnssl);
	}
}

int	main(int argc, char **argv)
{
	t_pvd_connection	*mainS;
	t_pvd_connection	*binaryS;
	char			*attributes;
	t_pvd_list		pvdList;
	t_rdnss_list		rdnss;
	struct timeval		tv;
	int			i;

	printf("===================================\n");
	printf("          1st step\n");
	printf("===================================\n");

	if ((mainS = pvd_connect(-1)) == NULL) {
		fprintf(stderr, "Error connecting to pvdid-daemon\n");
		return(1);
	}

	if ((binaryS = pvd_get_binary_socket(mainS)) == NULL) {
		fprintf(stderr, "Error creating binary socket to pvdid-daemon\n");
	}
	if (binaryS != NULL) {
		pvd_disconnect(binaryS);
	}

	if (pvd_get_attributes_sync(mainS, "pvd.cisco.com", &attributes) == 0) {
		printf("Attributes for pvd.cisco.com : %s\n", attributes);
		free(attributes);
	}
	else {
		printf("Error retrieving attributes for pvd.cisco.com\n");
	}

	if (pvd_get_pvd_list_sync(mainS, &pvdList) == 0) {
		printf("PvD list :");

		for (i = 0; i < pvdList.npvd; i++) {
			printf(" %s", pvdList.pvdnames[i]);
			free(pvdList.pvdnames[i]);
		}
		printf("\n");
	}

	if (pvd_get_rdnss_sync(mainS, "pvd.cisco.com", &rdnss) == 0) {
		printf("RDNSS :");
		for (i = 0; i < rdnss.nrdnss; i++) {
			printf(" %s", rdnss.rdnss[i]);
		}
		printf("\n");
		pvd_release_rdnss(&rdnss);
	}

	GetDnssl(mainS, "pvd.cisco.com");

	printf("===================================\n");
	printf("          2nd step\n");
	printf("===================================\n");

	// Time to perform the loop-based same kind of scenario
	// Some other processes must run to trigger various
	// notifications. This may take some time (2 minutes
	// for example using the provided test 'infrastructure'
	pvd_subscribe_notifications(mainS);
	pvd_subscribe_pvd_notifications(mainS, "*");
	pvd_get_pvd_list(mainS);
	pvd_get_attributes(mainS, "*");

	tv.tv_sec = 180;	// Loop up to 3 minutes
	tv.tv_usec = 0;

	while (1) {
		fd_set	fdsI;
		int	rc;

		FD_ZERO(&fdsI);
		FD_SET(pvd_connection_fd(mainS), &fdsI);
		// Linux specific : tv is expected to be updated with the
		// remaining time
		if ((rc = select(pvd_connection_fd(mainS) + 1, &fdsI, NULL, NULL, &tv)) == -1) {
			perror("select");
			return(1);
		}

		if (rc == 0) {
			// Timeout => go to next step
			break;
		}


		if (FD_ISSET(pvd_connection_fd(mainS), &fdsI)) {
			char	*msg;
			int	multiLines;

			if ((rc = pvd_read_data(mainS)) != PVD_READ_OK) {
				// Connection with the daemon broken -> exit
				if (errno != 0) {
					perror("recv");
				}
				else {
					fprintf(stderr, "Lost connection with the daemon\n");
				}
				return(1);
			}

			do {
				rc = pvd_get_message(mainS, &multiLines, &msg);

				if (rc != PVD_NO_MESSAGE_READ) {
					printf("================================\n");
					printf("%s", msg);
					printf("================================\n");

				}
			} while (rc == PVD_MORE_DATA_AVAILABLE);
		}
	}

	printf("===================================\n");
	printf("          3rd step\n");
	printf("===================================\n");

	// Now, mixed calls of loop-based and synchronous calls
	// Every 18 seconds, we perform a synchronous call. Before
	// the call, we request some data asynchronously (the
	// DNS)
	for (i = 0; i < 10; i++) {
		fd_set	fdsI;
		int	rc;

		tv.tv_sec = 18;
		tv.tv_usec = 0;

		FD_ZERO(&fdsI);
		FD_SET(pvd_connection_fd(mainS), &fdsI);

		printf("Calling select\n");
		if ((rc = select(pvd_connection_fd(mainS) + 1, &fdsI, NULL, NULL, &tv)) == -1) {
			perror("select");
			return(1);
		}

		if (rc == 0) {
			pvd_get_attributes(mainS, "localhost");
			pvd_get_attribute(mainS, "pvd.orange.com", "name");
			GetDnssl(mainS, "pvd.cisco.com");
			continue;
		}

		if (FD_ISSET(pvd_connection_fd(mainS), &fdsI)) {
			char	*msg;
			int	multiLines;

			if ((rc = pvd_read_data(mainS)) != PVD_READ_OK) {
				// Connection with the daemon broken -> exit
				if (errno != 0) {
					perror("recv");

				}
				else {
					fprintf(stderr, "Lost connection with the daemon\n");
				}
				return(1);
			}

			do {
				rc = pvd_get_message(mainS, &multiLines, &msg);

				if (rc != PVD_NO_MESSAGE_READ) {
					printf("================================\n");
					printf("%s", msg);
					printf("================================\n");

				}
			} while (rc == PVD_MORE_DATA_AVAILABLE);
		}
	}


	pvd_disconnect(mainS);
	return(0);
}

/* ex: set ts=8 noexpandtab wrap: */
