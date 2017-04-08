#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Special setup for OpenVPN.
# This is invoked by all.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf "`which printenv`" "$1" ; }
get_var() { read_rc openvpn_"$1"_"$2" || read_rc openvpn_"$2" || true ; }

list_instances() { for i in "${config_dir}"/*.conf ; do basename "$i" .conf ; done ; }

redo-ifchange rc.conf general-services "openvpn@.service" "openvpn-log@.service" "openvpn-otp@.service"

install -d -m 0755 -- "/var/log/openvpn/sv"

r="/var/local/sv"
e="--no-systemd-quirks --bundle-root"

find "$r/" -maxdepth 1 -type d -name 'openvpn@*' -print0 |
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

list_instances |
while read -r i
do
	service="openvpn@$i"
	log="openvpn-log@$i"
	otp="openvpn-otp@$i"

	system-control convert-systemd-units $e "$r/" "./${service}.service"
	rm -f -- "$r/${service}/log"
	ln -s -- "../${log}" "$r/${service}/log"
	rm -f -- "$r/${service}/service/config"
	ln -s -- "${config_dir}/${i}.conf" "$r/${service}/service/config"
	rm -f -- "$r/${service}/service/secret.up"
	ln -s -- "../../${otp}/service/root/secret.up" "$r/${service}/service/secret.up"

	system-control convert-systemd-units $e "$r/" "./${otp}.service"
	rm -f -- "$r/${otp}/log"
	ln -s -- "../${log}" "$r/${otp}/log"
	install -d -m 0700 "$r/${otp}/service/root"
	setfacl -m "u:openvpn-otp:rwx" "$r/${otp}/service/root" || setfacl -m "user:openvpn-otp:rwxpD::allow" "$r/${otp}/service/root" || :

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

	system-control preset "${service}" "${log}" "${otp}"
	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
	echo >> "$3"
done
