#!/bin/sh -e
objects="main-exec.o builtins-system-manager.o"
libraries="builtins.a manager.a utils.a"
[ "`uname`" = "FreeBSD" ] || kqueue=-lkqueue
[ "`uname`" = "FreeBSD" ] && static="-static"
[ "`uname`" = "FreeBSD" ] || rt=-lrt
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${kqueue} ${static} ${rt}
