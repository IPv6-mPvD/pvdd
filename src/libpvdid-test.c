/*
 * INSERT PROPER HEADER HERE
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>

#include "libpvdid.h"

int	main(int argc, char **argv)
{
	int		mainS;
	int		binaryS;
	char		*attributes;
	t_pvdid_list	pvdIdList;
	t_pvdid_rdnss	rdnss;
	t_pvdid_dnssl	dnssl;

	if ((mainS = pvdid_connect(-1)) == -1) {
		fprintf(stderr, "Error connecting to pvdid-daemon\n");
		return(1);
	}

	if ((binaryS = pvdid_get_binary_socket(mainS)) == -1) {
		fprintf(stderr, "Error creating binary socket to pvdid-daemon\n");
	}
	if (binaryS != -1) {
		close(binaryS);
	}

	if (pvdid_get_attributes_sync(mainS, "pvd.cisco.com", &attributes) == 0) {
		printf("Attributes for pvd.cisco.com : %s\n", attributes);
		free(attributes);
	}
	else {
		printf("Error retrieving attributes for pvd.cisco.com\n");
	}

	if (pvdid_get_pvdid_list_sync(mainS, &pvdIdList) == 0) {
		int	i;

		printf("PvD list :");

		for (i = 0; i < pvdIdList.nPvdId; i++) {
			printf(" %s", pvdIdList.pvdIdList[i]);
			free(pvdIdList.pvdIdList[i]);
		}
		printf("\n");
	}

	if (pvdid_get_rdnss_sync(mainS, "pvd.cisco.com", &rdnss) == 0) {
		int	i;

		printf("RDNSS :");
		for (i = 0; i < rdnss.nRdnss; i++) {
			printf(" %s", rdnss.Rdnss[i]);
		}
		printf("\n");
		pvdid_release_rdnss(&rdnss);
	}

	if (pvdid_get_dnssl_sync(mainS, "pvd.cisco.com", &dnssl) == 0) {
		int	i;

		printf("DNSSL :");
		for (i = 0; i < dnssl.nDnssl; i++) {
			printf(" %s", dnssl.Dnssl[i]);
		}
		printf("\n");
		pvdid_release_dnssl(&dnssl);
	}

	close(mainS);
	return(0);
}

/* ex: set ts=8 noexpandtab wrap: */
