#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for ldconfig.
# This is invoked by all.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf "`which printenv`" "$1" ; }
get_var1() { read_rc ldconfig"$1" || true ; }
get_var2() { read_rc ldconfig"$1" || read_rc ldconfig"$2" || true ; }

get_files() {
	local dirs
	for var
	do
		if path="`get_var1 ${var}`"
		then
			test -n "${path}" || continue
			for d in ${path}
			do
				test -n "$d" || continue
				if test -e "$d"
				then
					redo-ifchange "$d"
					if test -d "$d"
					then
						dirs="${dirs} $d"
					fi
				else
					redo-ifcreate "$d"
				fi
			done
		fi
	done
	if test -n "${dirs}"
	then
		find ${dirs} -type f -print0 | xargs -0 -r awk '!x[$0]++' | tr '\r\n' '  '
	fi
}

redo-ifchange rc.conf general-services

if s="`system-control find ldconfig`"
then
	system-control set-service-env "${s}" rc_paths_elf "/lib /usr/lib /usr/local/lib `get_var1 _paths` `get_files _local_dirs`"
	system-control set-service-env "${s}" rc_paths_elf32 "/lib /usr/lib /usr/local/lib `get_var1 _paths` `get_var1 32_paths` `get_files _local_dirs _local32_dirs`"
	system-control set-service-env "${s}" rc_paths_aout "/lib/aout /usr/lib/aout /usr/local/lib/aout `get_var2 _paths_aout _paths`"

	system-control preset -- "${s}"
	if system-control is-enabled "${s}"
	then
		echo >> "$3" on "${s}"
	else
		echo >> "$3" off "${s}"
	fi
	system-control print-service-env "${s}" >> "$3"
fi
