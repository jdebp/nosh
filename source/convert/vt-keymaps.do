#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Find the vt(4) keyboard map files.
# This is invoked by keyboard-maps.do .

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_var_default() { read_rc "$1" || printf '%s\n' "$2" ; }

redo-ifchange rc.conf

case "`uname`" in
(FreeBSD)
	keymaps="`get_var_default \"keymaps\" /usr/share/vt/keymaps`"
	;;
(*)
	keymaps="`get_var_default \"keymaps\" /usr/local/share/vt/keymaps`"
	;;
esac
redo-ifchange "${keymaps}"
rm -f -- "$3"
ln -s -- "${keymaps}" "$3"
