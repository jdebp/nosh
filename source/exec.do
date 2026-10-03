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
		uuid=-luuid
		rt=-lrt
		;;
	(FreeBSD)
		util=-lutil
		static="-static"
		;;
	(*BSD)
		util=-lutil
		;;
esac
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${uuid} ${rt} ${util} ${static}
