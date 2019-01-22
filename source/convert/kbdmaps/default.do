#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# 2015-12-03: This line forces a rebuild for the new map file layout.
# 2015-12-21: This line forces a rebuild for the new map file layout.
option="`basename "$1"`"
cc="${option%%.*}"
option="${option#${cc}.}"
case "${option}" in
*.capsctrl)	capsctrl=swap_capsctrl.kbd; option="${option%.capsctrl}" ;;
*)		capsctrl="" ;;
esac
case "${option}" in
10[1-9].*)	keycount="${option%%.*}"; option="${option#${keycount}.}" ;;
10[1-9])	keycount="${option}"; option="" ;;
*)		keycount="" ;;
esac
keycount="${keycount:+.${keycount}}"
option="${option:+.${option}}"
keymaps="/usr/share/vt/keymaps/"
if test -d "${keymaps}"
then
	if test -e "${keymaps}/${cc}${keycount}${option}.kbd"
	then
		src="${keymaps}/${cc}${keycount}${option}.kbd"
		keycountdiff=""
	elif test -e "${keymaps}/${cc}.102${option}.kbd"
	then
		redo-ifcreate "${keymaps}/${cc}${keycount}${option}.kbd"
		src="${keymaps}/${cc}.102${option}.kbd"
		keycountdiff="${cc}.102_to_${cc}${keycount}.kbd"
		if test -e "${keycountdiff}"
		then
			redo-ifchange "${keycountdiff}"
		else
			redo-ifcreate "${keycountdiff}"
			keycountdiff=""
		fi
	else
		redo-ifcreate "${keymaps}/${cc}${keycount}${option}.kbd" "${keymaps}/${cc}.102${option}.kbd"
		src="${keymaps}/${cc}${option}.kbd"
		keycountdiff="${cc}_to_${cc}${keycount}.kbd"
		if test -e "${keycountdiff}"
		then
			redo-ifchange "${keycountdiff}"
		else
			redo-ifcreate "${keycountdiff}"
			keycountdiff=""
		fi
	fi
	countrydiff=""
	redo-ifchange "${src}"
else
	redo-ifcreate "${keymaps}"
	src="/dev/null"
	countrydiff="default_to_${cc}${keycount}".kbd
	if test -e "${countrydiff}"
	then
		redo-ifchange "${countrydiff}"
		keycountdiff=""
	else
		redo-ifcreate "${countrydiff}"
		countrydiff="default_to_${cc}".kbd
		if test -e "${countrydiff}"
		then
			redo-ifchange "${countrydiff}"
			keycountdiff="${cc}_to_${cc}${keycount}.kbd"
			if test -e "${keycountdiff}"
			then
				redo-ifchange "${keycountdiff}"
			else
				redo-ifcreate "${keycountdiff}"
				keycountdiff=""
			fi
		else
			redo-ifcreate "${countrydiff}"
			countrydiff=""
			keycountdiff=""
		fi
	fi
fi
redo-ifchange soft_backspace.kbd soft_delete.kbd soft_enter.kbd soft_return.kbd ${capsctrl} ${countrydiff} ${keycountdiff}
console-convert-kbdmap "${src}" soft_backspace.kbd soft_delete.kbd soft_enter.kbd soft_return.kbd ${capsctrl} ${countrydiff} ${keycountdiff} > "$3"
