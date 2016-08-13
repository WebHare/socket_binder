/*
MIT License

Copyright (c) 1999-2016 B-Lex IT B.V.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/* This is a simple tester application for socket_binder. Run it as a
   non-root user, connect to the root-bound port, and it should give you
   a short greeting message before closing the connection.
*/

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/un.h>

int connect_to_unix_socket (const char *path)
{
  struct sockaddr_un addr;
  int socketfd;

  if (strlen(path) >= sizeof(addr.sun_path)-1)
  {
    fprintf(stderr, "Socket pathname too long (max %d bytes)\n", (int)sizeof(addr.sun_path)-1);
    return -1;
  }

  strcpy(addr.sun_path, path);
  addr.sun_family = AF_UNIX;

  /* connect to the socket */
  socketfd = socket(PF_UNIX, SOCK_STREAM,0);
  if (socketfd == -1)
  {
    perror("Creating unix socket");
    return -1;
  }

  if (connect(socketfd, (struct sockaddr*)&addr, strlen(addr.sun_path)+sizeof(addr.sun_family)+1)==-1)
  {
    perror("Connecting to unix socket");
    return -1;
  }
  return socketfd;
}

int send_the_socket(int fd_to_send, int receiving_unix_socket, int family, const char *address, int port)
{
  struct msghdr msg = {0};
  struct cmsghdr *cmsg;
  struct iovec invec;
  char buf[CMSG_SPACE(sizeof (int))];
  char request[512];

  if(strlen(address)>255)
  {
    errno=EINVAL;
    return 0;
  }

  request[0]=1; //request a bind
  request[1]=family;
  request[2]=(unsigned char)(port>>8);
  request[3]=(unsigned char)(port&0xff);
  request[4]=strlen(address);
  strcpy(request+5,address);

  invec.iov_base = request;
  invec.iov_len = 5+strlen(address);
  msg.msg_iov = &invec;
  msg.msg_iovlen = 1;
  msg.msg_control = buf;
  msg.msg_controllen = CMSG_SPACE(sizeof (int));
  cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof (int));
  *(int*)CMSG_DATA(cmsg) = fd_to_send;
  msg.msg_controllen = cmsg->cmsg_len;

  if ((size_t)sendmsg(receiving_unix_socket, &msg, 0) != (size_t)invec.iov_len)
  {
    errno=EINVAL;
    return 0;
  }

  if(recv(receiving_unix_socket,&errno,sizeof(errno),0) != sizeof(errno))
    errno=EINVAL;

  return errno==0;
}

int main(int argc, char *argv[])
{
  int unixfd;
  int listenfd;
  int acceptedfd;
  int i=1;

  if (argc<5)
  {
    fprintf(stderr, "Syntax: socket_user <path to socket binder> <4|6> <ip address> <port>\n");
    return EXIT_FAILURE;
  }

  unixfd=connect_to_unix_socket(argv[1]);
  if(unixfd==-1)
    return EXIT_FAILURE;

  listenfd=socket(atol(argv[2]) == 6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
  if (listenfd == -1)
  {
    perror("Setting up TCP/IP socket");
    return EXIT_FAILURE;
  }
  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(int)) == -1)
  {
    perror("Enabling SO_REUSEADDR");
    return EXIT_FAILURE;
  }
#ifdef IPV6_V6ONLY
  i=1;
  if (atol(argv[2])==6 && setsockopt(listenfd, IPPROTO_IPV6, IPV6_V6ONLY, &i, sizeof(int)) == -1)
  {
    perror("Enabling IPV6_V6ONLY");
    return EXIT_FAILURE;
  }
#endif
  if(!send_the_socket(listenfd, unixfd, atol(argv[2]), argv[3], atol(argv[4])))
  {
    perror("send_the_socket");
    return EXIT_FAILURE;
  }
  close(unixfd);

  if (listen(listenfd,65535)==-1)
  {
    perror("Listening on TCP/IP socket");
    return EXIT_FAILURE;
  }

  /* we succesfully received a remote fd */
  puts("Got remote socket. Entering accept");
  acceptedfd = accept(listenfd, NULL, NULL);
  if (acceptedfd == -1)
  {
    perror("Accepting incoming connection on remote fd");
    return EXIT_FAILURE;
  }

  write(acceptedfd, "This is socket_user 2.0\n", 24);
  close(acceptedfd);
  return EXIT_SUCCESS;
}
