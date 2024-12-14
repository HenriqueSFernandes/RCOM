#include "../include/download.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string.h>

void print_url_info(UrlInfo *info) {
  printf("User: %s\n", info->user);
  printf("Password: %s\n", info->password);
  printf("Host: %s\n", info->host);
  printf("Port: %d\n", info->port);
  printf("Path: %s\n", info->path);
}

int parse_url(char *host, UrlInfo *info) {
  if (host == NULL || info == NULL) {
    perror("Invalid arguments when parsing the url.\n");
    return -1;
  }

  // Validate the prefix.
  const char *prefix = "ftp://";
  if (strncmp(host, prefix, strlen(prefix)) != 0) {
    fprintf(stderr, "URL does not start with 'ftp://'.\n");
    return -1;
  }
  const char *cursor = host + strlen(prefix);

  memset(info, 0, sizeof(UrlInfo));
  info->port = 21; // TODO: fetch the port from the url.

  // Get the username and, optionally, the password.
  const char *at = strchr(cursor, '@');
  if (at) {
    const char *colon = strchr(cursor, ':');
    if (colon && colon < at) {
      // User and password (<user>:<password>@<host>)
      strncpy(info->user, cursor, colon - cursor);
      strncpy(info->password, colon + 1, at - colon - 1);
    } else {
      // No password (<user>@<host>)
      strncpy(info->user, cursor, at - cursor);
    }
    cursor = at + 1; // Move cursor past '@'
  }

  // Get the host and, optionally, the port.
  const char *slash = strchr(cursor, '/');
  const char *colon = strchr(cursor, ':');
  if (colon && (!slash || colon < slash)) {
    // Host:Port
    strncpy(info->host, cursor, colon - cursor);
    info->port = atoi(colon + 1);
  } else {
    // Host (no port)
    if (slash) {
      strncpy(info->host, cursor, slash - cursor);
    } else {
      strcpy(info->host, cursor); // No path; host is the rest of the URL
    }
  }

  // Get the path if it exists.
  if (slash) {
    strcpy(info->path, slash);
  } else {
    strcpy(info->path, "/"); // If the path is empty, use the root directory.
  }

  return 0;
}

int get_ip(char *host) {

  struct hostent *h;
  if ((h = gethostbyname(host)) == NULL) {
    herror("gethostbyname()");
    return -1;
  }

  printf("Host name  : %s\n", h->h_name);
  printf("IP Address : %s\n", inet_ntoa(*((struct in_addr *)h->h_addr)));

  return 0;
}

int connect_to_socket(const char *ip, const int port, int *socket_fd) {

  if (ip == NULL || socket_fd == NULL) {
    return -1;
  }

  int sockfd;
  struct sockaddr_in server_addr;

  /*Server address handling*/
  bzero((char *)&server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr =
      inet_addr(ip); /*32 bit Internet address network byte ordered*/
  server_addr.sin_port =
      htons(port); /*Server TCP port must be network byte ordered */

  /*Open a TCP socket*/
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket()");
    return -1;
  }
  /*Connect to the server*/
  if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("connect()");
    return -1;
  }

  *socket_fd = sockfd;
  return 0;
}
