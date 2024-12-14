#ifndef DOWNLOAD_H
#define DOWNLOAD_H

typedef struct {
  char user[256];
  char password[256];
  char host[256];
  char ip[256];
  char path[1024];
  int port;
} UrlInfo;

void print_url_info(UrlInfo *info);

int parse_url(char *host, UrlInfo *info);

int get_ip(char *host, char *ip);

int connect_to_socket(const char *ip, const int port, int *socket);
#endif
