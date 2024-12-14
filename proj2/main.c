#include "include/download.h"
#include <stdio.h>

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
    return -1;
  }

  char response[1024] = "";
  int response_code = 0;
  read_response(socket1, response, &response_code);

  return 0;
}
