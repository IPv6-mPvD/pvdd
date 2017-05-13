/*
 * INSERT PROPER HEADER HERE
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "libpvdid.h"

static	void	GetDnssl(t_pvd_connection *conn, char *pvdId)
{
	int		i;
	t_pvdid_dnssl	dnssl;

	if (pvdid_get_dnssl_sync(conn, "pvd.cisco.com", &dnssl) == 0) {
		printf("DNSSL %s :", pvdId);
		for (i = 0; i < dnssl.nDnssl; i++) {
			printf(" %s", dnssl.Dnssl[i]);
		}
		printf("\n");
		pvdid_release_dnssl(&dnssl);
	}
}

int	main(int argc, char **argv)
{
	t_pvd_connection	*mainS;
	t_pvd_connection	*binaryS;
	char			*attributes;
	t_pvdid_list		pvdIdList;
	t_pvdid_rdnss		rdnss;
	struct timeval		tv;
	int			i;

	printf("===================================\n");
	printf("          1st step\n");
	printf("===================================\n");

	if ((mainS = pvdid_connect(-1)) == NULL) {
		fprintf(stderr, "Error connecting to pvdid-daemon\n");
		return(1);
	}

	if ((binaryS = pvdid_get_binary_socket(mainS)) == NULL) {
		fprintf(stderr, "Error creating binary socket to pvdid-daemon\n");
	}
	if (binaryS != NULL) {
		pvdid_disconnect(binaryS);
	}

	if (pvdid_get_attributes_sync(mainS, "pvd.cisco.com", &attributes) == 0) {
		printf("Attributes for pvd.cisco.com : %s\n", attributes);
		free(attributes);
	}
	else {
		printf("Error retrieving attributes for pvd.cisco.com\n");
	}

	if (pvdid_get_pvdid_list_sync(mainS, &pvdIdList) == 0) {
		printf("PvD list :");

		for (i = 0; i < pvdIdList.nPvdId; i++) {
			printf(" %s", pvdIdList.pvdIdList[i]);
			free(pvdIdList.pvdIdList[i]);
		}
		printf("\n");
	}

	if (pvdid_get_rdnss_sync(mainS, "pvd.cisco.com", &rdnss) == 0) {
		printf("RDNSS :");
		for (i = 0; i < rdnss.nRdnss; i++) {
			printf(" %s", rdnss.Rdnss[i]);
		}
		printf("\n");
		pvdid_release_rdnss(&rdnss);
	}

	GetDnssl(mainS, "pvd.cisco.com");

	printf("===================================\n");
	printf("          2nd step\n");
	printf("===================================\n");

	// Time to perform the loop-based same kind of scenario
	// Some other processes must run to trigger various
	// notifications. This may take some time (2 minutes
	// for example using the provided test 'infrastructure'
	pvdid_subscribe_notifications(mainS);
	pvdid_subscribe_pvdid_notifications(mainS, "*");
	pvdid_get_pvdid_list(mainS);
	pvdid_get_attributes(mainS, "*");

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

			if ((rc = pvdid_read_data(mainS)) != PVD_READ_OK) {
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
				rc = pvdid_get_message(mainS, &multiLines, &msg);

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
			pvdid_get_attributes(mainS, "localhost");
			pvdid_get_attribute(mainS, "pvd.orange.com", "pvdId");
			GetDnssl(mainS, "pvd.cisco.com");
			continue;
		}

		if (FD_ISSET(pvd_connection_fd(mainS), &fdsI)) {
			char	*msg;
			int	multiLines;

			if ((rc = pvdid_read_data(mainS)) != PVD_READ_OK) {
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
				rc = pvdid_get_message(mainS, &multiLines, &msg);

				if (rc != PVD_NO_MESSAGE_READ) {
					printf("================================\n");
					printf("%s", msg);
					printf("================================\n");

				}
			} while (rc == PVD_MORE_DATA_AVAILABLE);
		}
	}


	pvdid_disconnect(mainS);
	return(0);
}

/* ex: set ts=8 noexpandtab wrap: */
