#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
  // Disable output buffering
  setbuf(stdout, NULL);

  int server_socket, client_addr_len;
  struct sockaddr_in client_addr;

  server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket == -1) {
    printf("Socket creation failed: %s...\n", strerror(errno));
    return 1;
  }
  // // Since the tester restarts your program quite often, setting REUSE_PORT
  // // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &reuse,
                 sizeof(reuse)) < 0) {
    printf("SO_REUSEPORT failed: %s \n", strerror(errno));
    return 1;
  }

  struct sockaddr_in server_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(4221),
      .sin_addr = {htonl(INADDR_ANY)},
  };

  if (bind(server_socket, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) != 0) {
    printf("Bind failed: %s \n", strerror(errno));
    return 1;
  }

  int connection_backlog = 5;
  if (listen(server_socket, connection_backlog) != 0) {
    printf("Listen failed: %s \n", strerror(errno));
    return 1;
  }

  printf("Waiting for a client to connect...\n");
  client_addr_len = sizeof(client_addr);

  int client_socket =
      accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
  printf("Client connected\n");

  char buffer[1024];

  read(client_socket, buffer, sizeof(buffer));
  printf("request data: %s", buffer);

  char *method = strtok(buffer, " ");
  char *path = strtok(NULL, " ");

  char ok[] = "HTTP/1.1 200 OK\r\n\r\n";
  char not_found[] = "HTTP/1.1 404 Not Found\r\n\r\n";

  if (strncmp(path, "/echo/", 6) == 0) {
    size_t content_length = strlen(path) - 6;
    char *content = path + 6;
    const char *format = "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/plain\r\n"
                         "Content-Length: %zu\r\n\r\n%s";
  } else if (strcmp(path, "/") == 0) {
    send(client_socket, ok, sizeof(ok), 0);
  } else {
    send(client_socket, not_found, sizeof(not_found), 0);
  }

  close(server_socket);

  return 0;
}
