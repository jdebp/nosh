#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
nam="`basename "$1"`"
src="${nam}.xml"
man="index.html"
install -d "tmp/${nam}"
redo-ifchange "${src}" exec "version.xml"
exec ./exec setlock "tmp/${nam}/index.html.lock" sh -c "xmlto --skip-validation -o \"tmp/${nam}\" html \"${src}\" && sed -e 's/href=\"${man}#/href=\"#/g' \"tmp/${nam}/${man}\" > \"$3\""
