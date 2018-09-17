#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim:set filetype=sh:
#
# This prototype is copied into every (real) user's system-control/convert directory.
#
# This is run by the per-user external configuration import subsystem.
# This is the top level of the build system.

exec redo-ifchange services
