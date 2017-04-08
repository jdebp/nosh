#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# This is called by a number of .do scripts that need to read "system" configuration settings.

freenas_db="/data/freenas-v1.db"
default_rc="/etc/defaults/rc.conf"

convert_freenas() {
	for i in "${default_rc}" /etc/rc.conf
	do
		if test -e "$i"
		then
			redo-ifchange "$i"
			cat "$i"
		else
			redo-ifcreate "$i"
		fi
	done

	redo-ifchange "${freenas_db}"

	# TODO: There are a lot more settings in the FreeNAS database to import.

	sqlite3 "${freenas_db}" "SELECT tun_var,tun_value FROM system_tunable WHERE tun_enabled = 1 AND tun_type = 'rc' ORDER BY id" |
	while read -r var val
	do
		# FIXME: This is a poor man's quoting mechanism, and isn't robust.
		printf "%s=\"%s\"" "${var}" "${val}"
	done
}

convert_with_sysrc() {
	redo-ifchange "${default_rc}"
	for i in `sysrc -n rc_conf_files`
	do
		if test -e "$i"
		then
			redo-ifchange "$i"
		else
			redo-ifcreate "$i"
		fi
	done
	# FIXME: This is a poor man's quoting mechanism, and isn't robust.
	sysrc -A | sed -e 's/: /="/' -e 's/$/"/'
}

get_default() { clearenv read-conf "${default_rc}" "`which printenv`" "$1" ; }

convert_longhand() {
	redo-ifchange "${default_rc}"
	rc_conf_files="`get_default rc_conf_files`"
	for i in $rc_conf_files
	do
		if test -e "$i"
		then
			redo-ifchange "$i"
		else
			redo-ifcreate "$i"
		fi
	done
	clearenv --keep-path sh -c "
	for i in \"${default_rc}\" $rc_conf_files ;
	do
		test -e \"$i\" && . \"$i\" ;
	done ;
	set
	"
}

# These get us *only* the operating system variables, safely.
read_os() { /bin/exec clearenv read-conf /etc/os-release "`which printenv`" "$1" ; }

convert_linux() {
	# Stuff that can be overriden by explicit lines in rc.conf comes first.
	printf "entropy_enable=%s\n" "DUMMY"
	case "`read_os ID`:`read_os VERSION_ID`" in
	arch:*|centos:*|rhel:*)
		printf "entropy_file=%s\n" "/var/lib/systemd/random-seed"
		;;
	debian:*|ubuntu:*)
		printf "entropy_file=%s\n" "/var/lib/urandom/random-seed"
		;;
	*)      	
		printf "entropy_file=%s\n" "/var/lib/urandom/random-seed"
		;;
	esac

	if test -r /etc/rc.conf
	then
		redo-ifchange /etc/rc.conf
		cat /etc/rc.conf
	fi

	# Now for stuff that always overrides rc.conf .
}

if test -r "${freenas_db}"
then
	convert_freenas > "$3"
	exit $?
fi

if type sysrc >/dev/null 2>&1 
then 
	convert_with_sysrc > "$3"
	exit $?
fi

if test -r "${default_rc}"
then
	convert_longhand > "$3"
	exit $?
fi

# This is a fairly arbitrary test.
if test -r /etc/os-release
then
	redo-ifchange /etc/os-release
	convert_linux > "$3"
	exit $?
fi

echo > "$3" '# no rc.conf file'
exit $?
