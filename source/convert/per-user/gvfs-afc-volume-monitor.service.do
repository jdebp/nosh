#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim:set filetype=sh:
#
# This is run at binary package compile time to decide which service source file to use.

case "`uname`" in
Linux)	ext=linux ;;
*BSD)	ext=bsd ;;
*)	ext=who ;;
esac
redo-ifchange "$1.${ext}"
ln -s -f "`basename \"$1\"`.${ext}" "$3"
