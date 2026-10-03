#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
case "`uname`" in
(Linux)
	# A far superior mechanism would be to read /etc/os-release with read-conf.
	# Unfortunately, at this point in the build process read-conf is not yet available.
	# This must match the test in package/debian/*.funcs .
	if test -e /etc/arch-release
	then
		redo-ifdelete /etc/arch-release
		ext="arch"
	else
		redo-ifcreate /etc/arch-release
		ext="debian"
	fi
	;;
(NetBSD)	ext="netbsd" ;;
(*BSD)	ext="bsd" ;;
(*)	ext="unknown" ;;
esac
redo-ifchange "$1.${ext}"
ln -s -f "`basename \"$1\"`.${ext}" "$3"
