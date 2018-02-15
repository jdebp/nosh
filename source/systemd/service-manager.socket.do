#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
case "`uname`" in
Linux)
	redo-ifchange /etc/os-release exec
	# These get us *only* the operating system variables, safely.
	read_os() { ./exec clearenv read-conf /etc/os-release "`which printenv`" "$1" ; }

	case "`read_os ID`:`read_os VERSION_ID`" in
	arch:*) 	ext=redhat-linux ;;
	debian:*) 	ext=debian-linux ;;
	centos:*)	ext=redhat-linux ;;
	rhel:*) 	ext=redhat-linux ;;
	*)      	ext=who ;;
	esac
	;;
*BSD)	ext=bsd ;;
*)	ext=who ;;
esac
redo-ifchange "$1.${ext}"
ln -s -f "`basename \"$1\"`.${ext}" "$3"
