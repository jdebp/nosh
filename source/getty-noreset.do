#!/bin/sh -e
case "`uname`" in
Linux)	ext=linux ;;
*BSD)	ext=bsd ;;
*)	ext=who ;;
esac
redo-ifchange "$1.${ext}"
ln -s -f "`basename \"$1\"`.${ext}" "$3"
