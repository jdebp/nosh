#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim:set filetype=sh:
#
# This is run at binary package compile time to decide which service source file to use.

case "`uname`" in
Linux)
	redo-ifchange os_version
	read -r os_version < os_version
	case "${os_version}" in
	arch:*) 	ext=linux ;;
	debian:[78]) 	ext=debian7-linux ;;
	debian:*) 	ext=linux ;;
	gentoo:*) 	ext=linux ;;
	centos:*) 	ext=linux ;;
	rhel:*) 	ext=linux ;;
	*)      	ext=who ;;
	esac
	;;
*BSD)	ext=bsd ;;
*)	ext=who ;;
esac
redo-ifchange "$1.${ext}"
ln -s -f "`basename \"$1\"`.${ext}" "$3"
