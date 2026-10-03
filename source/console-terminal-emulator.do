#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
main="`basename "$1"`"
objects="object/main-exec.o object/builtins-${main}.o"
libraries='library/builtins.a library/utils.a'
case "`uname`" in
	(Linux)
		crypt=-lcrypt
		;;
	(*BSD)
		util=-lutil
		crypt=-lcrypt
		;;
esac
redo-ifchange config/haspam.h
grep -q -F HAS_PAM config/haspam.h && pam="-lpam"
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${crypt} ${static} ${util} ${pam}
