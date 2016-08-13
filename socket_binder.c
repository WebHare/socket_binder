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

/* socket_binder sets up a unix domain socket, and allows any process able
   to connect to that port to request a listening and/or bound socket.
   effectively this allows such processes to use port numbers <1024.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <errno.h>
#include <arpa/inet.h>

int *currentsockets=NULL;
int numcurrentsockets=0;
int unix_listen_fd;
fd_set readfds;
int highestfd;

int connect_to_unix_socket (const char *path)
{
  struct sockaddr_un addr;
  int socketfd;

  if (strlen(path) >= sizeof(addr.sun_path)-1)
    return -1;

  strcpy(addr.sun_path, path);
  addr.sun_family = AF_UNIX;

  /* connect to the socket */
  socketfd = socket(PF_UNIX, SOCK_STREAM,0);
  if (socketfd == -1)
    return -1;

  if (connect(socketfd, (struct sockaddr*)&addr, strlen(addr.sun_path)+sizeof(addr.sun_family)+1)==-1)
  {
    close(socketfd);
    return -1;
  }

  return socketfd;
}

int setup_unix_socket(const char *socketpath)
{
  struct sockaddr_un addr;
  int socketfd;

  int testfd = connect_to_unix_socket(socketpath);

/* make sure we don't overwrite an existing socket by connecting to it */
  if(testfd != -1)
  {
    fprintf(stderr, "Socket is already being provided for file %s\n",socketpath);
    close(testfd);
    return -1;
  }

  socketfd = socket(PF_UNIX, SOCK_STREAM,0);
  if (socketfd == -1)
  {
    perror("Creating unix socket");
    return -1;
  }

  if (strlen(socketpath) >= sizeof(addr.sun_path)-1)
  {
    fprintf(stderr, "Socket pathname too long (max %ld bytes)\n", sizeof(addr.sun_path)-1);
    return -1;
  }

  unlink(socketpath);
  strcpy(addr.sun_path, socketpath);
  addr.sun_family = AF_UNIX;
  if (bind(socketfd, (struct sockaddr*)&addr, strlen(addr.sun_path)+sizeof(addr.sun_family)+1)==-1)
  {
    perror("Binding unix socket");
    return -1;
  }

  if (listen(socketfd,65535)==-1)
  {
    perror("Listening on unix socket");
    return -1;
  }
  return socketfd;
}

void setup_readfds()
{
  int i;

  highestfd = unix_listen_fd;
  FD_ZERO(&readfds);
  FD_SET(unix_listen_fd, &readfds);

  for(i=0;i<numcurrentsockets;++i)
  {
    if(highestfd < currentsockets[i])
      highestfd = currentsockets[i];

    FD_SET(currentsockets[i], &readfds);
  }
}

void accept_incoming()
{
  int connfd;
  struct sockaddr_un incomingaddr;
  socklen_t addrlen;

  addrlen = sizeof(incomingaddr);
  connfd = accept(unix_listen_fd, (struct sockaddr*) &incomingaddr, &addrlen);
  if (connfd == -1)
  {
    perror("Accept error on unix socket");
    return;
  }

  int *newsockets = realloc(currentsockets, sizeof(int)*numcurrentsockets+1);
  if(!newsockets)
  {
    perror("Out of memory accepting connection");
    close(connfd);
    return;
  }

  currentsockets = newsockets;
  currentsockets [numcurrentsockets++] = connfd;
}

int handle_read(int connfd)
{
  char inbuffer[512];
  int readbytes;
  int receivedfd=-1;
  struct msghdr hdr={0,0,0,0,0,0,0};
  struct iovec invec;
  char databuffer[sizeof(struct cmsghdr) + sizeof(int)];
  struct cmsghdr *cmsg;

  invec.iov_base = inbuffer;
  invec.iov_len = sizeof(inbuffer);
  hdr.msg_iov = &invec;
  hdr.msg_iovlen = 1;
  hdr.msg_control = databuffer;
  hdr.msg_controllen = sizeof(databuffer);

  readbytes = recvmsg(connfd, &hdr, 0);
  for (cmsg = CMSG_FIRSTHDR(&hdr); cmsg != NULL; cmsg = CMSG_NXTHDR(&hdr,cmsg))
  {
    if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS && cmsg->cmsg_len == CMSG_LEN(sizeof(int)))
    {
      receivedfd = *((int*)CMSG_DATA(cmsg));
    }
  }

  if(readbytes < 1)
  {
    perror("recvmsg failed");
    close(receivedfd);
    return 0;
  }

  if(inbuffer[0]==1)
  {
    struct sockaddr_storage ipaddr;
    memset(&ipaddr,0,sizeof(ipaddr));
    int port;
    socklen_t len;

    /* packet format: inbuffer[1] == family (4 or 6), inbuffer[2-3] = port, inbuffer[4]=length of readable ip address, inbuffer[5..]=ip address */
    if(readbytes<6 || readbytes<(5+inbuffer[4]) || (inbuffer[1]!=4 && inbuffer[1]!=6))
      return 0;

    port = (((unsigned char)inbuffer[2]) << 8) | ((unsigned char)inbuffer[3]);
    inbuffer[5+(unsigned)inbuffer[4]]=0; /* ensure zero termination */

    if(inbuffer[1] == 4) /* ipv4 */
    {
      struct sockaddr_in *ip4addr;

      ip4addr = (struct sockaddr_in *)&ipaddr;
      ip4addr->sin_family = AF_INET;
      ip4addr->sin_port = htons(port);
      len=sizeof(struct sockaddr_in);
      if(inet_pton(AF_INET, inbuffer+5, &ip4addr->sin_addr)<0)
      {
        close(receivedfd);
        return 0;
      }
    }
    else /* ipv6 */
    {
      struct sockaddr_in6 *ip6addr;

      ip6addr = (struct sockaddr_in6 *)&ipaddr;
      ip6addr->sin6_family = AF_INET6;
      ip6addr->sin6_port = htons(port);
      len=sizeof(struct sockaddr_in6);
      if(inet_pton(AF_INET6, inbuffer+5, &ip6addr->sin6_addr)<0)
      {
        close(receivedfd);
        return 0;
      }
    }

    if (bind(receivedfd, (struct sockaddr*)&ipaddr, len) != -1)
      errno=0;

    write(connfd, &errno, sizeof(errno));
    close(receivedfd);
  }

  close(receivedfd);
  return 0;
}

void run_socketbinder()
{
  while(1)
  {
    int i;

    setup_readfds();
    if(select(highestfd+1, &readfds, NULL, NULL, NULL) < 0)
    {
      if(errno!=EAGAIN && errno!=EINTR)
      {
        perror("select failed");
        exit(EXIT_FAILURE);
      }
      continue;
    }

    for(i=numcurrentsockets-1;i>=0;--i) /* walk backwards to allow deallocation */
      if(FD_ISSET(currentsockets[i], &readfds) && !handle_read(currentsockets[i]))
      {
        /* close and delete the socket from the list */
        close(currentsockets[i]);
        if(i != numcurrentsockets-1) /* not the last element, so move the remaining fds backwards */
          memmove(&currentsockets[i], &currentsockets[i+1], (numcurrentsockets-i-1) * sizeof(currentsockets[i]));
        --numcurrentsockets;
      }

    if(FD_ISSET(unix_listen_fd, &readfds))
      accept_incoming();
  }
}

int main(int argc, char *argv[])
{
  int showpid=0;
  int stayinforeground=0;

  for(;argc>=2;--argc,++argv)
  {
    if(strcmp(argv[1],"-p") == 0)
    {
      showpid = 1;
      continue;
    }
    if(strcmp(argv[1],"-f") == 0)
    {
      stayinforeground=1;
      continue;
    }
    break; //unrecognized parameter
  }

  if(argc<2 || argv[1][0]=='-') //no extra parameter? then we didn't get the connection port or were confused about the parameters
  {
    fprintf(stderr, "Syntax: socket_binder [-p] [-f] <commpipe> [mode] [username[.groupname]]\n");
    fprintf(stderr, " -p: print the socket_binder pid\n");
    fprintf(stderr, " -f: stay in the foreground\n");
    fprintf(stderr, " commpipe:  path of the communication pipe. any process which can access\n");
    fprintf(stderr, "            this file can request binds and sockets\n");
    fprintf(stderr, " mode:      mode for the communication pipe\n");
    fprintf(stderr, " username:  user which will become owner of the communication pipe\n");
    fprintf(stderr, " groupname: group which will become owner of the communication pipe\n");
    return EXIT_FAILURE;
  }

  unix_listen_fd = setup_unix_socket(argv[1]);
  if (unix_listen_fd == -1)
    return EXIT_FAILURE;

  if (argc>=4) //argv[3] = username
  {
    char username[64];
    char *dot;
    struct passwd *userinfo;
    gid_t groupid;

    if(strlen(argv[3])>63)
    {
      fprintf(stderr,"User name too long");
      return EXIT_FAILURE;
    }
    strcpy(username, argv[3]);
    dot = strchr(username,'.');
    if(dot) *dot=0;
    userinfo = getpwnam(username);
    if (userinfo == NULL)
    {
      fprintf(stderr,"Cannot find user '%s'\n", username);
      return EXIT_FAILURE;
    }

    groupid = userinfo->pw_gid;
    if(dot)
    {
      struct group *specifiedgroup = getgrnam(dot+1);
      if (specifiedgroup == NULL)
      {
        fprintf(stderr,"Cannot find group '%s'\n", dot+1);
        return EXIT_FAILURE;
      }
      groupid = specifiedgroup->gr_gid;
    }

    if(chown(argv[1], userinfo->pw_uid, groupid))
    {
      perror("fchown on commpipe failed");
      return EXIT_FAILURE;
    }
  }
  if (argc>=3) //argv[2] = mode
  {
    if(chmod(argv[1], strtoul(argv[2],NULL,8))!=0)
    {
      perror("fchmod on commpipe failed");
      return EXIT_FAILURE;
    }
  }

  if(!stayinforeground)
  {
    /* http://www.erlenstar.demon.co.uk/unix/faq_2.html#SEC16 */
    if (fork() != 0)
      _exit(0); /* we're the parent, so die! */
    setsid();
    if (fork() != 0)
      _exit(0); /* we're another parent, die as well.. */
  }

  if(showpid)
    printf("%u\n",getpid());

  if(!stayinforeground)
  {
    fflush(stdout);
    chdir("/");
    close(0);
    open("/dev/null",O_RDWR);
    dup2(0,1);
    dup2(0,2);
    /* end daemonize code */
  }

  run_socketbinder();
  return EXIT_SUCCESS;
}
