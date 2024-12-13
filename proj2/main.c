#include "include/download.h"
#include <stdio.h>

int main(int argc, char *argv[]) {
  printf("%d\n", argc);
  if (argc != 2) {
    fprintf(stderr, "Usage: %s ftp://[<user>:<password>@]<host>/<url-path>\n",
            argv[0]);
    return -1;
  }

  char *host = argv[1];
  get_ip(host);

  return 0;
}
