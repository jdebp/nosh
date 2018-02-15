#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for host.conf.
# This is invoked by all.do .
#

nss=nsswitch.conf

redo-ifchange "$nss"
echo > "$3" "# Automatically re-generated from $nss"
awk >> "$3" '/^[[:space:]]*hosts:/ {split($0,a);delete a[1];for (i in a) if (0 == match(a[i],"=") && "cache" != a[i]) if (a[i] == "files") print "hosts"; else print a[i]; }' "$nss"
