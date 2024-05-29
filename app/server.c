#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

char *directory = NULL;

void *http_handler(void *args);

int main(int argc, char **argv) {

  // Disable output buffering
  setbuf(stdout, NULL);

  if (argc >= 2 &&
      strncmp(argv[1], "--directory", strlen("--directory")) == 0) {
    directory = argv[2];
  }

  int server_socket, client_addr_len;
  struct sockaddr_in client_addr;
  pthread_t tid;

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

  int connection_backlog = 10;
  if (listen(server_socket, connection_backlog) != 0) {
    printf("Listen failed: %s \n", strerror(errno));
    return 1;
  }
  printf("Waiting for a client to connect...\n");

  while (1) {
    client_addr_len = sizeof(client_addr);
    intptr_t client_socket =
        accept(server_socket, (struct sockaddr *)&client_addr,
               (unsigned int *)&client_addr_len);
    if (client_socket < 0) {
      break;
    }
    pthread_create(&tid, NULL, http_handler, (void *)client_socket);
    printf("Client connected\n");
  }

  close(server_socket);

  return 0;
}

void *http_handler(void *args) {
  intptr_t client_socket = (intptr_t)args;
  char buffer[1024];

  ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
  buffer[bytes_read] = '\0';
  printf("request data: \n%s\r\n", buffer);

  char *method = strtok(buffer, " ");
  char *path = strtok(NULL, " ");
  char *request_line = strtok(NULL, "\r\n");

  char *host = NULL;
  char *accept = NULL;
  char *user_agent = NULL;
  char *request_body = NULL;
  char *accept_encoding = NULL;

  // Parse the headers
  char *header_line = strtok(NULL, "\r\n");
  while (header_line != NULL && strlen(header_line) > 0) {
    if (strncmp(header_line, "Host: ", 6) == 0) {
      host = header_line;
    } else if (strncmp(header_line, "Accept: ", 8) == 0) {
      accept = header_line;
    } else if (strncmp(header_line, "User-Agent: ", 12) == 0) {
      user_agent = header_line;
    } else if (strncmp(header_line, "Accept-Encoding: ", 17) == 0) {
      accept_encoding = header_line;
    } else {
      request_body = header_line;
    }
    header_line = strtok(NULL, "\r\n");
  }

  size_t content_length;
  char *content;
  char *format;

  char ok[] = "HTTP/1.1 200 OK\r\n\r\n";
  char not_found[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
  char response[1024];

  if (strncmp(path, "/files/", 7) == 0 && directory != NULL) {
    FILE *file;
    long file_size;
    char *filename = path + 7;
    char *full_path = malloc(strlen(directory) + strlen(filename) + 1);
    printf("%s\n", filename);

    strcpy(full_path, directory);
    strcat(full_path, filename);

    full_path[strlen(directory) + strlen(filename)] = '\0';
    if (strncmp(method, "POST", 4) == 0) {
      if ((file = fopen(full_path, "w"))) {
        fputs(request_body, file);
        printf("%s\n\r", request_body);

        fseek(file, 0, SEEK_END);
        file_size = ftell(file);

        fclose(file);
        sprintf(response,
                "HTTP/1.1 201 Created\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Content-Length: %ld\r\n\r\n%s",
                file_size, request_body);

        printf("response data: %s\n", response);
        send(client_socket, response, strlen(response), 0);
      } else {
        strcpy(response, "HTTP/1.1 404 Not Found\r\n\r\n");
        send(client_socket, response, strlen(response), 0);
      }
    }

    if ((file = fopen(full_path, "r"))) {
      fseek(file, 0, SEEK_END);
      file_size = ftell(file);
      fseek(file, 0, SEEK_SET);

      char *content = malloc(file_size + 1);
      fread(content, file_size, 1, file);
      fclose(file);

      content[file_size] = '\0';
      sprintf(response,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/octet-stream\r\n"
              "Content-Length: %ld\r\n\r\n%s",
              file_size, content);
      printf("response data: %s", response);
      send(client_socket, response, strlen(response), 0);
    } else {
      puts("couldn't open the file");
      strcpy(response, "HTTP/1.1 404 Not Found\r\n\r\n");
      send(client_socket, response, strlen(response), 0);
    }
  }

  if (strncmp(path, "/echo/", 6) == 0) {

    content_length = strlen(path) - 6;
    content = path + 6;

    if (accept_encoding != NULL) {
      printf("accept encoding: %s\r\n", accept_encoding);

      int gzip = 0;
      char *encoding[sizeof(accept_encoding)];
      int i = 0;
      char *token = strtok(accept_encoding, " ");

      while (token != NULL) {
        encoding[i] = token;
        printf("encoding[%d]: %s\n", i, encoding[i]);

        if (strncmp(encoding[i], "gzip,", 5) == 0 ||
            strncmp(encoding[i], "gzip", 4) == 0) {
          gzip = 1;
          break;
        }

        token = strtok(NULL, " ");
        i++;
      }

      if (gzip) {
        format = "HTTP/1.1 200 OK\r\n"
                 "Content-Encoding: gzip\r\n"
                 "Content-Type: text/plain\r\n"
                 "Content-Length: %zu\r\n\r\n%s";
      } else {
        format = "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/plain\r\n"
                 "Content-Length: %zu\r\n\r\n%s";
      }
    } else {
      format = "HTTP/1.1 200 OK\r\n"
               "Content-Type: text/plain\r\n"
               "Content-Length: %zu\r\n\r\n%s";
    }
    printf("format: %s\n", format);

    sprintf(response, format, content_length, content);
    printf("response data : \n%s", response);

    send(client_socket, response, strlen(response), 0);
  }

  if (strncmp(path, "/user-agent", 11) == 0) {

    content = user_agent + 12;
    content_length = strlen(content);

    format = "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/plain\r\n"
             "Content-Length: %zu\r\n\r\n%s";

    sprintf(response, format, content_length, content);
    printf("response data : \n%s", response);

    send(client_socket, response, strlen(response), 0);
  }
  if (strcmp(path, "/") == 0) {

    printf("200 OK\n");
    send(client_socket, ok, strlen(ok), 0);

  } else {

    printf("404 not found\n");
    send(client_socket, not_found, strlen(not_found), 0);
  }

  return NULL;
}
