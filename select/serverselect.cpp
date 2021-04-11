#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <vector>

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
  fd_set rfds;
  struct timeval tv;
  int listen_sock, conn_sock, maxfd;
  struct sockaddr_in addr, cli_addr;
  socklen_t cli_size;
  int conn_num = 0;  // 连接数量
  std::vector<int> conn_socks;
  std::vector<sockaddr_in> cli_addrs;
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

  // main loop
  for (;;) {
    FD_ZERO(&rfds);
    FD_SET(listen_sock, &rfds);  // 监控新的客户端连接
    maxfd = listen_sock;

    for (int sockfd : conn_socks) {
      if (sockfd != -1) {
        FD_SET(sockfd, &rfds);  // 监控已连接客户端套接字是否可读
        maxfd = sockfd > maxfd ? sockfd : maxfd;
      }
    }

    // Timeout for select().
    tv.tv_sec = 30;
    tv.tv_usec = 0;

    int ret = select(maxfd + 1, &rfds, NULL, NULL, &tv);
    if (ret == -1) {
      perror("select");
      break;
    } else if (ret == 0) {
      printf("No data within 30 seconds.\n");
      continue;
    }

    // 当监听到新的客户端连接时创建连接套接字
    if (FD_ISSET(listen_sock, &rfds)) {
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

      conn_socks.push_back(conn_sock);
      cli_addrs.push_back(cli_addr);
    }

    // 当已连接客户端套接字可读时读取数据
    for (int i = 0; i < conn_socks.size(); i++) {
      if (FD_ISSET(conn_socks[i], &rfds)) {
        ssize_t s;

        // 如果连接套接字是阻塞模式, 当数据读完后 read 会阻塞, 直到该连接上的客户端重新发送数据.
        // 结果就是程序不能继续处理新的客户端连接, 因为程序被阻塞在这里了.
        while ((s = read(conn_socks[i], buf, BUF_SIZE)) > 0) {
          printf("\nReceived data from client [%s]: %.*s", inet_ntoa(cli_addrs[i].sin_addr), (int)s, buf);
        }

        // Send data to client.
        sprintf(buf, "This is server.");
        if (write(conn_socks[i], buf, strlen(buf)) == -1) {
          // Connection has been closed by client.
          perror("write");
          close(conn_socks[i]);
          conn_socks[i] = -1;
        }
      }
    }
  }

  close(listen_sock);
  for (int sockfd : conn_socks) {
    close(sockfd);
  }
}