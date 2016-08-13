# Socket binder
Socket binder is a daemon to allow nonprivileged processes to bind to privileged ports

## Introduction
The socket binder helps non-root processes to bind to privileged ports (below 1024). It does this by working as a privileged
helper process that performs no other job than creating and binding sockets to the privileged ports and then passing this
socket over a unix file descriptor to the requiring process. This allows the source code of the binder to be much more easily
audited, and allows processes which require low numbered ports (such as webservers) to run without any root privileges at all,
even at startup.

To control access to the socket_binder, set up proper privileges on the unix socket it creates. The linux RPM will set up
a group 'sockbind', and any user which is a member of this group may use socket_binder to bind to any port < 1024

## Advantages
The socket_binder solution to binding below port 1024 has the following advantages:

- It's portable to other systems than Linux (it supports OSX and hopefully FreeBSD still too)

- It doesn't rely on filesystem suid bits or capabilities

- Processes using the socket binder can be started without using any wrapper like authbind

- Processes binaries can be easily replaced during development, no need to set uid/capabilities after every recompile

- Simple file permissions to manage access

- Processes can change to which ports/ip addresses they bind without restarting or without some way of retaining root privileges

- Obtaining a socket is simple, the code can be embedded directly into the server application

## Disadvantages

- Another package to install

- Uncommon approach, may confuse system administrators

- Requires modification of the server application

- No support for explicitly specifying which ports to bind - any process with access to the unix socket can bind any privileged port.

- No library available (we've never bothered to, we just copy the two functions, building and integrating yet another library never seemed to be worth the effort)

## Who uses this?
The WebHare webserver reads the ports/ip addresses to bind to from a database server, using a scripting language in a VM - way
past the point that we would have been comfortable retaining user privileges.


## Build/development/test
This test builds the socket_binder daemon and the socket_user example application. socket_user will be started as normal
user (assuming you're not running all steps below as root), but be able to bind to privileged port 888.

```
make socket_binder socket_user
sudo socket_binder /tmp/testsocket    # this immediately daemonizes and returns
sudo chown $USER /tmp/testsocket      # this will change ownership of the socket back to you
./socket_user /tmp/testsocket 4 127.0.0.1 888
# and now, on a seperate terminal, or `CTRL+Z` and `bg` it
telnet 127.0.0.1 888
# you'll see "This is socket_user 2.0"
```

## Using the socket_binder from a server application
- Copy connect_to_unix_socket and send_the_socket from socket_user.c to your application

- Do something like this, but add error checking

```
// Connect to the socket_binder
int unixfd = connect_to_unix_socket('/path/to/socket_binder/socket');

// Create the listening socket
int listenfd = socket(atol(argv[2]) == 6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);

// Bind to IPv4 port 80 on 127.0.0.1
send_the_socket(listenfd, unixfd, 4, "127.0.0.1", 80);

// Listen
listen(listenfd,16)

// And start accepting connection!
```

See socket_user.c main() for a more concrete example

## Building a RPM

The RPM package by default places the socket in `/var/run/socket_binder.socket`

To build a CentOS 5 and up installable RPM

```
docker run --rm -v `pwd`:/socket_binder centos:5.11 /socket_binder/linux/build_rpm_inside_docker.sh
```

To test this we use:

```
docker run --rm -ti -v `pwd`:/socket_binder centos:5.11 /bin/sh
rpm -i /socket_binder/socket_binder-1.0.1-1.x86_64.rpm
```

and verify it's all up & running

## Installing on Darwin

```
make
sudo make install-darwin
```
