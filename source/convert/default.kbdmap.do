#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# 2015-12-03: This line forces a rebuild for the new map file layout.
# 2015-12-21: This line forces a rebuild for the new map file layout.
n="`basename "$1"`"
case "$n" in
*.capsctrl)	capsctrl=swap_capsctrl.kbd;n="${n%.capsctrl}";;
*)		capsctrl="";;
esac
keymaps="/usr/share/vt/keymaps/"
if test -d "${keymaps}"
then
	src="${keymaps}/${n}.kbd"
	countrydiff=""
	redo-ifchange "${src}"
else
	redo-ifcreate "${keymaps}"
	src="/dev/null"
	case "${n}" in
	us)	countrydiff="";;
	*)	countrydiff="us_to_$n".kbd;;
	esac
fi
redo-ifchange soft_backspace.kbd ${capsctrl} ${countrydiff}
console-convert-kbdmap "${src}" soft_backspace.kbd ${capsctrl} ${countrydiff} > "$3"
