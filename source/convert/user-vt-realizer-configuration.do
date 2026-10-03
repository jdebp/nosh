#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Convert the /etc/ttys external configuration format.
# This is invoked by all.do .

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_var() { read_rc "$1" || true ; }
get_var_default() { read_rc "$1" || printf '%s\n' "$2" ; }

redo-ifchange rc.conf

get_default_map() {
	local keymap="`get_var_default \"keymap\" us`"
	local cc="${keymap%%.*}"
	local option="${keymap#${cc}}"
	local keys=105

	# There are a whole bunch of old syscons names for keyboard mappings that rc.conf could be using.
	# This incorporates the modernizations done by /etc/rc.d/syscons plus the translation therefrom to yield the name built in our kbdmaps directory.
	case "${cc}" in
	(hy)			cc="am";;
	(br275)			cc="br";;
	(fr_CA)			cc="ca-fr";;
	(swissgerman)		cc="ch";;
	(swissfrench)		cc="ch-fr";;
	(ce)			cc="centraleuropean";;
	(colemak)		cc="colemak";;
	(cs)			cc="cz";;
	(german)		cc="de";;
	(danish)		cc="dk";;
	(estonian)		cc="ee";;
	(spanish)		cc="es";;
	(finnish)		cc="fi";;
	(el)			cc="gr";;
	(gb)			cc="uk";;
	(iw)			cc="il";;
	(icelandic)		cc="is";;
	(kk)			cc="kz";;
	(norwegian)		cc="no";;
	(dutch)			cc="nl";;
	(pl_PL)			cc="pl";;
	(swedish)		cc="se";;
	(eee_nordic)		cc="nordic";option=".asus-eee";;
	esac

	case "${cc}" in
	(ko)			keys="106";;
	(br)			keys="107";;
	(jp)			keys="109";;
	(us)			keys="104";;
	(*)			keys="105";;
	esac

	case "${option}" in
	(.armscii-8)		option="";;
	(.iso*.acc)
		case "${cc}" in
		(br)		option="";;
		(nl)		option="";;
		(*)		option=".acc";;
		esac
		;;
	(.us101.acc)		option=".acc";keys="104";;
	(.macbook.acc)		option=".acc";;
	(.iso*.macbook)		option=".macbook";;
	(.iso2.101keys)		option="";keys="104";;
	(.iso*)
		case "${cc}" in
		(br)		option=".noacc";;
		(nl)		option=".noacc";;
		(*)		option="";;
		esac
		;;
	(.pt154.io)		option=".io";;
	(.pt154.kst)		option=".kst";;
	(.106x)			option=".capsctrl";keys="109";;
	(.*-ctrl)		option=".capsctrl";;
	(.bds.ctrlcaps)		option=".bds.capsctrl";;
	(.phonetic.ctrlcaps)	option=".phonetic.capsctrl";;
	(.ISO8859-2)		option="";;
	(.koi8-r.shift)		option=".shift";;
	(.koi8-r.win)		option=".win";;
	(.koi8-u.shift.alt)	option=".shift.alt";;
	(.iso2)			option=".qwerty";;
	esac

	printf '%s.%s%s\n' "${cc}" "${keys}" "${option}"
}

install -d -m 02055 -o root -g user-vt-realizer user-vt user-vt/kbdmaps user-vt/fonts

# Unless overridden, everyone defaults to a single keyboard map.
if m="`get_default_map`"
then
	test -e user-vt/kbdmaps/default || ln -f -s ../../kbdmaps/"$m" user-vt/kbdmaps/default
	printf "kbdmaps/default=%s\n" "$m" >> "$3"
fi
