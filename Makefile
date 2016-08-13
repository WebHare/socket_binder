PREFIX ?= /usr/local
RPMBASE ?= ${HOME}/rpmbuild/
PACKAGE = socket_binder-1.0.1-1
TMPSTORE = /tmp/building

all:	socket_binder

socket_binder:	socket_binder.c
	cc -W -g ${CFLAGS} $< -o $@

socket_user:	socket_user.c
	cc -W -g ${CFLAGS} $< -o $@

install: socket_binder
	mkdir -p ${PREFIX}/sbin/
	cp -f socket_binder ${PREFIX}/sbin/

uninstall:
	rm -f $PREFIX/socket_binder

install-darwin: install
	cp -f darwin/com.b-lex.socket_binder.plist /Library/LaunchDaemons/
	launchctl load /Library/LaunchDaemons/com.b-lex.socket_binder.plist

uninstall-darwin: uninstall
	launchctl unload /Library/LaunchDaemons/com.b-lex.socket_binder.plist
	rm /Library/LaunchDaemons/com.b-lex.socket_binder.plist

clean:
	rm -f socket_user socket_binder

${RPMBASE}/SOURCES/socket_binder.tar.gz:
	rm -rf -- ${TMPSTORE}
	mkdir -p ${TMPSTORE}/socket_binder
	cp -r Makefile *.c linux/socket_binderctl ${TMPSTORE}/socket_binder
	tar zcf ${RPMBASE}/SOURCES/socket_binder.tar.gz -C ${TMPSTORE} socket_binder

rpm: ${RPMBASE}/SOURCES/socket_binder.tar.gz
	cp -f linux/socket_binder.spec ${RPMBASE}/SPECS/
	rpmbuild -ba --target x86_64 ${RPMBASE}/SPECS/socket_binder.spec
	cp ${RPMBASE}/RPMS/x86_64/${PACKAGE}.x86_64.rpm .
