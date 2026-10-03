#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Convert all kbdmap files.
# This is invoked by all.do .

redo-ifchange vt-keymaps

list_keyboard_maps() {

	if test -d 'vt-keymaps/'
	then
		redo-ifdelete vt-keymaps/
		find 'vt-keymaps/' -maxdepth 1 -name '*.kbd' -a \( -type l -o -type f \) |
		while read -r k
		do
			b="`basename \"$k\" .kbd`"
			case "$b" in
			*.pc98.*)	continue;;	# deny-listed
			*.pc98)		continue;;	# deny-listed
			*.caps)		continue;;	# unnecessary
			*.ctrl)		continue;;	# unnecessary
			*.capsctrl)	continue;;	# unnecessary
			*.102)		b="${b%%.*}";;
			*.10[1-9].*)	continue;;	# unnecessary
			*.10[1-9])	continue;;	# unnecessary
			esac
			printf "%s\n" "$b"
			printf >> "$3" 'SCO Multiscreen Console keymap file %s\n' "${k}"
		done
	else
		redo-ifcreate vt-keymaps/
	fi

	# FIXME: missing: hu ko kz se
	printf "%s\n" br ca ca-fr ch ch-fr cz de dk es 'fi' gb-shaw gr il is jp nl no pl uk us
}

list_keyboard_maps '' '' "$3" |
awk '!x[$0]++' |
while read -r b
do
	cc="${b%%.*}"
	option="${b#${cc}}"
	for keys in 104 105 106 107 109
	do
		echo >> "$3" kbdmaps/"${cc}"."${keys}""${option}"
		redo-ifchange kbdmaps/"${cc}"."${keys}""${option}"
		echo >> "$3" kbdmaps/"${cc}"."${keys}""${option}".capsctrl
		redo-ifchange kbdmaps/"${cc}"."${keys}""${option}".capsctrl
	done
done
