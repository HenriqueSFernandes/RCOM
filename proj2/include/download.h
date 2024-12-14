#ifndef DOWNLOAD_H
#define DOWNLOAD_H

typedef struct {
  char user[256];
  char password[256];
  char host[256];
  char ip[256];
  char path[1024];
  char filename[1024];
  int port;
} UrlInfo;

enum state {
  CODE,    // Receiving a code
  MESSAGE, // Receiving a message
  STOP
};

// Print the URL information.
void print_url_info(UrlInfo *info);

// Parse the URL and fill the UrlInfo struct.
int parse_url(char *host, UrlInfo *info);

// Get the IP address from the host.
int get_ip(char *host, char *ip);

// Connect to the socket.
int connect_to_socket(const char *ip, const int port, int *socket_fd);

// Establish the connection.
int establish_connection(const UrlInfo *info, int *socket_fd);

// Read the response from the server.
int read_response(const int socket_fd, char *response, int *response_code);

// Flush the socket.
int flush_socket(const int socket_fd);

// Send a message to the server.
int send_message(const int socket_fd, const char *message);
#endif
