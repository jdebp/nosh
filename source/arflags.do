#!/bin/sh -e
if test _"`uname`" = _"Interix"
then
	ar="wlib.exe"
	arflags="-n -q"
else
	ar="ar"
	arflags="rc"
fi
case "`basename "$1"`" in
ar)
	echo "$ar" > "$3"
	;;
arflags)
	echo "$arflags" > "$3"
	;;
*)
	echo 1>&2 "$1: No such target."
	exit 111
	;;
esac
true
