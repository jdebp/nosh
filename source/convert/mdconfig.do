#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for mdconfig.
# This is invoked by all.do .
#

# This gets us *only* the configuration variables, safely.
dump_rc() { clearenv read-conf rc.conf printenv ; }
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_var1() { read_rc "$1" || true ; }
get_var2() { read_rc mdconfig_"$1"_"$2" || read_rc mdconfig_"$2" || true ; }

parse_config() {
	file=''
	kind=''
	set -- `getopt f:t:s: "$@"`
	while test $# -gt 0
	do
		case "$1" in
		--)	break;;
		-f)	shift; file="$1";;
		-t)	shift; kind="$1";;
		esac
		shift
	done
	test -n "${kind}" || test -z "${file}" || kind="vnode"
}

r="/etc/service-bundles/services"
e="--no-systemd-quirks --etc-bundle --bundle-root"

redo-ifchange rc.conf /etc/fstab mdconfig@.service newfs@.service populate_md@.service

find "$r/" -maxdepth 1 -type d \( -name 'mdconfig@*' -o -name 'newfs@-dev-md*' \) -print0 |
xargs -0 -r system-control disable --

# The md device list is determined by the list of mdconfig_md* rc.conf variables.
dump_rc |
grep -E 'mdconfig_md[[:digit:]]+=' |
while read -r mdline
do
	md="`expr \"${mdline}\" : 'mdconfig_\(md[0-9][0-9]*\)='`"
	test -n "${md}" || continue
	config="${mdline#*=}"

	parse_config ${config}

	owner="`get_var2 \"${md}\" owner`"
	perms="`get_var2 \"${md}\" perms`"
	files="`get_var2 \"${md}\" files`"
	cmand="`get_var2 \"${md}\" cmd`"
	ftype="`get_var2 \"${md}\" fstype`"
	newfs="`get_var2 \"${md}\" newfs`"

	if test "${file}" != "${file%.uzip}"
	then
		dev="-dev-${md}.uzip"
	else
		dev="-dev-${md}"
	fi

	# First the service bundle that creates the block special device.

	mdconfig_service="mdconfig@${dev}"

	system-control convert-systemd-units $e "$r/" "./${mdconfig_service}.service"
	mkdir -p -m 0755 "$r/${mdconfig_service}/service/env"
	rm -f -- "$r/${mdconfig_service}/log"
	ln -s -- "../sysinit-log" "$r/${mdconfig_service}/log"
	if test "${file}" != "${file%.uzip}"
	then
		rm -f -- "$r/${mdconfig_service}/wants/kmod@geom_uzip"
		ln -s -- "../../kmod@geom_uzip" "$r/${mdconfig_service}/wants/"
	fi
	if test -n "${file}"
	then
		directory="${file}"
		while test "${directory}" != '/'
		do
			directory="`dirname \"${directory}\"`"
			dir="`echo \"${directory}\" | tr '/' '-'`"
			rm -f -- "$r/${mdconfig_service}/after/mount@${dir}"
			ln -s -- "../../mount@${dir}" "$r/${mdconfig_service}/after/"
		done
	fi

	system-control set-service-env "${mdconfig_service}" flags "${config}"
	if test -n "${owner}"
	then
		system-control set-service-env "${mdconfig_service}" owner "${owner}"
	else
		system-control set-service-env "${mdconfig_service}" owner 
	fi
	if test -n "${perms}"
	then
		system-control set-service-env "${mdconfig_service}" perms "${perms}"
	else
		system-control set-service-env "${mdconfig_service}" perms 
	fi

	system-control enable "${mdconfig_service}"

	if system-control is-enabled "${mdconfig_service}"
	then
		echo >> "$3" on "${mdconfig_service}"
	else
		echo >> "$3" off "${mdconfig_service}"
	fi
	system-control print-service-env "${mdconfig_service}" >> "$3"

	# Next the service bundle that formats the block special device with a new filesystem.

	case "${kind}" in
	swap|malloc)
		newfs_service="newfs@${dev}"

		system-control convert-systemd-units $e "$r/" "./${newfs_service}.service"
		mkdir -p -m 0755 "$r/${newfs_service}/service/env"
		rm -f -- "$r/${newfs_service}/log"
		ln -s -- "../sysinit-log" "$r/${newfs_service}/log"

		if test -n "${newfs}"
		then
			system-control set-service-env "${newfs_service}" flags "${newfs}"
		else
			system-control set-service-env "${newfs_service}" flags
		fi

		system-control enable "${newfs_service}"

		if system-control is-enabled "${newfs_service}"
		then
			echo >> "$3" on "${newfs_service}"
		else
			echo >> "$3" off "${newfs_service}"
		fi
		system-control print-service-env "${newfs_service}" >> "$3"
		;;
	esac

	# The original mdconfig mechanism requires that an appropriate entry be separately added to /etc/fstab.
	# Because this is a compatibility import mechanism, this imposes the same requirement.
	# So we do not make mount@ or fsck@ service bundles here; and expect the separately added /etc/fstab entry to be processed elsewhere.
	# One minor difference with deferring to /etc/fstab is that it, not rc.conf, controls whether an fsck@ service bundle is enabled.
	# So remember not to set an fsck pass number on non-vnode and uzip-compressed vnode memory disc devices.

	if mnt="`system-control get-mount-where -- \"/dev/${md}\"`"
	then
		case "${kind}" in
		swap|malloc)
			directory="${mnt}"
			while test "${directory}" != '/'
			do
				dir="`echo \"${directory}\" | tr '/' '-'`"
				rm -f -- "$r/${newfs_service}/before/mount@${dir}"
				ln -s -- "../../mount@${dir}" "$r/${newfs_service}/before/"
				directory="`dirname \"${directory}\"`"
			done
			;;
		esac

		populate_service="populate_md@`echo ${mnt}|sed -e 's:/:-:g'`"

		system-control convert-systemd-units $e "$r/" "./${populate_service}.service"
		mkdir -p -m 0755 "$r/${populate_service}/service/env"
		rm -f -- "$r/${populate_service}/log"
		ln -s -- "../sysinit-log" "$r/${populate_service}/log"

		if test -n "${owner}"
		then
			system-control set-service-env "${populate_service}" owner "${owner}"
		else
			system-control set-service-env "${populate_service}" owner 
		fi
		if test -n "${perms}"
		then
			system-control set-service-env "${populate_service}" perms "${perms}"
		else
			system-control set-service-env "${populate_service}" perms 
		fi

		system-control enable "${populate_service}"

		if system-control is-enabled "${populate_service}"
		then
			echo >> "$3" on "${populate_service}"
		else
			echo >> "$3" off "${populate_service}"
		fi
		system-control print-service-env "${populate_service}" >> "$3"
	fi
done
