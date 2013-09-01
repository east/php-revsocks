#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include "regex_url.h"

static inline int
regex_num_matches(regmatch_t *matches, size_t size)
{
	int i, len;
	for (i = 0, len = 0; i < size; i++)
	{
		if (matches[i].rm_so != -1)
			len++;
	}

	return len;
}

int
parse_url(const char *url, char *protocol, char *host, int *port, char *uri)
{
	const int max_matches = 6;
	regex_t regex;
	int reti, ret_val;
	regmatch_t matches[max_matches];

	*port = -1;

	regcomp(&regex, "^([a-z]{1,7}):\\/\\/([a-zA-Z0-9.]{1,127}):?([0-9]{1,5})?(\\/.{0,254})$", REG_EXTENDED);	

	reti = regexec(&regex, url, max_matches, matches, 0);

	if (reti == 0)
	{
		int num_matches = regex_num_matches(matches, max_matches);

		if (num_matches != 4 && num_matches != 5)
		{
			ret_val = -1;
			goto _exit;
		}
	
		int i, cur;
		for (i = 0, cur = 0; i < max_matches; i++)
		{
			int start = matches[i].rm_so;
			int end = matches[i].rm_eo;

			if (start == -1)
				continue; /* no match */

			int len = end-start;

			if (cur == 1) /* protocol */
			{
				memcpy(protocol, url+start, len);
				protocol[len] = '\0';
			}
			else if(cur == 2) /* host */
			{
				memcpy(host, url+start, len);
				host[len] = '\0';
			}
			else if((cur == 3 && num_matches == 4) || cur == 4) /* uri */
			{
				/* uri */
				memcpy(uri, url+start, len);
				uri[len] = '\0';
			}
			else if(cur == 3) /* port */
			{
				char buf[16];

				memcpy(buf, url+start, len);
				buf[len] = '\0';
				*port = atoi(buf);
			}

			cur++;
		}

		ret_val = 0;
		goto _exit;
	}
	else if (reti == REG_NOMATCH)
		ret_val = -2;
	else
		ret_val = -3;

_exit:
	regfree(&regex);
	return ret_val;	
}

