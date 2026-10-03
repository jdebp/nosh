#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
case "`uname`" in
(FreeBSD)	ext=freebsd ;;
(NetSD)		ext=netbsd ;;
(*BSD)		ext=bsd ;;
(Linux)  	ext=linux ;;
(*)		ext=who ;;
esac
redo-ifchange "$1.${ext}"
ln -s -f "`basename \"$1\"`.${ext}" "$3"
