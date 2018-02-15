#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim:set filetype=sh:
case "`uname`" in
Linux)
	redo-ifchange /etc/os-release
	# These get us *only* the operating system variables, safely.
	read_os() { /bin/exec clearenv read-conf /etc/os-release "`which printenv`" "$1" ; }

	case "`read_os ID`:`read_os VERSION_ID`" in
	arch:*) 	ext=linux ;;
	debian:[78]) 	ext=debian7-linux ;;
	debian:*) 	ext=linux ;;
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
