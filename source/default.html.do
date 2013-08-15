#!/bin/sh -e
mkdir -p tmp
src="`basename "$1"`.xml"
man="index.html"
redo-ifchange "${src}"
xmlto --skip-validation -o tmp html "${src}"
mv "tmp/${man}" "$3"
