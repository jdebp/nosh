#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim:set filetype=sh:
#
# This prototype is copied into every (real) user's system-control/convert directory.
#
# This is run by the per-user external configuration import subsystem.
# It is used to determine the XDG config path.

printf "%s" "${XDG_CONFIG_DIRS:-/usr/local/share:/usr/share:/share}" | sed -e 's/:/\n/g' > "$3"
