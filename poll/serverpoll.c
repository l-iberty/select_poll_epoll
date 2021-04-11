#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUF_SIZE 512
#define LISTENQ 5
#define SERV_PORT 9999
#define MAX_EVENTS 10

int setnonblocking(int sock) {
  int opts;

  // 获取sock的文件状态标志
  opts = fcntl(sock, F_GETFL);
  if (opts == -1) {
    perror("fcntl(sock, F_GETFL)");
    return 1;
  }

  // 设置非阻塞I/O
  opts = opts | O_NONBLOCK;
  if (fcntl(sock, F_SETFL, opts) == -1) {
    perror("fcntl(sock,F_SETFL,opts)");
    return 1;
  }
  return 0;
}

void broken_pipe_handler(int sig) {}

int main() {
  struct pollfd *pfds = NULL;
  struct timeval tv;
  int listen_sock, conn_sock, nfds;
  struct sockaddr_in addr, cli_addr;
  socklen_t cli_size;
  int conn_num = 0;  // 连接数量
  struct sockaddr_in *cli_addrs = NULL;
  char buf[BUF_SIZE];

  struct sigaction sa;
  sa.sa_handler = broken_pipe_handler;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGPIPE, &sa, NULL);

  /* Code to set up listening socket, 'listen_sock',
     (socket(), bind(), listen()) omitted */

  printf("------ Server listening at %d ------\n", SERV_PORT);

  // 创建监听套接字
  if ((listen_sock = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    return EXIT_FAILURE;
  }

  // socket 地址结构
  addr.sin_family = AF_INET;
  addr.sin_port = htons(SERV_PORT);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  // 绑定监听套接字
  if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr))) {
    perror("bind");
    close(listen_sock);
    return EXIT_FAILURE;
  }

  // 监听
  if (listen(listen_sock, LISTENQ)) {
    perror("listen");
    close(listen_sock);
    return EXIT_FAILURE;
  }

  pfds = (struct pollfd *)calloc(1, sizeof(struct pollfd));
  if (pfds == NULL) {
    perror("calloc");
    return EXIT_FAILURE;
  }
  pfds[0].fd = listen_sock;
  pfds[0].events = POLLIN;
  nfds = 1;

  cli_addrs = (struct sockaddr_in *)calloc(1, sizeof(struct sockaddr_in));
  if (cli_addrs == NULL) {
    perror("calloc");
    return EXIT_FAILURE;
  }

  // main loop
  for (;;) {
    int ret = poll(pfds, nfds, -1);
    if (ret == -1) {
      perror("poll");
      break;
    }

    // 当监听到新的客户端连接时创建连接套接字
    if (pfds[0].revents & POLLIN) {
      cli_size = sizeof(cli_addr);
      conn_sock = accept(listen_sock, (struct sockaddr *)&cli_addr, &cli_size);
      if (conn_sock == -1) {
        perror("accept");
        break;
      }

      printf("Received a connection from %s\n", inet_ntoa(cli_addr.sin_addr));
      printf(">> conn_num = %d\n", ++conn_num);

      // 连接套接字设置为非阻塞模式
      setnonblocking(conn_sock);

      nfds++;
      pfds = (struct pollfd *)realloc(pfds, nfds * sizeof(struct pollfd));
      if (pfds == NULL) {
        perror("ralloc");
        break;
      }
      pfds[nfds - 1].fd = conn_sock;
      pfds[nfds - 1].events = POLLIN;

      cli_addrs[conn_num - 1] = cli_addr;
      cli_addrs = (struct sockaddr_in *)realloc(cli_addrs, (conn_num + 1) * sizeof(struct sockaddr_in));
      if (cli_addrs == NULL) {
        perror("ralloc");
        break;
      }
    }

    // 当已连接客户端套接字可读时读取数据
    for (int i = 1; i < nfds; i++) {
      if (pfds[i].revents & POLLIN) {
        ssize_t s;

        // 如果连接套接字是阻塞模式, 当数据读完后 read 会阻塞, 直到该连接上的客户端重新发送数据.
        // 结果就是程序不能继续处理新的客户端连接, 因为程序被阻塞在这里了.
        while ((s = read(pfds[i].fd, buf, BUF_SIZE)) > 0) {
          printf("\nReceived data from client [%s]: %.*s", inet_ntoa(cli_addrs[i - 1].sin_addr), (int)s, buf);
        }

        // Send data to client.
        sprintf(buf, "This is server.");
        if (write(pfds[i].fd, buf, strlen(buf)) == -1) {
          // Connection has been closed by client.
          perror("write");
          close(pfds[i].fd);
          pfds[i].fd = -1;
        }
      }
    }
  }

  if (pfds) {
    free(pfds);
  }
  if (cli_addrs) {
    free(cli_addrs);
  }

  for (int i = 0; i < nfds; i++) {
    close(pfds[i].fd);
  }
}