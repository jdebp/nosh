#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim:set filetype=sh:
#
# This is run at binary package compile time to decide which service source file to use.

if test -r /etc/os-release
then
	redo-ifchange command/build-helper /etc/os-release "`readlink -f /etc/os-release`"
	# These get us *only* the operating system variables, safely.
	read_os() { command/build-helper clearenv setenv "$1" "$2" read-conf /etc/os-release printenv "$1" ; }
elif test -r /usr/lib/os-release
then
	redo-ifcreate /etc/os-release
	redo-ifchange command/build-helper /usr/lib/os-release "`readlink -f /usr/lib/os-release`"
	# These get us *only* the operating system variables, safely.
	read_os() { command/build-helper clearenv setenv "$1" "$2" read-conf /usr/lib/os-release printenv "$1" ; }
else
	redo-ifcreate /etc/os-release /usr/lib/os-release
	# The os-release system has defined defaults.
	# Note that the os_version file is only consulted in the first place for Linux-based operating systems.
	read_os() { printf "%s\n" "$2" ; }
fi
printf "%s:%s\n" "`read_os ID linux`" "`read_os VERSION_ID \"\"`" > "$3"
exec true
