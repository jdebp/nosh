#!/bin/sh -e
objects="main-exec.o builtins-session-manager.o"
libraries="builtins.a manager.a utils.a"
[ "`uname`" = "FreeBSD" ] || kqueue=-lkqueue
[ "`uname`" = "FreeBSD" ] || rt=-lrt
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${kqueue} ${static} ${rt}
