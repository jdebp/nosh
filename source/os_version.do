#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim:set filetype=sh:
#
# This is run at binary package compile time to decide which service source file to use.

if test -r /etc/os-release
then
	redo-ifchange exec /etc/os-release "`readlink -f /etc/os-release`"
	# These get us *only* the operating system variables, safely.
	read_os() { ./exec clearenv setenv "$1" "$2" read-conf /etc/os-release printenv "$1" ; }
elif test -r /usr/lib/os-release
then
	redo-ifcreate /etc/os-release
	redo-ifchange exec /usr/lib/os-release
	# These get us *only* the operating system variables, safely.
	read_os() { ./exec clearenv setenv "$1" "$2" read-conf /usr/lib/os-release printenv "$1" ; }
else
	redo-ifcreate /etc/os-release
	redo-ifcreate /usr/lib/os-release
	# The os-release system has defined defaults.
	read_os() { printf "%s\n" "$2" ; }
fi
printf "%s:%s\n" "`read_os ID linux`" "`read_os VERSION_ID`" > "$3"
exec true
