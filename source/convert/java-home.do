#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for java services.
# This is invoked by all.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }

redo-ifchange rc.conf general-services

case "`uname`" in
(Linux)		extended_regexp="-r" ; no_target_dir="-T" ;;
(OpenBSD)	extended_regexp="-E" ; no_target_dir="" ;;
(*BSD)		extended_regexp="-E" ; no_target_dir="-h" ;;
esac

version_of() {
	echo "$@" |
	sed ${extended_regexp} -e 's/^[^[:digit:]]*([[:digit:]])\.([[:digit:]])\.[[:digit:]][^[:digit:]]+.*$/\1.\2/' -e 's/^[^[:digit:]]*([[:digit:]])[^[:digit:].][^[:digit:]]+.*$/1.\1/'
}

match() {
	local s="$1"
	local i
	shift
	for i
	do
		test _"$s" != _"$i" || return 0
	done
	return $#
}

found=
for etc in /usr/local/etc /etc /usr/pkg/etc
do
	if ! test -e ${etc}/jvms
	then
		redo-ifcreate ${etc}/jvms
		continue;
	fi
	redo-ifchange ${etc}/jvms
	if ! test -d ${etc}/jvms
	then
		continue;
	fi
	found=${etc}

	# We obtain our information from the javavms database.

	list_jvms() { sed ${extended_regexp} -e 's/[[:space:]]*#.*$//' ${etc}/jvms ; }
	find_jvm() {
		list_jvms |
		while read -r j
		do
			n="`dirname \"$j\"`"
			n="`dirname \"$n\"`"
			n="`basename \"$n\"`"
			o=
			m=
			case "$n" in
			openjdk*)
				o=native
				m=openjdk
				;;
			linux-sun*)
				o=foreign
				m=sun
				;;
			linux-oracle*)
				o=foreign
				m=oracle
				;;
			esac
			v="`version_of \"$n\"`"
			match "$v" $1 || continue
			match "$o" $2 || continue
			match "$m" $3 || continue
			echo "$j"
		done |
		sort -V |
		tail -n 1
	}
	default_jvm() { list_jvms | head -n 1 ; }

	break
done

if test -z "${found}"
then
	for lib in /usr/local/lib /usr/pkg/lib /usr/lib /lib
	do
		if ! test -e ${lib}/jvm
		then
			redo-ifcreate ${lib}/jvm
			continue;
		fi
		redo-ifchange ${lib}/jvm
		if ! test -d ${lib}/jvm
		then
			continue;
		fi
		found=${lib}

		# We obtain our information from the Debian JVM directory.

		redo-ifchange ${lib}/jvm/*/bin
		list_jvms() { ls -d -- ${lib}/jvm/*/bin/java ; }
		find_jvm() {
			list_jvms |
			while read -r j
			do
				n="`dirname \"$j\"`"
				n="`dirname \"$n\"`"
				n="`basename \"$n\"`"
				o=
				m=
				case "$n" in
				(default-java)	
					continue
					;;
				(java-*-openjdk*)
					o=native
					m=openjdk
					;;
				(java-*-gcj*)
					o=native
					m=gnu
					;;
				(java-*-sun*)
					o=native
					m=sun
					;;
				(java-*-oracle*)
					o=native
					m=oracle
					;;
				(*)
					continue
					;;
				esac
				v="`version_of \"$n\"`"
				match "$v" $1 || continue
				match "$o" $2 || continue
				match "$m" $3 || continue
				echo "$j"
			done |
			sort -V |
			tail -n 1
		}
		if j="`read_rc java_home`"
		then
			default_jvm() { printf "%s/bin/java" "`read_rc java_home`" ; }
		elif test -x ${lib}/jvm/default-java/bin/java
		then
			redo-ifchange ${lib}/jvm/default-java/bin/java
			default_jvm() { echo ${lib}/jvm/default-java/bin/java ; }
		elif test -x /usr/local/bin/java
		then
			redo-ifchange /usr/local/bin/java
			default_jvm() { echo /usr/local/bin/java ; }
		elif test -x /usr/bin/java
		then
			redo-ifcreate /usr/local/bin/java
			redo-ifchange /usr/bin/java
			default_jvm() { echo /usr/bin/java ; }
		else
			redo-ifcreate /usr/bin/java /usr/local/bin/java
			default_jvm() { echo /bin/java ; }
		fi

		break
	done
fi

if test -z "${found}"
then
	# We obtain our information from rc.conf or fall back to a hardwired single JVM.

	if j="`read_rc java_home`"
	then
		find_jvm() { printf "%s/bin/java" "`read_rc java_home`" ; }
		list_jvms() { printf "%s/bin/java" "`read_rc java_home`" ; }
		default_jvm() { printf "%s/bin/java" "`read_rc java_home`" ; }
	elif test -x /usr/local/bin/java
	then
		redo-ifchange /usr/local/bin/java
		find_jvm() { echo /usr/local/bin/java ; }
		list_jvms() { echo /usr/local/bin/java ; }
		default_jvm() { echo /usr/local/bin/java ; }
	elif test -x /usr/bin/java
	then
		redo-ifcreate /usr/local/bin/java
		redo-ifchange /usr/bin/java
		find_jvm() { echo /usr/bin/java ; }
		list_jvms() { echo /usr/bin/java ; }
		default_jvm() { echo /usr/bin/java ; }
	else
		redo-ifcreate /usr/bin/java /usr/local/bin/java
		find_jvm() { echo /bin/java ; }
		list_jvms() { echo /bin/java ; }
		default_jvm() { echo /bin/java ; }
	fi
fi

specific_home() {
	local JAVA_HOME
	if JAVA_HOME="`system-control print-service-env \"$1\" \"JAVA_HOME\"`" && test -n "${JAVA_HOME}"
	then
		printf " %s: %s\n" "$1" "${JAVA_HOME}"
	else
		JAVA_HOME="`find_jvm \"$2\" \"$3\" \"$4\"`"
		JAVA_HOME="${JAVA_HOME%/bin/java}"
		system-control set-service-env "$1" "JAVA_HOME" "${JAVA_HOME:-/usr/local}"
		printf "+%s: %s\n" "$1" "${JAVA_HOME:-/usr/local}"
	fi
}

default_home() {
	local JAVA_HOME
	if JAVA_HOME="`system-control print-service-env \"$1\" \"JAVA_HOME\"`" && test -n "${JAVA_HOME}"
	then
		printf " %s: %s\n" "$1" "${JAVA_HOME}"
	else
		JAVA_HOME="`default_jvm`"
		JAVA_HOME="${JAVA_HOME%/bin/java}"
		system-control set-service-env "$1" "JAVA_HOME" "${JAVA_HOME:-/usr/local}"
		printf "+%s: %s\n" "$1" "${JAVA_HOME:-/usr/local}"
	fi
}

list_jvms | nl -p >> "$3"

if ! test -d /usr/local/etc
then
	redo-ifcreate /usr/local/etc
elif ! test -d /usr/local/etc/system-control
then
	redo-ifchange /usr/local/etc
	redo-ifcreate /usr/local/etc/system-control
else
	redo-ifchange /usr/local/etc /usr/local/etc/system-control

	l="/usr/local/etc/system-control/local-java-services"
	if ! test -e "${l}" 
	then
		cat >>"${l}" <<-EOT
		# Local services that need JAVA_HOME configured by external configuration import.
		#
		# Lines are of the form:
		#   default_home service-name
		#   specific_home service-name version-list type-list manufacturer-list
		#
		# Empty lists match everything.
		# versions are 1.6, 1.7, 1.8, and so on.
		# types are native and foreign.
		# manufacturers are openjdk, gnu, sun, and oracle.

		EOT
	fi

	redo-ifchange "${l}"
	. "${l}" >> "$3"
fi
