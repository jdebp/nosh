#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Convert the /etc/ttys external configuration format.
# This is invoked by all.do .
#
# Note that we do not look at "console".
# emergency-login@console intentionally cannot be disabled via this mechanism.
# And there is no ttylogin@console service to adjust, for various reasons.
# One reason is that there will be a TUI login service for the underlying device, be it a virtual or a real terminal.
#
# Design:
#   * The run-kernel-vt and run-user-vt packages use preset/disable in their post-install and post-deinstall scripts to enable/disable their particular login services.
#   * Normal preset mechanisms apply, and take precedence over /etc/ttys.
#   * On the BSDs, there will always be an /etc/ttys file, defining fall-back presets.
#   * On Linux, there will usually not be an /etc/ttys file, and the default is preset enabled.
# See the Nosh Guide for more information.
#

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$1: Defaulted $2 to $3." ; fi ; }

list_heads() {
	seq 0 0 | sed -e 's:^:head:'
}

keymaps="/usr/share/vt/keymaps/"

list_keyboard_maps() {
	if test -d "${keymaps}"
	then
		redo-ifchange "${keymaps}"
		find "${keymaps}" -name '*.kbd' |
		while read -r k
		do
			b="`basename \"$k\" .kbd`"
			case "$b" in
			*.pc98.*)	continue;;	# blacklisted
			*.pc98)		continue;;	# blacklisted
			*.caps)		continue;;	# unnecessary
			*.ctrl)		continue;;	# unnecessary
			*.capsctrl)	continue;;	# unnecessary
			hu.102)		b="hu";;
			*.10[1-9].*)	continue;;	# unnecessary
			*.10[1-9])	continue;;	# unnecessary
			esac
			printf "%s\n" "$b"
		done
	else
		redo-ifcreate "${keymaps}"
		for b in us uk jp de
		do
			printf "%s\n" "$b"
		done
	fi
}

list_user_virtual_terminals() {
	seq 1 3 | sed -e 's:^:vc:' -e 's:$:-tty:'
}

list_kernel_virtual_terminals() {
	case "`uname`" in
	Linux)
		seq 1 12 | sed -e 's:^:tty:' 
		;;
	OpenBSD)
		for i in C D E F G H I J
		do 
			printf "tty$i%s\n" 0 1 2 3 4 5 6 7 8 9 a b
		done
		;;
	*BSD)
		printf "ttyv%s\n" 0 1 2 3 4 5 6 7 8 9 a b c d e f
		;;
	esac
}

list_real_terminals() {
	case "`uname`" in
	Linux) 
		# Linux is technically /dev/ttyS[0-9]* , but no-one has that many real terminal devices nowadays.
		seq 0 99 | sed -e 's:^:ttyS:'
		seq 0 99 | sed -e 's:^:ttyACM:'
		# These are special serial devices in several virtual machines.
		printf "%s\n" hvc0 xvc0 hvsi0 sclp_line0 ttysclp0 '3270!tty1'
		;;
	OpenBSD)
		printf "ttyU%s\n" 0 1 2 3
		for i in 0 1 2 3 4 5 6 7
		do 
			printf "tty$i%s\n" 0 1 2 3 4 5 6 7 8 9 a b c d e f
		done
		;;
	*BSD)
		printf "ttyu%s\n" 0 1 2 3 4 5 6 7 8 9 a b c d e f
		;;
	esac
}

# These files/directories not existing is not an error; but is a reason to rebuild when they appear.
for i in /etc/ttys /dev
do
	if test -e "$i"
	then
		redo-ifchange "$i"
	else
		redo-ifcreate "$i"
	fi
done

case "`uname`" in
FreeBSD)
	redo-ifchange termcap/termcap.db
	;;
esac

list_kernel_virtual_terminals | 
while read -r n
do
	if test -c "/dev/$n"
	then
		system-control preset --ttys --prefix "cyclog@ttylogin@" -- "$n"
		system-control preset --ttys --prefix "ttylogin@" -- "$n"
		if system-control is-enabled "ttylogin@$n"
		then
			echo >> "$3" on "$n"
		else
			echo >> "$3" off "$n"
		fi
	else
		redo-ifcreate "/dev/$n"
		if s="`system-control find \"cyclog@ttylogin@$n\" 2>/dev/null`"
		then
			system-control disable "$s"
		fi
		if s="`system-control find \"ttylogin@$n\" 2>/dev/null`"
		then
			system-control disable "$s"
			echo >> "$3" off "$n"
		else
			echo >> "$3" no "$n"
		fi
	fi
done

list_user_virtual_terminals | 
while read -r n
do
	system-control preset --ttys --prefix "cyclog@ttylogin@" -- "$n"
	system-control preset --ttys --prefix "ttylogin@" -- "$n"
	if system-control is-enabled "ttylogin@$n"
	then
		echo >> "$3" on "$n"
	else
		echo >> "$3" off "$n"
	fi
done

list_real_terminals | 
while read -r n
do
	if test -c "/dev/$n"
	then
		system-control preset --ttys --prefix "cyclog@ttylogin@" -- "$n"
		system-control preset --ttys --prefix "ttylogin@" -- "$n"
		if system-control is-enabled "ttylogin@$n"
		then
			echo >> "$3" on "$n"
		else
			echo >> "$3" off "$n"
		fi
	else
		redo-ifcreate "/dev/$n"
		if s="`system-control find \"cyclog@ttylogin@$n\" 2>/dev/null`"
		then
			system-control disable "$s"
		fi
		if s="`system-control find \"ttylogin@$n\" 2>/dev/null`"
		then
			system-control disable "$s"
			echo >> "$3" off "$n"
		else
			echo >> "$3" no "$n"
		fi
	fi
done

list_keyboard_maps |
while read -r b
do
	cc="${b%%.*}"
	option="${b#${cc}}"
	for keys in 104 105 106 107 109
	do
		redo-ifchange kbdmaps/"${cc}"."${keys}""${option}"
		redo-ifchange kbdmaps/"${cc}"."${keys}""${option}".capsctrl
	done
done

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_var() { read_rc "$1" || true ; }

redo-ifchange rc.conf

if ! keymap="`read_rc \"keymap\"`"
then
	keymap="us"
fi
cc="${keymap%%.*}"
option="${keymap#${cc}}"

# There are a whole bunch of old names for keyboard mappings that systems could be using.
# Note that this is NOT the same as the modernizations done by the /etc/rc.d system in the syscons to vt conversion.
# These are the names built in our kbdmaps directory.
case "${cc}" in
hy)			cc="am";;
br275)			cc="br";;
fr_CA)			cc="ca-fr";;
swissgerman)		cc="ch";;
swissfrench)		cc="ch-fr";;
ce)			cc="centraleuropean";;
colemak)		cc="colemak";;
cs)			cc="cz";;
german)			cc="de";;
danish)			cc="dk";;
estonian)		cc="ee";;
spanish)		cc="es";;
finnish)		cc="fi";;
el)			cc="gr";;
gb)			cc="uk";;
iw)			cc="il";;
icelandic)		cc="is";;
kk)			cc="kz";;
norwegian)		cc="no";;
dutch)			cc="nl";;
pl_PL)			cc="pl";;
swedish)		cc="se";;
eee_nordic)		cc="nordic";option=".asus-eee";;
esac
case "${cc}" in
ko)			keys="106";;
br)			keys="107";;
jp)			keys="109";;
us)			keys="104";;
*)			keys="105";;
esac
case "${option}" in
.armscii-8)		option="";;
.iso*.acc)
	case "${cc}" in
	br)		option="";;
	nl)		option="";;
	*)		option=".acc";;
	esac
	;;
.us101.acc)		option=".acc";keys="104";;
.macbook.acc)		option=".acc";;
.iso*.macbook)		option=".macbook";;
.iso2.101keys)		option="";keys="104";;
.iso*)
	case "${cc}" in
	br)		option=".noacc";;
	nl)		option=".noacc";;
	*)		option="";;
	esac
	;;
.pt154.io)		option=".io";;
.pt154.kst)		option=".kst";;
.106x)			option=".capsctrl";keys="109";;
.*-ctrl)		option=".capsctrl";;
.bds.ctrlcaps)		option=".bds.capsctrl";;
.phonetic.ctrlcaps)	option=".phonetic.capsctrl";;
.ISO8859-2)		option="";;
.koi8-r.shift)		option=".shift";;
.koi8-r.win)		option=".win";;
.koi8-u.shift.alt)	option=".shift.alt";;
.iso2)			option=".qwerty";;
esac

list_heads |
while read -r n
do
	case "`uname`" in
	Linux)
		set_if_unset console-fb-realizer@"$n" KERNEL_VT "1"
		set_if_unset console-fb-realizer@"$n" FRAMEBUFFER "/dev/fb0"
		set_if_unset console-fb-realizer@"$n" INPUTS "--ps2mouse /dev/input/mice"
		set_if_unset console-fb-realizer@"$n" OWNED_DEVICES "/dev/input/event0 /dev/input/mice /dev/tty1 /dev/fb0"
		;;
	*BSD)
		set_if_unset console-fb-realizer@"$n" KERNEL_VT "ttyv0"
		set_if_unset console-fb-realizer@"$n" INPUTS "--sysmouse /dev/sysmouse"
		set_if_unset console-fb-realizer@"$n" OWNED_DEVICES "/dev/uhid0 /dev/sysmouse /dev/ttyv0"
		set_if_unset console-fb-realizer@"$n" DETACHED_UGEN_DEVICES ""
		;;
	esac

	set_if_unset console-input-method@"$n" lower /run/dev/"$n"mux
	set_if_unset console-input-method@"$n" upper /run/dev/"$n"

	svcdir="`system-control find console-multiplexor@\"$n\"`"
        if ! test -e "${svcdir}/service/provisioned"
        then
                for vc in vc1 vc2 vc3
                do
                        ln -f -s "/run/dev/${vc}" "${svcdir}/service/"
                done
                touch -- "${svcdir}/service/provisioned"
        fi

	svcdir="`system-control find console-fb-realizer@\"$n\"`"
	list_keyboard_maps |
	while read -r b
	do
		cc="${b%%.*}"
		option="${b#${cc}}"
		for keys in 104 105 106 107 109
		do
			ln -f -s "`pwd`/kbdmaps/${cc}"."${keys}""${option}" "${svcdir}/service/kbdmaps/"
			ln -f -s "`pwd`/kbdmaps/${cc}"."${keys}""${option}".capsctrl "${svcdir}/service/kbdmaps/"
		done
	done

	system-control set-service-env console-fb-realizer@"$n" "KBDMAP" "kbdmaps/${cc}.${keys}${option}"

	printf >> "$3" "%s:\n" console-fb-realizer@"$n"
	system-control print-service-env console-fb-realizer@"$n" >> "$3"
done
