#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim:set filetype=sh:
#

# This is run by the system-wide external configuration import subsystem.
# It is used to auto-generate source files for service bundles, from D-Bus system-wide service definition files.

name="$1""$2"
busname="`basename \"$1\"`"

for p in /usr/local/share /usr/share /share
do
	if ! test -e "$p"
	then
		redo-ifcreate "$p"
		continue
	fi
	redo-ifchange "$p"
	test -d "$p" || continue
	d="$p/dbus-1/system-services"
	if ! test -e "$d"
	then
		redo-ifcreate "$d"
		continue
	fi
	redo-ifchange "$d"
	test -d "$d" || continue

	f="$d"/"`basename \"${name}\"`"
	if ! test -e "${f}"
	then
		redo-ifcreate "${f}"
		continue
	fi
	redo-ifchange "${f}"
	test -r "${f}" || continue

	e="`sed -n -e 's/^Exec=//p' \"${f}\"`"
	test -n "$e" || continue
	for p in /usr/local/sbin /usr/local/bin /usr/sbin /usr/bin /sbin /bin
	do
		e="${e#${p}/}"
	done
	test _"false" != _"${e}" || continue
	test _"true" != _"${e}" || continue

	cat > "$3" <<- EOT
	## **************************************************************************
	## File generated by processing ${f}
	## **************************************************************************

	[Unit]
	Description=Startable-on-demand Desktop Bus server ${busname} service

	[Service]
	Type=dbus
	EnvironmentDirectory=env
	Restart=on-abort
	ExecStart=${e}
	EOT
	exec true
done

echo 1>&2 "${name}: Do not know how to make a service for this."
exec false
