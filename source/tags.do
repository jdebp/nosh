#!/bin/sh -e
files="*.cpp *.h"
rm -f -- "$3"
opts=""
test _"`uname`" = _"Linux" && opts="--extra=+fq"
ctags -f "$3" ${opts} ${files}
redo-ifchange ${files}
