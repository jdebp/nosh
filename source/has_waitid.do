#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
main="`basename "$1"`"
objects="${main}.o"
libraries=""
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries}
