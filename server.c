#define _GNU_SOURCE // Required for CPU_SET macros
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sched.h> // For CPU affinity
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define PORT 8080
#define MAX_EVENTS 10000
#define BUFFER_SIZE 1024

// --- Helper Functions ---

void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    perror("fcntl F_GETFL");
    exit(EXIT_FAILURE);
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl F_SETFL");
    exit(EXIT_FAILURE);
  }
}

// --- Worker Logic (The Event Loop) ---

void start_worker(int worker_id) {
  int server_fd, epoll_fd;
  struct sockaddr_in address;
  struct epoll_event ev, events[MAX_EVENTS];

  // 1. Create Socket (Each worker creates its own socket!)
  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  // 2. Enable SO_REUSEPORT
  // This allows multiple processes to bind to the SAME port (8080).
  // The kernel will round-robin distribute new connections to these waiting
  // sockets.
  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt))) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  // 3. Bind and Listen
  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("Bind failed");
    exit(EXIT_FAILURE);
  }
  set_nonblocking(server_fd);
  if (listen(server_fd, SOMAXCONN) < 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  // 4. Epoll Setup
  epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    perror("epoll_create1");
    exit(EXIT_FAILURE);
  }

  ev.events = EPOLLIN | EPOLLET;
  ev.data.fd = server_fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
    perror("epoll_ctl: server_socket");
    exit(EXIT_FAILURE);
  }

  printf("[Worker %d] Listening on port %d\n", worker_id, PORT);

  // 5. Event Loop
  while (1) {
    int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

    for (int i = 0; i < nfds; ++i) {
      if (events[i].data.fd == server_fd) {
        // Accept loop
        while (1) {
          struct sockaddr_in client_addr;
          socklen_t client_len = sizeof(client_addr);
          int client_fd =
              accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

          if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
              break;
            else {
              perror("accept");
              break;
            }
          }

          set_nonblocking(client_fd);
          ev.events = EPOLLIN | EPOLLET;
          ev.data.fd = client_fd;
          epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
        }
      } else {
        // Handle Client Read/Write
        int client_fd = events[i].data.fd;
        char buffer[BUFFER_SIZE];

        // Read loop for Edge Triggering
        while (1) {
          ssize_t count = read(client_fd, buffer, sizeof(buffer));
          if (count == -1) {
            if (errno == EAGAIN)
              break;
            else {
              close(client_fd);
              break;
            }
          } else if (count == 0) {
            close(client_fd);
            break;
          }

          // Basic Response
          char *response = "HTTP/1.1 200 OK\r\nContent-Length: "
                           "13\r\nConnection: keep-alive\r\n\r\nHello, World!";
          int result = write(client_fd, response, strlen(response));
          if (result)
            continue;
        }
      }
    }
  }
  close(server_fd);
}

// --- Main Process (Manager) ---

int main() {
  // 1. Detect Number of Cores
  long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
  printf("System has %ld cores. Spawning workers...\n", num_cores);

  // 2. Spawn Workers
  for (int i = 0; i < num_cores; i++) {
    pid_t pid = fork();

    if (pid < 0) {
      perror("fork failed");
      exit(EXIT_FAILURE);
    } else if (pid == 0) {
      // CHILD PROCESS

      // Optional: CPU Affinity (Pin process to core for max cache hits)
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(i, &cpuset);
      sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

      start_worker(i); // Enters infinite loop
      exit(0);
    }
  }

  // 3. Parent Waits (Master Process)
  // In a real server, you would handle signals (SIGINT, SIGTERM) here
  // to kill/restart workers.
  while (wait(NULL) > 0)
    ;

  return 0;
}
