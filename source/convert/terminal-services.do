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

case "`uname`" in
Linux)
	set_if_unset console-fb-realizer@head0 KERNEL_VT "1"
	set_if_unset console-fb-realizer@head0 FRAMEBUFFER "/dev/fb0"
	set_if_unset console-fb-realizer@head0 KBDMAP kbdmaps/us.kbdmap
	set_if_unset console-fb-realizer@head0 INPUTS "--ps2mouse /dev/input/mice"
	set_if_unset console-fb-realizer@head0 OWNED_DEVICES "/dev/input/event0 /dev/input/mice /dev/tty1 /dev/fb0"
	;;
*BSD)
	set_if_unset console-fb-realizer@head0 KERNEL_VT "ttyv0"
	set_if_unset console-fb-realizer@head0 KBDMAP kbdmaps/us.kbdmap
	set_if_unset console-fb-realizer@head0 INPUTS "--sysmouse /dev/sysmouse"
	set_if_unset console-fb-realizer@head0 OWNED_DEVICES "/dev/uhid0 /dev/sysmouse /dev/ttyv0"
	set_if_unset console-fb-realizer@head0 DETACHED_UGEN_DEVICES ""
	;;
esac

keymaps="/usr/share/vt/keymaps/"
svcdir="`system-control find console-fb-realizer@head0`"
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
		esac
		redo-ifchange kbdmaps/"$b".kbdmap
		ln -f -s "`pwd`/kbdmaps/$b".kbdmap "${svcdir}/service/kbdmaps/"
		redo-ifchange kbdmaps/"$b".capsctrl.kbdmap
		ln -f -s "`pwd`/kbdmaps/$b".capsctrl.kbdmap "${svcdir}/service/kbdmaps/"
	done
else
	for b in us uk
	do
		redo-ifchange kbdmaps/"$b".kbdmap
		ln -f -s "`pwd`/kbdmaps/$b".kbdmap "${svcdir}/service/kbdmaps/"
		redo-ifchange kbdmaps/"$b".capsctrl.kbdmap
		ln -f -s "`pwd`/kbdmaps/$b".capsctrl.kbdmap "${svcdir}/service/kbdmaps/"
	done
fi

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
			for j in 0 1 2 3 4 5 6 7 8 9 a b ; do echo tty$i$j ; done 
		done
		;;
	*BSD)
		for i in 0 1 2 3 4 5 6 7 8 9 a b c d e f ; do echo ttyv$i ; done 
		;;
	esac
}

list_real_terminals() {
	case "`uname`" in
	# Linux is technically /dev/ttyS[0-9]* , but no-one has that many real terminal devices nowadays.
	Linux) 
		seq 0 99 | sed -e 's:^:ttyS:'
		seq 0 99 | sed -e 's:^:ttyACM:'
		;;
	OpenBSD)
		for i in 0 1 2 3 3 ; do echo ttyU$i ; done
		for i in 0 1 2 3 4 5 6 7
		do 
			for j in 0 1 2 3 4 5 6 7 8 9 a b c d e f ; do echo tty$i$j ; done
		done
		;;
	*BSD)
		for i in 0 1 2 3 4 5 6 7 8 9 a b c d e f ; do echo ttyu$i ; done 
		;;
	esac
}

redo-ifchange "/dev"

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
