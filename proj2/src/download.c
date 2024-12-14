#include "../include/download.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string.h>

void print_url_info(UrlInfo *info) {
  printf("\n========== URL Information ==========\n");
  printf("User        : %s\n", strlen(info->user) ? info->user : "N/A");
  printf("Password    : %s\n", strlen(info->password) ? info->password : "N/A");
  printf("Host        : %s\n", strlen(info->host) ? info->host : "N/A");
  printf("IP          : %s\n", strlen(info->ip) ? info->ip : "N/A");
  printf("Port        : %d\n", info->port);
  printf("Path        : %s\n", strlen(info->path) ? info->path : "/");
  printf("Filename    : %s\n", strlen(info->filename) ? info->filename : "N/A");
  printf("Passive IP  : %s\n",
         strlen(info->passive_ip) ? info->passive_ip : "N/A");
  printf("Passive Port: %d\n", info->passive_port);
  printf("=====================================\n");
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
    info->port = 21;
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
    perror("No path found in the url.\n");
    return -1;
  }

  // Get the filename.
  const char *last_slash = strrchr(info->path, '/');
  if (last_slash) {
    if (*(last_slash + 1) == '\0') {
      perror("No filename found in the url.\n");
      return -1;
    }
    strcpy(info->filename, last_slash + 1);
  } else {
    perror("No filename found in the url.\n");
    return -1;
  }

  // Get the ip address.
  if (get_ip(info->host, info->ip) != 0) {
    return -1;
  }

  return 0;
}

int get_ip(char *host, char *ip) {

  struct hostent *h;
  if ((h = gethostbyname(host)) == NULL) {
    herror("gethostbyname()");
    return -1;
  }

  const char *resolved_ip = inet_ntoa(*((struct in_addr *)h->h_addr));
  if (resolved_ip == NULL) {
    perror("Failed to get the ip address.\n");
    return -1;
  }
  strcpy(ip, resolved_ip);
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

int establish_connection(const UrlInfo *info, int *socket_fd) {
  if (info == NULL || socket_fd == NULL) {
    return -1;
  }

  if (connect_to_socket(info->ip, info->port, socket_fd) != 0) {
    perror("Error connecting to the socket.\n");
    return -1;
  }

  // Read the response.
  char response[1024] = "";
  int response_code = 0;
  if (read_response(*socket_fd, response, &response_code) != 0) {
    return -1;
  }

  return 0;
}

int read_response(const int socket_fd, char *response, int *response_code) {
  if (response == NULL || response_code == NULL) {
    return -1;
  }

  enum state current_state = CODE;

  int message_index = 0;
  *response_code = 0;

  while (current_state != STOP) {
    char current_char = 0;
    if (read(socket_fd, &current_char, 1) < 0) {
      perror("Error reading from the socket.\n");
      return -1;
    }

    switch (current_state) {
    case CODE:
      if (current_char == '\n') {
        current_char = STOP;
      } else if (current_char == ' ' || current_char == '-') {
        current_state = MESSAGE;
      } else if (current_char >= '0' && current_char <= '9') {
        *response_code = *response_code * 10 + (current_char - '0');
      }
      break;

    case MESSAGE:
      // printf("%x, %c\n", current_char, current_char);
      if (current_char == '\n') {
        response[message_index] = '\0';
        current_state = STOP;
      } else {
        response[message_index++] = current_char;
      }
      break;

    case STOP:
      break;
    }
  }
  printf("\n======= Response Informations =======\n");
  printf("Response Code   : %d\n", *response_code);
  printf("Response Message: %s\n", response);
  printf("=====================================\n");
  flush_socket(socket_fd);

  return 0;
}

// Function definition taken from ChatGPT.
int flush_socket(const int socket_fd) {
  char buffer[1024];

  // Set socket to non-blocking mode
  int flags = fcntl(socket_fd, F_GETFL, 0);
  if (flags == -1) {
    perror("fcntl(F_GETFL)");
    return -1;
  }
  if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl(F_SETFL, O_NONBLOCK)");
    return -1;
  }

  // Read and discard data from the socket
  while (1) {
    ssize_t bytes_read = read(socket_fd, buffer, sizeof(buffer));

    if (bytes_read <= 0) {
      break;
    }
  }

  // Reset socket to blocking mode if necessary (optional)
  flags = fcntl(socket_fd, F_GETFL, 0);
  fcntl(socket_fd, F_SETFL, flags & ~O_NONBLOCK);

  return 0;
}

int send_message(const int socket_fd, const char *message) {
  if (message == NULL) {
    return -1;
  }
  printf("\n========== Sending Message ==========\n");
  printf("Message: %s", message);
  printf("=====================================\n");
  if (write(socket_fd, message, strlen(message)) < 0) {
    perror("Error writing to the socket.\n");
    return -1;
  }
  return 0;
}

int login(const int socket_fd, const UrlInfo *info) {

  if (info == NULL) {
    return -1;
  }

  // Send the username.
  if (send_message(socket_fd, "USER anonymous\r\n") != 0) {
    return -1;
  }

  // Read the response.
  char response[1024] = "";
  int response_code = 0;
  if (read_response(socket_fd, response, &response_code) != 0) {
    return -1;
  }

  if (response_code != 331) {
    perror("Error logging in.\n");
    return -1;
  }

  // Send the password if it exists.
  if (strlen(info->password) > 0) {
    if (send_message(socket_fd, "PASS anonymous\r\n") != 0) {
      return -1;
    }
    if (read_response(socket_fd, response, &response_code) != 0) {
      return -1;
    }
    if (response_code != 230) {
      perror("Error logging in.\n");
      return -1;
    }
  }

  printf("Logged in successfully.\n");

  return 0;
}

int enter_passive_mode(const int socket_fd, UrlInfo *info) {
  if (info == NULL) {
    return -1;
  }

  if (send_message(socket_fd, "PASV\r\n") != 0) {
    return -1;
  }

  // Read the response.
  char response[1024] = "";
  int response_code = 0;
  if (read_response(socket_fd, response, &response_code) != 0) {
    return -1;
  }

  if (response_code != 227) {
    perror("Error entering passive mode.\n");
    return -1;
  }

  // Parse the passive mode response.
  char *start = strchr(response, '(');
  char *end = strchr(response, ')');
  if (start == NULL || end == NULL) {
    perror("Error parsing the passive mode response.\n");
    return -1;
  }

  int ip1, ip2, ip3, ip4, port1, port2;
  sscanf(start, "(%d,%d,%d,%d,%d,%d)", &ip1, &ip2, &ip3, &ip4, &port1, &port2);
  sprintf(info->passive_ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
  info->passive_port = port1 * 256 + port2;

  return 0;
}

int close_connection(const int socket_fd) {
  if (close(socket_fd) < 0) {
    perror("Error closing the connection.\n");
    return -1;
  }
  return 0;
}
