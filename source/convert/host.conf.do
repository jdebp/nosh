#!/bin/sh -e
#
# Special setup for host.conf.
# This is invoked by all.do .
#

nss=/etc/nsswitch.conf

redo-ifchange "$nss"
echo > "$3" "# Automatically re-generated from $nss"
# FIXME: Not correct for Linux
awk >> "$3" '/^[[:space:]]*hosts:/ {split($0,a);delete a[1];for (i in a) print a[i];}' "$nss"
