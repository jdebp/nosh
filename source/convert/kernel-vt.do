#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Special setup for kernel virtual terminals.
# This is invoked by general-services.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf "`which printenv`" "$1" ; }
get_var() { read_rc "$1" || true ; }

redo-ifchange rc.conf general-services

case "`uname`" in
*BSD)	;;
*)	echo > "$3" 'No kernel-vt';exit 0;;
esac

for i in keyboard keybell keyrate
do
	v="`get_var \"$i\"`"
	case "$v" in
	[Nn][Oo])
		system-control set-service-env kernel-vt-kbdcontrol "$i"
		;;
	*)
		system-control set-service-env kernel-vt-kbdcontrol "$i" "$v"
		;;
	esac
done

for i in font8x8 font8x14 font8x16 blanktime cursor scrnmap
do
	v="`get_var \"$i\"`"
	case "$v" in
	[Nn][Oo])
		system-control set-service-env kernel-vt-vidcontrol "$i"
		;;
	*)
		system-control set-service-env kernel-vt-vidcontrol "$i" "$v"
		;;
	esac
done

keymap="`get_var \"keymap\"`"
original="${keymap}"
# There are a whole bunch of old names for keyboard mappings that systems could be using.
# Note that this is NOT the same as the modernizations done by the /etc/rc.d system in the syscons to vt conversion.
# There are some subtle differences, in part because the conversion process builds capsctrl versions of everything.
case "${keymap}" in
hy.armscii-8)			keymap="am";;
be.iso.acc)			keymap="be.acc";;
be.iso)				keymap="be";;
bg.bds.ctrlcaps)		keymap="bg.bds.capsctrl";;
bg.phonetic.ctrlcaps)		keymap="bg.phonetic.capsctrl";;
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
esac
if ! test "${original}" = "${keymap}"
then
	echo 1>&2 $0: Please adjust /etc/rc.conf to read keymap="${keymap}".
fi
case "${keymap}" in
[Nn][Oo])
	system-control set-service-env console-fb-realizer@head0 "KBDMAP" "kbdmaps/us.kbdmap"
	system-control set-service-env kernel-vt-kbdcontrol "keymap" "us"
	;;
*)
	system-control set-service-env console-fb-realizer@head0 "KBDMAP" "kbdmaps/${keymap}.kbdmap"
	system-control set-service-env kernel-vt-kbdcontrol "keymap" "${keymap}"
	;;
esac
system-control print-service-env kernel-vt-kbdcontrol >> "$3"
system-control print-service-env kernel-vt-vidcontrol >> "$3"
system-control print-service-env console-fb-realizer@head0 "KBDMAP" >> "$3"
