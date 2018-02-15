#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Convert uhidd services for generic USB devices.
# This is invoked by all.do .
#

redo-ifchange /dev "uhidd@.service" 

lr="/var/local/sv/"
e="--no-systemd-quirks --escape-instance --local-bundle --bundle-root"

list_generic_USB_devices() {
	seq 0 9 | 
	while read -r i
	do 
		seq 0 9 | 
		while read -r j
		do 
			echo /dev/ugen"$i"."$j"
		done
	done
}
is_hid() {
	usbconfig -d "$1" | grep -E -q ' uhid[[:digit:]]+:'
}

for i in kmod@vkbd
do
	test -L "$lr"/"$i" || ln -s -- ../../sv/"$i" "$lr"/
done

find "$lr/" -maxdepth 1 -type d -name 'uhidd@*' -print0 |
xargs -0 system-control disable --
system-control disable uhidd-log

for target in uhidd
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

list_generic_USB_devices | 
while read -r i
do
	n="`basename \"$i\"`"
	system-control convert-systemd-units $e "$lr/" "./uhidd@$n.service"
	rm -f -- "$lr/uhidd@$n/wants/cyclog@uhidd"
	rm -f -- "$lr/uhidd@$n/log"
	ln -s -- "../../../sv/uhidd-log" "$lr/uhidd@$n/log"
	install -d -m 0755 -- "$lr/uhidd@$n/service/env"
	if ! test -e "$i"
	then
		if s="`system-control find \"uhidd@$n\" 2>/dev/null`"
		then
			system-control disable "$s"
		fi
		redo-ifcreate "$i"
		echo >> "$3" no "$n"
	elif is_hid "$n"
	then
		if s="`system-control find \"uhidd@$n\" 2>/dev/null`"
		then
			system-control disable "$s"
		fi
		echo >> "$3" kernel "$n"
	else
		system-control preset uhidd-log
		system-control preset --prefix "uhidd@" -- "$n"
		if system-control is-enabled "uhidd@$n"
		then
			echo >> "$3" on "$n"
		else
			echo >> "$3" off "$n"
		fi
	fi
done
