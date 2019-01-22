#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for MySQL, MariaDB, and Percona.
# This is invoked by all.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_var() { read_rc mysql_"$1"_"$2" || read_rc mysql_"$2" || true ; }

print_instance_options() { my_print_defaults --defaults-group-suffix="$1" safe_mysqld mysqld_safe server "mysqld$1" ; }
list_instances() {
	local l
	local i
	for i
	do
		if ! test -e "$i"
		then
			redo-ifcreate "$i"
			echo 1>&2 "$i" "does not exist."
			continue
		fi
		redo-ifchange "$i"
		if ! test -r "$i"
		then
			echo 1>&2 "$i" "is not valid."
			continue
		fi
		while read -r l
		do
			case "$l" in
			\[mysqld@*\]*)
				l="${l#\[mysqld@}"
				printf "%s\n" "${l%%]*}"
				;;
			\[mysqld[0-9]*\]*)
				l="${l#\[mysqld}"
				printf "%s\n" "${l%%]*}"
				;;
			\[mysqld\]*)
				printf "\n"
				;;
			\!includefile\ *)
				l="${l#!includefile }"
				list_instances "$l"
				;;
			\!includedir\ *)
				l="${l#!includedir }"
				if test -d "$l"
				then
					redo-ifchange "$l"
					list_instances "$l"/*.cnf
				fi
				;;
			esac
		done < "$i"
	done
}

redo-ifchange rc.conf general-services "mysql@.service"

r="/var/local/sv"
e="--no-systemd-quirks --bundle-root"

find "$r/" -maxdepth 1 -type d -name 'mysql@*' -print0 |
xargs -0 system-control disable --
find "$r/" -maxdepth 1 -type d -name 'mariadb@*' -print0 |
xargs -0 system-control disable --

install -d -m 0755 -- "/var/log/mysql/sv"

for etcdir in "/etc" "/etc/mysql" "/usr/local/etc" "/usr/local/etc/mysql"
do
	if ! test -e "${etcdir}"
	then
		redo-ifcreate "${etcdir}"
		echo >>"$3" "${etcdir}" "does not exist."
		continue
	fi
	if ! test -x "${etcdir}"/
	then
		redo-ifchange "${etcdir}"
		echo >>"$3" "${etcdir}" "is not valid."
		continue
	fi
	list_instances "${etcdir}/my.cnf" 2>>"$3"
done |
while read -r i
do
	service="mysql@$i"
	log="mysql-log@$i"

	case "`uname`" in
	Linux)	basedir="/usr" ;;
	*BSD)	basedir="/usr/local" ;;
	*)	echo 1>&2 "$0: Do not know the MySQL root directory for your system." ; exec false ;;
	esac
	ledir="${basedir}/libexec"
	plugindir="${basedir}/lib/mysql/plugin"
	datadir="`get_var \"$i\" dbdir`"
	if test -z "${datadir}"
	then
		if test -d "/var/db/mysql$i"
		then
			datadir="/var/db/mysql$i"
		else
			redo-ifcreate "/var/db/mysql$i"
			datadir="/var/lib/mysql$i"
		fi
	fi
	defaults_extra_file="`get_var \"$i\" optfile`"
	if test -z "${defaults_extra_file}"
	then
		if test -r "${datadir}/my.cnf" 
		then
			defaults_extra_file="${datadir}/my.cnf"
		else
			redo-ifcreate "${datadir}/my.cnf" 
		fi
	fi
	pid_file="`get_var \"$i\" pidfile`"
	if test -z "${pid_file}"
	then
		pid_file="/run/mysqld$i/mysqld.pid"
	fi
	mysqld=""

	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 -- "$r/${service}/service/env"
	rm -f -- "$r/${service}/log"
	ln -s -- "../${log}" "$r/${service}/log"

	install -d -m 0755 -o mysql-log -- "/var/log/mysql/sv/${service}"

	system-control convert-systemd-units $e "$r/" "./${log}.service"
	install -d -m 0755 -- "$r/${log}/service/env"
	rm -f -- "$r/${log}/main"
	ln -s -- "/var/log/mysql/sv/${service}" "$r/${log}/main"

	system-control set-service-env "${service}" defaults_file
	system-control set-service-env "${service}" port
	system-control set-service-env "${service}" socket "/run/mysqld$i/mysqld.sock"
	system-control set-service-env "${service}" user
	system-control set-service-env "${service}" nice
	system-control set-service-env "${service}" flush_caches
	system-control set-service-env "${service}" open_file_limit
	system-control set-service-env "${service}" core_file_size
	system-control set-service-env "${service}" numa_interleave
	system-control set-service-env "${service}" flags "`get_var \"$i\" args`"
	rm -f -- "$r/${service}/service/env/TZ"

	print_instance_options "$i" | (
		while read -r arg
		do
			case "${arg}" in

			# options that are recognised by mysqld but that require interception
			--basedir=*)
				basedir="${arg#--basedir=}"
				;;
			--datadir=*)
				datadir="${arg#--datadir=}"
				;;
			--plugin-dir=*)
				plugindir="${arg#--plugin-dir=}"
				;;
			--ledir=*)
				ledir="${arg#--ledir=}"
				;;
			--defaults-extra-file=*)
				defaults_extra_file="${arg#--defaults-extra-file=}"
				;;
			--port=*)
				system-control set-service-env "${service}" port "${arg#--port=}"
				;;
			--socket=*)
				system-control set-service-env "${service}" socket "${arg#--socket=}"
				;;
			--timezone=*)
				system-control set-service-env "${service}" TZ "${arg#--timezone=}"
				;;

			# options that were recognized and enacted by the old mysqld-safe wrapper not by mysqld itself
			--mysqld=*)
				mysqld="${arg#--mysqld=}"
				;;
			--user=*)
				system-control set-service-env "${service}" user "${arg#--user=}"
				;;
			--nice=*)
				system-control set-service-env "${service}" nice "${arg#--nice=}"
				;;
			--flush-caches=*)
				system-control set-service-env "${service}" flush_caches "${arg#--flush-caches=}"
				;;
			--open-files-limit=*|--open_files_limit=*)
				system-control set-service-env "${service}" open_file_limit "${arg#--open?files?limit=}"
				;;
			--core-file-size=*)
				system-control set-service-env "${service}" core_file_size "${arg#--core-file-size=}"
				;;
			--numa-interleave=*)
				numa_interleave="${arg#--numa-interleave=}"
				test -n "${numa_interleave}" && if test 0 -eq "${numa_interleave}"
				then
					numa_interleave=""
				else
					numa_interleave="all"
				fi
				system-control set-service-env "${service}" numa_interleave "${numa_interleave}"
				;;
			--pid-file=*)
				pid_file="${arg#--pid-file=}"
				;;
			--malloc-lib=*|--mysqld-version=*)
				echo >> "$3" "Ignoring unsupported ${arg}"
				;;
			--log-error=*|--skip-syslog|--syslog-tag=*|--skip-kill-mysqld*)
				echo >> "$3" "Ignoring inappropriate ${arg}"
				;;
			--syslog) 	# always in effect anyway
				;;

			# options that are used on the mysqld-safe command line only, which we should never see
			--help) ;;

			*)
				printf >> "$r/${service}/service/env/flags" "%s  " "${arg}"
				;;
			esac
		done

		test -n "${ledir}" || ledir="${basedir}/libexec"
		rm -f -- "$r/${service}/service/bin"
		rm -f -- "$r/${service}/service/sbin"
		rm -f -- "$r/${service}/service/share"
		rm -f -- "$r/${service}/service/libexec"
		rm -f -- "$r/${service}/service/data"
		rm -f -- "$r/${service}/service/plugin"
		ln -s -- "${basedir}/bin" "$r/${service}/service/bin"
		ln -s -- "${basedir}/sbin" "$r/${service}/service/sbin"
		ln -s -- "${basedir}/share" "$r/${service}/service/share"
		ln -s -- "${ledir}" "$r/${service}/service/libexec"
		ln -s -- "${datadir}" "$r/${service}/service/data"
		ln -s -- "${plugindir}" "$r/${service}/service/plugin"
#		system-control set-service-env "${service}" basedir "${basedir}"
		system-control set-service-env "${service}" defaults_extra_file "${defaults_extra_file}"
		system-control set-service-env "${service}" pid_file "${pid_file}"
		if test -z "${mysqld}"
		then
			for i in libexec/mysqld sbin/mysqld bin/mysqld
			do
				if ! test -e "$r/${service}/service/${i}"
				then
					redo-ifcreate "$r/${service}/service/${i}"
				elif ! test -x "$r/${service}/service/${i}"
				then
					redo-ifchange "$r/${service}/service/${i}"
				else
					mysqld="${i}"
					break
				fi
			done
		fi
		mysql_install_db="mysqld --initialize"
		for i in libexec/mysql_install_db sbin/mysql_install_db bin/mysql_install_db
		do
			if ! test -e "$r/${service}/service/${i}"
			then
				redo-ifcreate "$r/${service}/service/${i}"
				continue
			fi
			redo-ifchange "$r/${service}/service/${i}"
			if test -x "$r/${service}/service/${i}"
			then
				mysql_install_db="${i}"
				break
			fi
		done
		system-control set-service-env "${service}" mysqld "${mysqld}"
		system-control set-service-env "${service}" mysql_install_db "${mysql_install_db}"
	)

	system-control preset "${service}" "${log}"
	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
	system-control print-service-env "${service}" >> "$3"
	echo >> "$3"
done
