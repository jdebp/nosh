#!/bin/sh -e
objects="common-manager.o main-exec.o builtins-system-manager.o api_mounts.o api_symlinks.o service-manager-socket.o system-control.o system-control-init.o system-control-status.o system-control-job.o system-control-enable.o"
libraries="builtins.a utils.a"
[ "`uname`" = "FreeBSD" ] || kqueue=-lkqueue
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${kqueue} -lrt
