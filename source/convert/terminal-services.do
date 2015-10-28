#!/bin/sh -e
#
# Convert the /etc/ttys external configuration format.
# This is invoked by all.do .
#
# Note that we do not look at "console".
# emergency-login@console intentionally cannot be disabled via this mechanism.
# And there is no ttylogin@console service to adjust, for various reasons.
# One reason is that there will be a service for the underlying device, be it a virtual or a real terminal.
#
# Design:
#   * The run-kernel-vt and run-user-vt packages use preset/disable in their post-install and post-deinstall scripts to enable/disable their particular login services.
#   * On the BSDs, there will always be an /etc/ttys file, defining presets for tty login services.
#   * On Linux, there will usually not be an /etc/ttys file, and everything will be considered preset on.
#     All user VT login services are thus always enabled.
#     Because kernel VT login services have no wanted-by, login services are started by the ttylogin-starter service, which uses reset.
# See the Nosh Guide for more information.
#

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$s: Defaulted $2 to $3." ; fi ; }

case "`uname`" in
Linux)
	set_if_unset console-fb-realizer@head0 KERNEL_VT "/dev/tty1"
	set_if_unset console-fb-realizer@head0 TTY "/dev/tty1"
	set_if_unset console-fb-realizer@head0 FRAMEBUFFER /dev/fb0
	set_if_unset console-fb-realizer@head0 KBDMAP kbdmaps/us.kbdmap
	set_if_unset console-fb-realizer@head0 KEYBOARD /dev/input/event0
	set_if_unset console-fb-realizer@head0 MOUSE /dev/input/event1
	;;
*BSD)
	set_if_unset console-fb-realizer@head0 TTY "/dev/ttyv0"
	set_if_unset console-fb-realizer@head0 FRAMEBUFFER /dev/ttyv0
	set_if_unset console-fb-realizer@head0 KBDMAP kbdmaps/us.kbdmap
	set_if_unset console-fb-realizer@head0 ATKEYBOARD /dev/ttyv0
	set_if_unset console-fb-realizer@head0 MOUSE /dev/uhid0
	;;
esac

keymaps="/usr/share/vt/keymaps/"
svcdir="`system-control find console-fb-realizer@head0`"
if test -d "${keymaps}"
then
	redo-ifchange "${keymaps}"
	find "${keymaps}" -name '*.kbd' |
	while read k
	do
		b="`basename \"$k\" .kbd`"
		case "$b" in
		*.pc98.*)	continue;;	# blacklisted
		*.pc98)		continue;;	# blacklisted
		*.caps)		continue;;	# unnecessary
		*.ctrl)		continue;;	# unnecessary
		*.capsctrl)	continue;;	# unnecessary
		esac
		redo-ifchange "$b".kbdmap
		ln -f -s "`pwd`/$b".kbdmap "${svcdir}/service/kbdmaps/"
		redo-ifchange "$b".capsctrl.kbdmap
		ln -f -s "`pwd`/$b".capsctrl.kbdmap "${svcdir}/service/kbdmaps/"
	done
else
	redo-ifcreate "${keymaps}"
	redo-ifchange soft_backspace.kbd us_to_uk.kbd
	console-convert-kbdmap /dev/null soft_backspace.kbd > us.kbdmap
	console-convert-kbdmap /dev/null soft_backspace.kbd us_to_uk.kbd > uk.kbdmap
	for i in us.kbdmap uk.kbdmap
	do
		chmod 0644 -- "$i"
		ln -f -s "`pwd`/$i" "${svcdir}/service/kbdmaps/"
	done
fi

list_user_virtual_terminals() {
	seq 1 3 | sed -e 's:^:/run/dev/vc:'
}

list_kernel_virtual_terminals() {
	case "`uname`" in
	Linux) seq 1 12 | sed -e 's:^:/dev/tty:' ;;
	*BSD) for i in 0 1 2 3 4 5 6 7 8 9 a b c d e f ; do echo /dev/ttyv$i ; done ;;
	esac
}

list_real_terminals() {
	return 0
	case "`uname`" in
	Linux) seq 0 9 | sed -e 's:^:/dev/ttyS:' ;;
	*BSD) for i in 0 1 2 3 4 5 6 7 8 9 a b c d e f ; do echo /dev/ttyu$i ; done ;;
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

list_kernel_virtual_terminals | while read i
do
	n="`basename \"$i\"`"
	if ! test -e "$i"
	then
		system-control disable "cyclog@ttylogin@$n"
		system-control disable "ttylogin@$n"
		redo-ifcreate "$i"
	else
		system-control preset --ttys --prefix "cyclog@ttylogin@" -- "$n"
		system-control preset --ttys --prefix "ttylogin@" -- "$n"
	fi
	if system-control is-enabled "ttylogin@$n"
	then
		echo >> "$3" on "$n"
	else
		echo >> "$3" off "$n"
	fi
done

list_user_virtual_terminals | while read i
do
	n="`basename \"$i\"`"
	system-control preset --ttys --prefix "cyclog@ttylogin@" -- "$n-tty"
	system-control preset --ttys --prefix "ttylogin@" -- "$n-tty"
	if system-control is-enabled "ttylogin@$n-tty"
	then
		echo >> "$3" on "$n-tty"
	else
		echo >> "$3" off "$n-tty"
	fi
done

list_real_terminals | while read i
do
	n="`basename \"$i\"`"
	if test -e "$i"
	then
		system-control preset --ttys --prefix "cyclog@serial-getty@" -- "$n"
		system-control preset --ttys --prefix "serial-getty@" -- "$n"
	else
		system-control disable "cyclog@serial-getty@$n"
		system-control disable "serial-getty@$n"
		redo-ifcreate "$i"
	fi
	if system-control is-enabled "serial-getty@$n"
	then
		echo >> "$3" on "$n"
	else
		echo >> "$3" off "$n"
	fi
done
