#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
nam="`basename "$1"`"
src="${nam}.xml"
man="${nam}$2"
install -d "tmp/${nam}"
redo-ifchange "${src}" command/build-helper "version.xml"
exec command/build-helper setlock "tmp/${nam}/index.html.lock" sh -c "xmlto --skip-validation -o \"tmp/${nam}\" man \"${src}\" && mv \"tmp/${nam}/${man}\" \"$3\""
