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

list_user_virtual_terminals() {
	jot 3 1 | sed -e 's:^:vc:' -e 's:$:-tty:'
}

list_kernel_virtual_terminals() {
	case "`uname`" in
	(Linux)
		printf "tty%s\n" 1 2 3 4 5 6 7 8 9 10 11 12
		;;
	(OpenBSD)
		for i in C D E F G H I J
		do 
			printf "tty$i%s\n" 0 1 2 3 4 5 6 7 8 9 a b
		done
		;;
	(FreeBSD)
		printf "ttyv%s\n" 0 1 2 3 4 5 6 7 8 9 a b c d e f
		;;
	(NetBSD)
		for i in E F G H
		do 
			printf "tty$i%s\n" 0 1 2 3 4 5 6 7
		done
		;;
	esac
}

list_real_terminals() {
	case "`uname`" in
	(Linux) 
		# Linux is technically /dev/ttyS[0-9]* , but no-one has that many real terminal devices nowadays.
		jot 99 0 | sed -e 's:^:ttyS:'
		jot 99 0 | sed -e 's:^:ttyACM:'
		# These are special serial devices in several virtual machines.
		printf "%s\n" hvc0 xvc0 hvsi0 sclp_line0 ttysclp0 '3270!tty1'
		;;
	(OpenBSD)
		printf "ttyU%s\n" 0 1 2 3
		for i in 0 1 2 3 4 5 6 7
		do 
			printf "tty$i%s\n" 0 1 2 3 4 5 6 7 8 9 a b c d e f
		done
		;;
	(FreeBSD)
		printf "ttyu%s\n" 0 1 2 3 4 5 6 7 8 9 a b c d e f
		;;
	(NetBSD)
		printf "tty0%s\n" 0 1 2 3
		;;
	esac
}

show_enable() {
	local i
	for i
	do
		if system-control is-enabled "$i"
		then
			echo on "$i"
		else
			echo off "$i"
		fi
	done
}

show_settings() {
	local i
	for i
	do
		system-control print-service-env "${i}" | sed -e 's/^/	/'
	done
}

# These files/directories not existing is not an error; but is a reason to rebuild when they appear.
for i in /etc/ttys
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
	if ! test -e "/dev/$n"
	then
		redo-ifcreate "/dev/$n"
		continue
	elif test -c "/dev/$n"
	then
		system-control preset --ttys --prefix "cyclog@ttylogin@" -- "$n"
		system-control preset --ttys --prefix "ttylogin@" -- "$n"
		show_enable >> "$3" "ttylogin@$n" "cyclog@ttylogin@$n"
		system-control preset --prefix "cyclog@console-kvt-realizer@" -- "$n"
		system-control preset --prefix "console-kvt-realizer@" -- "$n"
		show_enable >> "$3" "console-kvt-realizer@$n" "cyclog@console-kvt-realizer@$n"
	else
		if s="`system-control find \"cyclog@ttylogin@$n\" 2>/dev/null`"
		then
			system-control disable -- "$s"
		fi
		if s="`system-control find \"ttylogin@$n\" 2>/dev/null`"
		then
			system-control disable -- "$s"
			echo >> "$3" off "ttylogin@$n"
		else
			echo >> "$3" no "ttylogin@$n"
		fi
		if s="`system-control find \"cyclog@console-kvt-realizer@$n\" 2>/dev/null`"
		then
			system-control disable -- "$s"
		fi
		if s="`system-control find \"console-kvt-realizer@$n\" 2>/dev/null`"
		then
			system-control disable -- "$s"
			echo >> "$3" off "console-kvt-realizer@$n"
		else
			echo >> "$3" no "console-kvt-realizer@$n"
		fi
	fi
	redo-ifdelete "/dev/$n"
done

list_user_virtual_terminals | 
while read -r n
do
	system-control preset --ttys --prefix "cyclog@ttylogin@" -- "$n"
	system-control preset --ttys --prefix "ttylogin@" -- "$n"
	show_enable >> "$3" "ttylogin@$n" "cyclog@ttylogin@$n"
done

list_real_terminals | 
while read -r n
do
	if ! test -e "/dev/$n"
	then
		redo-ifcreate "/dev/$n"
		continue
	elif test -c "/dev/$n"
	then
		system-control preset --ttys --prefix "cyclog@ttylogin@" -- "$n"
		system-control preset --ttys --prefix "ttylogin@" -- "$n"
		show_enable >> "$3" "ttylogin@$n" "cyclog@ttylogin@$n"
	else
		if s="`system-control find \"cyclog@ttylogin@$n\" 2>/dev/null`"
		then
			system-control disable -- "$s"
		fi
		if s="`system-control find \"ttylogin@$n\" 2>/dev/null`"
		then
			system-control disable -- "$s"
			echo >> "$3" off "ttylogin@$n"
		else
			echo >> "$3" no "ttylogin@$n"
		fi
	fi
	redo-ifdelete "/dev/$n"
done
