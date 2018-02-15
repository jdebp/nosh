#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Convert mixer services for generic USB devices.
# This is invoked by all.do .
#

redo-ifchange /dev "mixer@.service" 

lr="/var/local/sv/"
e="--no-systemd-quirks --escape-instance --local-bundle --bundle-root"

list_mixer_devices() {
	seq 0 9 | 
	while read -r i
	do 
                echo /dev/mixer"$i"
	done
}

find "$lr/" -maxdepth 1 -type d -name 'mixer@*' -print0 |
xargs -0 system-control disable --
system-control disable mixer-log

for target in mixer
do
	system-control preset -- "${target}".target

	if system-control is-enabled "${target}.target"
	then
		echo >> "$3" on "${target}"
	else
		echo >> "$3" off "${target}"
	fi
#	system-control print-service-env "${target}.target" >> "$3"
done

list_mixer_devices | 
while read -r i
do
	n="`basename \"$i\"`"
	system-control convert-systemd-units $e "$lr/" "./mixer@$n.service"
	rm -f -- "$lr/mixer@$n/wants/cyclog@mixer"
	rm -f -- "$lr/mixer@$n/log"
	ln -s -- "../../../sv/mixer-log" "$lr/mixer@$n/log"
	install -d -m 0755 -- "$lr/mixer@$n/service/env"
	if ! test -e "$i"
	then
		if s="`system-control find \"mixer@$n\" 2>/dev/null`"
		then
			system-control disable "$s"
		fi
		redo-ifcreate "$i"
		echo >> "$3" no "$n"
	else
		system-control preset mixer-log
		system-control preset --prefix "mixer@" -- "$n"
		if system-control is-enabled "mixer@$n"
		then
			echo >> "$3" on "$n"
		else
			echo >> "$3" off "$n"
		fi
	fi
done
