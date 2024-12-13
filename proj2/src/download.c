#include "../include/download.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>

int get_ip(char *host) {

  struct hostent *h;
  if ((h = gethostbyname(host)) == NULL) {
    herror("gethostbyname()");
    return 1;
  }

  printf("Host name  : %s\n", h->h_name);
  printf("IP Address : %s\n", inet_ntoa(*((struct in_addr *)h->h_addr)));

  return 0;
}
