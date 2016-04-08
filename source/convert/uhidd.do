#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Convert uhidd services for generic USB devices.
# This is invoked by all.do .
#

redo-ifchange /dev "uhidd@.service" 

lr="/var/local/sv/"
e="--no-systemd-quirks --escape-instance --bundle-root"

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

for i in cyclog@uhidd uhidd kmod@vkbd
do
	test -e "$lr"/"$i" || ln -s -- ../../sv/"$i" "$lr"/
done

list_generic_USB_devices | 
while read -r i
do
	n="`basename \"$i\"`"
	if ! test -e "$i"
	then
		if system-control find "uhidd@$n" 2>/dev/null 
		then
			system-control disable "uhidd@$n"
		fi
		redo-ifcreate "$i"
		echo >> "$3" no "$n"
	else
		system-control convert-systemd-units $e "$lr/" "./uhidd@$n.service"
		rm -f -- "$lr/uhidd@$n/log"
		ln -s -- "../../cyclog@uhidd" "$lr/uhidd@$n/log"
		system-control preset --prefix "uhidd@" -- "$n"
		if system-control is-enabled "uhidd@$n"
		then
			echo >> "$3" on "$n"
		else
			echo >> "$3" off "$n"
		fi
	fi
done
