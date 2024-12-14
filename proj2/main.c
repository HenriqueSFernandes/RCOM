#include "include/download.h"
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s ftp://[<user>:<password>@]<host>/<url-path>\n",
            argv[0]);
    return -1;
  }

  char *host = argv[1];
  UrlInfo info;
  if (parse_url(host, &info) != 0) {
    perror("Error parsing the URL.\n");
    return -1;
  }

  print_url_info(&info);

  int socket1;

  if (establish_connection(&info, &socket1) != 0) {
    perror("Error establishing connection.\n");
    close_connection(socket1);
    return -1;
  }

  if (login(socket1, &info) != 0) {
    perror("Error logging in.\n");
    close_connection(socket1);
    return -1;
  }

  if (enter_passive_mode(socket1, &info) != 0) {
    perror("Error entering passive mode.\n");
    close_connection(socket1);
    return -1;
  }

  print_url_info(&info);

  return 0;
}
