#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
case "`uname`" in
(Linux)	ext=linux ;;
(*BSD)	ext=bsd ;;
(*)	ext=who ;;
esac
src="$(basename "$1").${ext}"
redo-ifchange "${src}"
ln -f "${src}" "$3"
