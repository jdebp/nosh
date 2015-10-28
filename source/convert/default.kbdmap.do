#!/bin/sh -e
n="`basename "$1"`"
case "$n" in
*.capsctrl)	capsctrl=swap_capsctrl.kbd;n="${n%.capsctrl}";;
*)		capsctrl="";;
esac
src="/usr/share/vt/keymaps/${n}.kbd"
redo-ifchange "${src}" soft_backspace.kbd ${capsctrl}
console-convert-kbdmap "${src}" soft_backspace.kbd ${capsctrl} > "$3"
