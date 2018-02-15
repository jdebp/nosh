#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim:set filetype=sh:
#

printf "%s" "${XDG_CONFIG_DIRS:-/usr/local/share:/usr/share:/share}" | sed -e 's/:/\n/g' > "$3"
