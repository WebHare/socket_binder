#!/bin/sh
RPMBASE=/usr/src/redhat
yum install -y gcc rpm-build make
cd /socket_binder/
make rpm
cp /usr/src/redhat/RPMS/x86_64/socket_binder-*.x86_64.rpm /usr/src/redhat/SRPMS/socket_binder-*.src.rpm .
