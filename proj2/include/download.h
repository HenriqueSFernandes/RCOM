#ifndef DOWNLOAD_H
#define DOWNLOAD_H

typedef struct {
  char user[256];
  char password[256];
  char host[256];
  char ip[256];
  int port;
  char path[1024];
  char filename[1024];
  char passive_ip[256];
  int passive_port;
} UrlInfo;

enum state {
  CODE,    // Receiving response code
  MESSAGE, // Receiving message (not the actual response)
	RESPONSE, // Receiving the response
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

// Send a message to the server.
int send_message(const int socket_fd, const char *message);

// Login function.
int login(const int socket_fd, const UrlInfo *info);

// Enter passive mode.
int enter_passive_mode(const int socket_fd, UrlInfo *info);

// Download the file.
int download_file(const int socket_fd1, const int socket_fd2,
                  const UrlInfo *info);

// Close the connection.
int close_connection(const int socket_fd);
#endif
