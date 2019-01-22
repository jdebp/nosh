#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for kernel virtual terminals.
# This is invoked by all.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_var() { read_rc "$1" || true ; }

redo-ifchange rc.conf general-services

if ! keymap="`read_rc \"keymap\"`"
then
	keymap="us"
fi
original="${keymap}"
# There are a whole bunch of old names for keyboard mappings that systems could be using.
# These the vt names, on the presumption that kbdcontrol is going to be using the vt keymaps.
case "${keymap}" in
hy.armscii-8)			keymap="am";;
be.iso.acc)			keymap="be.acc";;
be.iso)				keymap="be";;
bg.bds.ctrlcaps)		keymap="bg.bds";;
bg.phonetic.ctrlcaps)		keymap="bg.phonetic";;
br275.iso.acc)			keymap="br";;
br275.*)			keymap="br.noacc";;
by.*)				keymap="by";;
fr_CA.iso.acc)			keymap="ca-fr";;
swissgerman.macbook.acc)	keymap="ch.macbook.acc";;
swissgerman.iso.acc)		keymap="ch.acc";;
swissgerman.*)			keymap="ch";;
swissfrench.iso.acc)		keymap="ch-fr.acc";;
swissfrench.*)			keymap="ch-fr";;
ce.iso2)			keymap="centraleuropean.qwerty";;
colemak.iso15.acc)		keymap="colemak.acc";;
cs.*|cz.*)			keymap="cz";;
german.iso.acc)			keymap="de.acc";;
german.*)			keymap="de";;
danish.iso.acc)			keymap="dk.acc";;
danish.iso.macbook)		keymap="dk.macbook";;
danish.*)			keymap="dk";;
estonian.*)			keymap="ee";;
spanish.dvorak)			keymap="es.dvorak";;
spanish.iso*.acc)		keymap="es.acc";;
spanish.iso)			keymap="es";;
finnish.*)			keymap="fi";;
fr.macbook.acc)			keymap="fr.macbook";;
fr.iso.acc)			keymap="fr.acc";;
fr.iso)				keymap="fr";;
el.iso07)			keymap="gr";;
gb.*|gb)			keymap="uk";;
gr.us101.acc)			keymap="gr.101.acc";;
hr.iso)				keymap="hr";;
hu.iso2.101keys)		keymap="hu.101";;
hu.iso2.102keys)		keymap="hu.102";;
iw.iso8)			keymap="il";;
icelandic.iso.acc)		keymap="is.acc";;
icelandic.iso)			keymap="is";;
it.iso)				keymap="it";;
jp.106x)			keymap="jp.capsctrl";;
jp.106)				keymap="jp";;
kk.pt154.io)			keymap="kz.io";;
kk.pt154.kst)			keymap="kz.kst";;
latinamerican.iso.acc)		keymap="latinamerican.acc";;
lt.iso4)			keymap="lt";;
norwegian.iso)			keymap="no";;
norwegian.dvorak)		keymap="no.dvorak";;
dutch.iso.acc)			keymap="nl";;
eee_nordic)			keymap="nordic.asus-eee";;
pl_PL.dvorak)			keymap="pl.dvorak";;
pl_PL.ISO8859-2)		keymap="pl";;
pt.iso.acc)			keymap="pt.acc";;
pt.iso)				keymap="pt";;
ru.koi8-r.shift)		keymap="ru.shift";;
ru.koi8-r.win)			keymap="ru.win";;
ru.*)				keymap="ru";;
swedish.*)			keymap="se";;
si.iso)				keymap="si";;
sk.iso2)			keymap="sk";;
tr.iso9.q)			keymap="tr";;
ua.koi8-u.shift.alt)		keymap="ua.shift.alt";;
ua.*)				keymap="ua";;
uk.*-ctrl)			keymap="uk.capsctrl";;
uk.dvorak)			keymap="uk.dvorak";;
uk.*)				keymap="uk";;
us.iso.acc)			keymap="us.acc";;
us.pc-ctrl)			keymap="us.capsctrl";;
us.iso)				keymap="us";;
[Nn][Oo])			keymap="us";;
esac
if ! test _"${original}" = _"${keymap}"
then
	echo 1>&2 $0: Please adjust /etc/rc.conf to read keymap="${keymap}".
fi

exec printf "%s\n" "${keymap}" > "$3"

if s="`system-control find kernel-vt-kbdcontrol 2>/dev/null`"
then
	for i in keyboard keybell keyrate
	do
		v="`get_var \"$i\"`"
		case "$v" in
		[Nn][Oo]|'')
			system-control set-service-env "${s}" "$i"
			;;
		*)
			system-control set-service-env "${s}" "$i" "$v"
			;;
		esac
	done
	system-control set-service-env "${s}" "keymap" "${keymap}"
	printf >> "$3" "%s:\n" "${s}"
	system-control print-service-env "${s}" >> "$3"
fi

if s="`system-control find kernel-vt-vidcontrol 2>/dev/null`"
then
	for i in font8x8 font8x14 font8x16 blanktime cursor scrnmap
	do
		v="`get_var \"$i\"`"
		case "$v" in
		[Nn][Oo]|'')
			system-control set-service-env "${s}" "$i"
			;;
		*)
			system-control set-service-env "${s}" "$i" "$v"
			;;
		esac
	done
	printf >> "$3" "%s:\n" "${s}"
	system-control print-service-env "${s}" >> "$3"
fi

if s="`system-control find kernel-vt-loadkeys 2>/dev/null`"
then
	system-control set-service-env "${s}" "keymap" "${keymap}"
	printf >> "$3" "%s:\n" "${s}"
	system-control print-service-env "${s}" >> "$3"
fi

if s="`system-control find kernel-vt-setfont 2>/dev/null`"
then
	for i in font8x8 font8x14 font8x16 scrnmap fontmap
	do
		v="`get_var \"$i\"`"
		case "$v" in
		[Nn][Oo]|'')
			system-control set-service-env "${s}" "$i"
			;;
		*)
			system-control set-service-env "${s}" "$i" "$v"
			;;
		esac
	done
	printf >> "$3" "%s:\n" "${s}"
	system-control print-service-env "${s}" >> "$3"
fi

if s="`system-control find kernel-vt-setterm 2>/dev/null`"
then
	for i in blanktime
	do
		v="`get_var \"$i\"`"
		case "$v" in
		[Nn][Oo]|'')
			system-control set-service-env "${s}" "$i"
			;;
		*)
			system-control set-service-env "${s}" "$i" "$v"
			;;
		esac
	done
	printf >> "$3" "%s:\n" "${s}"
	system-control print-service-env "${s}" >> "$3"
fi

if s="`system-control find kernel-vt-kbdrate 2>/dev/null`"
then
	v="`get_var \"keyrate\"`"
	# This service does not understand the kbdcontrol high-level settings, so we have to translate to the low-level kbdrate ones.
	case "$v" in
	[Nn][Oo]|'')
		system-control set-service-env "${s}" "rate"
		system-control set-service-env "${s}" "delay"
		;;
	slow)
		system-control set-service-env "${s}" "rate" "1000"
		system-control set-service-env "${s}" "delay" "504"
		;;
	normal)
		system-control set-service-env "${s}" "rate" "500"
		system-control set-service-env "${s}" "delay" "126"
		;;
	fast)
		system-control set-service-env "${s}" "rate" "250"
		system-control set-service-env "${s}" "delay" "34"
		;;
	*)
		system-control set-service-env "${s}" "rate" "${v%%.*}"
		system-control set-service-env "${s}" "delay" "${v#*.}"
		;;
	esac
	printf >> "$3" "%s:\n" "${s}"
	system-control print-service-env "${s}" >> "$3"
fi
