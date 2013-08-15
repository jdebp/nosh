#!/bin/sh -e
mkdir -p tmp
src="`basename "$1"`.xml"
man="`basename "$1"`.1"
redo-ifchange "${src}"
xmlto --skip-validation -o tmp man "${src}"
mv "tmp/${man}" "$3"
