#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# 2015-12-03: This line forces a rebuild for the new map file layout.
# 2015-12-21: This line forces a rebuild for the new map file layout.
# 2020-03-14: This line forces a rebuild for the new map file layout.
# 2025-04-18: This line forces a rebuild for the new map file layout.

redo-ifchange vt-keymaps

check_parts() {
	local f
	for f
	do
		if test -e "${f}"
		then
			redo-ifchange "${f}"
		else
			redo-ifcreate "${f}"
		fi
	done
	for f
	do
		test -f "${f}" || test -L "${f}" || return 111
		test -r "${f}" || return 111
	done
	return 0
}

find_parts() {
	local keymaps

	countrydiff=''
	keycountdiff=''
	fkeys=''
	src=''

	if test -d "vt-keymaps/"
	then
		src="vt-keymaps/${cc}${keycount}${option}.kbd"
		! check_parts "${src}" || return 0

		src="vt-keymaps/${cc}${option}.kbd"
		keycountdiff="${cc}_to_${cc}${keycount}.kbd"
		! check_parts "${src}" "${keycountdiff}" || return 0

		src="vt-keymaps/${cc}.102${option}.kbd"
		keycountdiff="${cc}.102_to_${cc}${keycount}.kbd"
		! check_parts "${src}" "${keycountdiff}" || return 0

		src="vt-keymaps/${cc}.102${option}.kbd"
		keycountdiff=''
		! check_parts "${src}" || return 0

		src="vt-keymaps/${cc}.101${option}.kbd"
		keycountdiff="${cc}.101_to_${cc}${keycount}.kbd"
		! check_parts "${src}" "${keycountdiff}" || return 0

		src="vt-keymaps/${cc}${option}.kbd"
		keycountdiff=''
		! check_parts "${src}" || return 0
	fi

	src='/dev/null'

	case "`uname`" in
	(FreeBSD)
		fkeys='sco_fkeys'.kbd

		countrydiff="default_to_${cc}${keycount}".kbd
		keycountdiff=''
		! check_parts "${countrydiff}" "${fkeys}" || return 0

		countrydiff="default_to_${cc}".kbd
		keycountdiff="${cc}_to_${cc}${keycount}.kbd"
		! check_parts "${countrydiff}" "${keycountdiff}" "${fkeys}" || return 0

		countrydiff="default_to_${cc}".kbd
		keycountdiff=''
		! check_parts "${countrydiff}" "${fkeys}" || return 0

		countrydiff=''
		keycountdiff=''

		fkeys=''
		;;
	(*)
		;;
	esac

	fkeys=''

	countrydiff="default_to_${cc}${keycount}".kbd
	keycountdiff=''
	! check_parts "${countrydiff}" || return 0

	countrydiff="default_to_${cc}".kbd
	keycountdiff="${cc}_to_${cc}${keycount}.kbd"
	! check_parts "${countrydiff}" "${keycountdiff}" || return 0

	countrydiff="default_to_${cc}".kbd
	keycountdiff=''
	! check_parts "${countrydiff}" || return 0

	countrydiff=''
	keycountdiff=''
	printf 1>&2 "%s: ERROR: %s: %s\n" "$0" "${cc}${keycount}${option}" 'Keyboard map source not found.'
	return 111
}

option="`basename "$1"`"
cc="${option%%.*}"
option="${option#${cc}.}"
case "${option}" in
(*.capsctrl)	capsctrl='swap_capsctrl.kbd'; option="${option%.capsctrl}" ;;
(*)		capsctrl='modelm_capsctrl.kbd' ;;
esac
case "${option}" in
(10[1-9].*)	keycount="${option%%.*}"; option="${option#${keycount}.}" ;;
(10[1-9])	keycount="${option}"; option='' ;;
(*)		keycount='' ;;
esac
keycount="${keycount:+.${keycount}}"
option="${option:+.${option}}"

check_parts soft_backspace.kbd soft_delete.kbd soft_enter.kbd soft_escape.kbd soft_return.kbd soft_tab.kbd "${capsctrl}"
find_parts

console-convert-kbdmap > "$3" "${src}" soft_backspace.kbd soft_delete.kbd soft_enter.kbd soft_escape.kbd soft_return.kbd soft_tab.kbd "${capsctrl}" ${countrydiff} ${keycountdiff} ${fkeys}
