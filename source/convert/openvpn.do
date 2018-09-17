#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for OpenVPN.
# This is invoked by all.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_var() { read_rc openvpn_"$1"_"$2" || read_rc openvpn_"$2" || true ; }

list_instances() {
	find "${config_dir}"/ -maxdepth 2 -name '*.conf' |
	while read -r i
	do 
		basename "$i" .conf
	done |
	awk '!x[$0]++'
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

redo-ifchange rc.conf general-services "openvpn-server@.service" "openvpn-client@.service" "openvpn-log@.service" "openvpn-otp@.service"

install -d -m 0755 -- "/var/log/openvpn/sv"

r="/var/local/sv"
e="--no-systemd-quirks --bundle-root"

find "$r/" -maxdepth 1 -type d \( -name 'openvpn@*' -o -name 'openvpn-client@*' -o -name 'openvpn-server@*' -o -name 'openvpn-otp@*' -o -name 'openvpn-log@*' \) -print0 |
xargs -0 system-control disable -- || true

config_dir="/etc/openvpn"

for config_dir in "/etc/openvpn" "/usr/share/openvpn" "/usr/local/etc/openvpn" "/usr/local/share/openvpn" ""
do
	test -n "${config_dir}" || exit 0
	if ! test -e "${config_dir}"
	then
		redo-ifcreate "${config_dir}"
		echo >>"$3" "${config_dir}" "does not exist."
		continue
	fi
	redo-ifchange "${config_dir}"
	if ! test -d "${config_dir}"
	then
		echo >>"$3" "${config_dir}" "is not valid."
		continue
	fi
	break
done

echo >>"$3" "Config in" "${config_dir}" "."

list_instances |
while read -r i
do
	# In the old system, there was one config file which was usually the client.
	# In the new system, client and server have separate config files.
	# Server mode is the "new" mode, so client bundle is the old-style fallback.
	# https://sourceforge.net/p/openvpn/mailman/message/33035101/
	# https://bugzilla.redhat.com/show_bug.cgi?id=1435831

	cli="openvpn-client@$i"
	srv="openvpn-server@$i"
	log="openvpn-log@$i"
	otp="openvpn-otp@$i"

	system-control convert-systemd-units $e "$r/" "./${srv}.service"
	rm -f -- "$r/${srv}/log"
	ln -s -- "../${log}" "$r/${srv}/log"
	rm -f -- "$r/${srv}/service/config"
	if test -e "${config_dir}/server/${i}.conf"
	then
		ln -s -- "${config_dir}/server/${i}.conf" "$r/${srv}/service/config"
		system-control preset -- "${srv}"
	else
		system-control disable -- "${srv}"
	fi

	system-control convert-systemd-units $e "$r/" "./${cli}.service"
	rm -f -- "$r/${cli}/log"
	ln -s -- "../${log}" "$r/${cli}/log"
	rm -f -- "$r/${cli}/service/secret.up"
	ln -s -- "../../${otp}/service/root/secret.up" "$r/${cli}/service/secret.up"
	rm -f -- "$r/${cli}/service/config"
	if test -e "${config_dir}/client/${i}.conf"
	then
		ln -s -- "${config_dir}/client/${i}.conf" "$r/${cli}/service/config"
		system-control preset -- "${cli}"
	elif test -e "${config_dir}/${i}.conf"
	then
		ln -s -- "${config_dir}/${i}.conf" "$r/${cli}/service/config"
		system-control preset -- "${cli}"
	else
		system-control disable -- "${cli}"
	fi

	system-control convert-systemd-units $e "$r/" "./${otp}.service"
	rm -f -- "$r/${otp}/log"
	ln -s -- "../${log}" "$r/${otp}/log"
	install -d -m 0700 "$r/${otp}/service/root"
	setfacl -m "u:openvpn-otp:rwx" "$r/${otp}/service/root" || setfacl -m "user:openvpn-otp:rwxpD::allow" "$r/${otp}/service/root" || :
	system-control preset -- "${otp}"

	for f in user_name user_key server_key
	do
		test \! -e "$r/${otp}/service/root/$f" || continue
		install -m 0400 /dev/null "$r/${otp}/service/root/$f"
		setfacl -m "u:openvpn-otp:r" "$r/${otp}/service/root/$f" || setfacl -m "user:openvpn-otp:r::allow" "$r/${otp}/service/root/$f" || :
	done

	system-control convert-systemd-units $e "$r/" "./${log}.service"
	rm -f -- "$r/${log}/main"
	ln -s -- "/var/log/openvpn/sv/$i" "$r/${log}/main"
	install -d -m 0755 -- "/var/log/openvpn/sv/$i"
	setfacl -m "u:openvpn-log:rwx" "/var/log/openvpn/sv/$i" || setfacl -m "user:openvpn-log:rwxpD::allow" "/var/log/openvpn/sv/$i" || :
	system-control preset -- "${log}"

	echo >> "$3"
	show_enable >> "$3" "${srv}" "${cli}" "${otp}" "${log}"
done
