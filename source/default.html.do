#!/bin/sh -e
mkdir -p tmp
src="`basename "$1"`.xml"
man="index.html"
redo-ifchange "${src}" exec "version.xml"
exec ./exec setlock tmp/index.html.lock sh -c "xmlto --skip-validation -o tmp html \"${src}\" && mv \"tmp/${man}\" \"$3\""
