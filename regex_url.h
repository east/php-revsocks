#ifndef REGEX_URL_H
#define REGEX_URL_H

/*
	this function splits an url into its parts.

	buffer min sizes for preventing overflows:

	protocol: 8
	host: 128
	uri: 256
*/

int parse_url(const char *url, char *protocol, char *host, int *port, char *uri);

#endif /* REGEX_URL_H */

