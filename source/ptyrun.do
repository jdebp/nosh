#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
main=$(basename $1)
redo-ifchange "${main}"
ln -f -- "${main}" "$3"
chmod 0755 $3
