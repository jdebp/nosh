#!/bin/sh -e
objects="common-manager.o main-exec.o builtins-session-manager.o api_mounts.o api_symlinks.o service-manager-socket.o system-control.o system-state-change.o system-control-status.o start-stop-service.o enable-disable-preset.o"
libraries="builtins.a utils.a"
[ "`uname`" = "FreeBSD" ] || kqueue=-lkqueue
[ "`uname`" = "FreeBSD" ] || rt=-lrt
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${kqueue} ${static} ${rt}
