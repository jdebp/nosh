#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:

exec redo-ifchange version.h systemd_names_escape_char.h all-commands all-misc all-targets all-services
