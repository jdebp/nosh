#!/bin/sh -e
files="*.cpp *.h"
rm -f -- "$3"
opts=""
[ "`uname`" = "Interix" ] || opts="-e"
ctags --extra=+q -f "$3" ${opts} ${files}
redo-ifchange ${files}
