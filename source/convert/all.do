#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Convert all external configuration formats.
# Use the "redo" command to run this .do script, via "redo all".
#
redo-ifchange volumes general-services terminal-services kernel-modules user-services host.conf sysctl.conf
