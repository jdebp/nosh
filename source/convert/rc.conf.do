#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# This is called by a number of .do scripts that need to read "system" configuration settings.

freenas_db="/data/freenas-v1.db"
default_rc="/etc/defaults/rc.conf"

# This gets us *only* the variables, safely.
read_variable() { clearenv read-conf "$1" "`which printenv`" "$2" ; }
read_optional_variable() { read_variable "$1" "$2" || echo "$3" ; }

convert_hostname() {
	local f h
	for f in /etc/HOSTNAME /etc/hostname
	do
		if ! test -r "$f"
		then
			redo-ifcreate "$f"
			continue
		fi
		read -r h < "$f"
		printf "# Converted from %s:\n" "${f}"
		printf "hostname=\"%s\"\n" "${h}"
		return 0
	done
	for f in /etc/sysconfig/network /etc/conf.d/hostname /etc/default/hostname
	do
		if ! test -r "$f"
		then
			redo-ifcreate "$f"
			continue
		fi
		if h="`read_variable \"$f\" hostname`" || h="`read_variable \"$f\" HOSTNAME`"
		then
			printf "# Converted from %s:\n" "${f}"
			printf "hostname=\"%s\"\n" "${h}"
			return 0
		fi
	done
	return 0
}

find_dotconf_files() { 
	local d
	for d in /etc /usr/local/etc /usr/share /usr/local/share /lib /usr/lib /usr/local/lib
	do 
		test \! -d "$d/$1" || ( 
			cd "$d/$1" && find -maxdepth 1 -name '*.conf' -a \( -type l -o -type f \)
		) | 
		while read -r i 
		do 
			echo "$d/$1" "${i#./}" 
		done 
	done |
	awk '{ if (!x[$2]++) print $1"/"$2"\n"; }'
}

list_modules_linux() { 
	# These are fundamental.
	echo autofs
	echo ipv6
	echo unix
	# This is the systemd system, with several .d files full of module list files.
	find_dotconf_files modules-load.d | xargs grep -h -- '^[^;#]' 
}

found_fonts() {
	local f
	while read -r f
	do
		f="${f#./}"
		case "${f}" in
		*16.psf.gz|*16.fnt)
			printf "font8x16=\"%s\"\n" "${f}"
			;;
		*15.psf.gz|*15.fnt)
			printf "font8x15=\"%s\"\n" "${f}"
			;;
		*14.psf.gz|*14.fnt)
			printf "font8x14=\"%s\"\n" "${f}"
			;;
		*13.psf.gz|*13.fnt)
			printf "font8x13=\"%s\"\n" "${f}"
			;;
		*8.psf.gz|*8.fnt)
			printf "font8x8=\"%s\"\n" "${f}"
			;;
		*)
			printf "font=\"%s\"\n" "${f}"
			;;
		esac
	done
}

find_named_fonts() {
	case "`uname`" in
	Linux)
		cd /usr/share/consolefonts/ &&
		for i
		do
			find . -name "${i}".psf.gz
		done
		;;
	*)
		cd /usr/share/syscons/fonts/ &&
		for i
		do
			find . -name "${i}".fnt
		done
		cd /usr/share/vt/fonts/ &&
		for i
		do
			find . -name "${i}".fnt
		done
		;;
	esac | 
	found_fonts
}

find_matching_fonts() {
	local face="$1"
	local size="$2"
	case "`uname`" in
	Linux)
		cd /usr/share/consolefonts/ &&
		find . -name "Uni*-${face:-*}${size:-*}".psf.gz
		;;
	*)
		cd /usr/share/syscons/fonts/ &&
		find . -name "Uni*-${face:-*}${size:-*}".fnt
		cd /usr/share/vt/fonts/ &&
		find . -name "Uni*-${face:-*}${size:-*}".fnt
		;;
	esac | 
	found_fonts
}

convert_debian_console_settings() {
	local confile=/etc/default/console-setup
	if test -r "${confile}"
	then
		redo-ifchange "${confile}"
		printf "# Converted from %s:\n" "${confile}"
	else
		redo-ifcreate "${confile}"
		return
	fi
	local i

	local charmap
	if charmap="`read_variable \"${confile}\" CHARMAP`" && 
	   test _"UTF-8" != _"${charmap}" &&
	   test _"guess" != _"${charmap}"
	then
		echo 1>&2 $0: "${confile}": "${charmap}": Non-Unicode CHARMAP ignored.
	fi

	local codeset
	if codeset="`read_variable \"${confile}\" CODESET`" && 
	   test _"Uni2" != _"${codeset}" &&
	   test _"guess" != _"${codeset}"
	then
		echo 1>&2 $0: "${confile}": "${codeset}": Non-Unicode CODESET ignored.
	fi

	local mode
	if mode="`read_variable \"${confile}\" VIDEOMODE`"
	then
		printf "allscreens_flags=\"%s\"\n" "${mode}"
	fi

	local blanktime
	if blanktime="`read_variable \"${confile}\" BLANK_TIME`"
	then
		printf "blanktime=\"%s\"\n" "${blanktime}"
	fi

	local vesapowersave
	if vesapowersave="`read_variable \"${confile}\" BLANK_DPMS`"
	then
		printf "vesapowersave=\"%s\"\n" "${vesapowersave}"
	fi

	local vesapowerdown
	if vesapowerdown="`read_variable \"${confile}\" POWERDOWN_TIME`"
	then
		printf "vesapowerdown=\"%s\"\n" "${vesapowerdown}"
	fi

	local keymap layout variant
	if keymap="`read_variable \"${confile}\" KEYMAP`" ||
	   keymap="`read_variable \"${confile}\" KMAP`"
	then
		printf "keymap=\"%s\"\n" "${keymap}"
	elif layout="`read_variable \"${confile}\" XKBLAYOUT`"
	then
		variant="`read_variable \"${confile}\" XKBVARIANT || :`"
		printf "keymap=\"%s\"\n" "${layout}${variant:+.${variant}}"
	fi

	local rate delay
	if rate="`read_variable \"${confile}\" KEYBOARD_RATE`"
	then
		delay="`read_variable \"${confile}\" KEYBOARD_DELAY || :`"
		printf "keyrate=\"%s\"\n" "${rate}${delay:+.${delay}}"
	fi

	local scrnmap
	if scrnmap="`read_variable \"${confile}\" CONSOLE_MAP`"
	then
		printf "scrnmap=\"%s\"\n" "${scrnmap}"
	fi

	local font face size
	if font="`read_variable \"${confile}\" FONT`"
	then
		find_named_fonts ${font}
	elif face="`read_variable \"${confile}\" FONTFACE`" ||
	     size="`read_variable \"${confile}\" FONTSIZE`"
	then
		# The file names do not incorporate the complete size specifications.
		case "${size}" in
		8x*)	size="${size#8x}" ;;
		*x8)	size="${size%x8}" ;;
		*x*)	
			a="${size%x*}"
			b="${size%x*}"
			if test "$a" -lt "$b"
			then
				size="${a}x${b}"
			else
				size="${b}x${a}"
			fi
			;;
		esac

		find_matching_fonts "${face}" "${size}"
	fi

	local fontmap
	if fontmap="`read_variable \"${confile}\" FONT_MAP`"
	then
		printf "fontmap=\"%s\"\n" "${fontmap}"
	fi
}

convert_systemd_console_settings() {
	local vccfile=/etc/vconsole.conf
	if test -r "${vccfile}"
	then
		redo-ifchange "${vccfile}"
		printf "# Converted from %s:\n" "${vccfile}"
	else
		redo-ifcreate "${vccfile}"
		return
	fi
	local i

	local keymap
	if keymap="`read_variable \"${vccfile}\" KEYMAP`"
	then
		printf "keymap=\"%s\"\n" "${keymap}"
	fi

	local font
	if font="`read_variable \"${vccfile}\" FONT`"
	then
		(
		cd /usr/share/consolefonts/ &&
		for i in ${font}
		do
			find . -name "${i}".psf.gz | found_linux_fonts
		done
		)
	fi

	local fontmap
	if fontmap="`read_variable \"${vccfile}\" FONT_MAP`"
	then
		printf "fontmap=\"%s\"\n" "${fontmap}"
	fi

	local scrnmap
	if scrnmap="`read_variable \"${vccfile}\" FONT_UNIMAP`"
	then
		printf "scrnmap=\"%s\"\n" "${scrnmap}"
	fi
}

convert_linux_kbdconf_console_settings() {
	local kbdfile=/etc/kbd/config
	if test -r "${kbdfile}"
	then
		redo-ifchange "${kbdfile}"
		printf "# Converted from %s:\n" "${kbdfile}"
	else
		redo-ifcreate "${kbdfile}"
		return
	fi
	local i

	local blanktime
	if blanktime="`read_variable \"${kbdfile}\" BLANK_TIME`"
	then
		printf "blanktime=\"%s\"\n" "${blanktime}"
	fi

	local vesapowersave
	if vesapowersave="`read_variable \"${kbdfile}\" BLANK_DPMS`"
	then
		printf "vesapowersave=\"%s\"\n" "${vesapowersave}"
	fi

	local vesapowerdown
	if vesapowerdown="`read_variable \"${kbdfile}\" POWERDOWN_TIME`"
	then
		printf "vesapowerdown=\"%s\"\n" "${vesapowerdown}"
	fi

	local keymap
	if keymap="`read_variable \"${kbdfile}\" KMAP`"
	then
		printf "keymap=\"%s\"\n" "${keymap}"
	fi

	local rate delay
	if rate="`read_variable \"${kbdfile}\" KEYBOARD_RATE`"
	then
		delay="`read_variable \"${kbdfile}\" KEYBOARD_DELAY || :`"
		printf "keyrate=\"%s\"\n" "${rate}${delay:+.${delay}}"
	fi

	local scrnmap
	if scrnmap="`read_variable \"${kbdfile}\" CONSOLE_MAP`"
	then
		printf "scrnmap=\"%s\"\n" "${scrnmap}"
	fi

	local font
	if font="`read_variable \"${kbdfile}\" CONSOLE_FONT`"
	then
		(
		cd /usr/share/consolefonts/ &&
		for i in ${font}
		do
			find . -name "${i}".psf.gz | found_linux_fonts
		done
		)
	fi

	local fontmap
	if fontmap="`read_variable \"${kbdfile}\" FONT_MAP`"
	then
		printf "fontmap=\"%s\"\n" "${fontmap}"
	fi
}

convert_linux_defkbd_console_settings() {
	local kbdfile=/etc/default/keyboard
	if test -r "${kbdfile}"
	then
		redo-ifchange "${kbdfile}"
		printf "# Converted from %s:\n" "${kbdfile}"
	else
		redo-ifcreate "${kbdfile}"
		return
	fi
	local i

	local keymap layout variant
	if keymap="`read_variable \"${kbdfile}\" KMAP`"
	then
		printf "keymap=\"%s\"\n" "${keymap}"
	elif layout="`read_variable \"${kbdfile}\" XKBLAYOUT`"
	then
		variant="`read_variable \"${kbdfile}\" XKBVARIANT || :`"
		printf "keymap=\"%s\"\n" "${layout}${variant:+.${variant}}"
	fi

	local rate delay
	if rate="`read_variable \"${kbdfile}\" KEYBOARD_RATE`"
	then
		delay="`read_variable \"${kbdfile}\" KEYBOARD_DELAY || :`"
		printf "keyrate=\"%s\"\n" "${rate}${delay:+.${delay}}"
	fi
}

convert_longhand() {
	if test -e "${default_rc}"
	then
		redo-ifchange "${default_rc}"
	else
		redo-ifcreate "${default_rc}"
		printf '# no %s file.\n' "${default_rc}"
	fi
	if type sysrc >/dev/null 2>&1 
	then 
		for i in `sysrc -n rc_conf_files`
		do
			if test -e "$i"
			then
				redo-ifchange "$i"
			else
				redo-ifcreate "$i"
			fi
		done
		printf '# dump by sysrc:\n'
		# FIXME: This is a poor man's quoting mechanism, and isn't robust.
		sysrc -A | sed -e 's/: /="/' -e 's/$/"/'
	else
		if test -e "${default_rc}"
		then
			rc_conf_files="`read_variable \"${default_rc}\" rc_conf_files`"
		else
			rc_conf_files="/etc/rc.conf"
		fi
		printf '# combined %s:\n' ${rc_conf_files}
		for i in $rc_conf_files
		do
			if test -e "$i"
			then
				redo-ifchange "$i"
			else
				redo-ifcreate "$i"
			fi
		done
		case "`uname`" in
		Linux|OpenBSD)
			for i in "${default_rc}" $rc_conf_files
			do
				! test -e "$i" || cat "$i"
			done
			;;
		*)
			clearenv --keep-path sh -c "
			for i in \"${default_rc}\" $rc_conf_files ;
			do
				! test -e \"$i\" || . \"$i\" ;
			done ;
			set
			"
			;;
		esac
	fi
}

convert_freenas() {
	convert_hostname
	convert_debian_console_settings
	convert_systemd_console_settings
	convert_longhand

	redo-ifchange "${freenas_db}"

	# TODO: There are a lot more settings in the FreeNAS database to import.

	sqlite3 "${freenas_db}" "SELECT tun_var,tun_value FROM system_tunable WHERE tun_enabled = 1 AND tun_type = 'rc' ORDER BY id" |
	while read -r var val
	do
		# FIXME: This is a poor man's quoting mechanism, and isn't robust.
		printf "%s=\"%s\"" "${var}" "${val}"
	done
}

convert_linux() {
	local m
	local os_version
	
	os_version="`read_variable \"$1\" ID`:`read_optional_variable \"$1\" VERSION_ID '*'`"

	#####
	# Stuff that can be overriden by explicit lines in rc.conf comes first.

	printf "entropy_enable=%s\n" "DUMMY"
	case "${os_version}" in
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
	if m="`which dhclient 2>&1`"
	then
		printf "dhclient_program=%s\n" "${m}"
	elif m="`which dhcpcd5 2>&1`"
	then
		printf "dhclient_program=%s\n" "${m}"
	fi
	m="`list_modules_linux | tr '\r\n' ' '`"
	printf "kld_list=\"%s\"\n" "${m}"

	convert_hostname

	# In order, so that systemd overrides kbd overrides Debian.
	convert_debian_console_settings
	convert_linux_kbdconf_console_settings
	convert_linux_defkbd_console_settings
	convert_systemd_console_settings

	#####
	# Now rc.conf itself.

	convert_longhand

	#####
	# Now for stuff that always overrides rc.conf .
}

convert_any() {
	convert_hostname
	convert_debian_console_settings
	convert_systemd_console_settings
	convert_longhand
}

if test -r "${freenas_db}"
then
	convert_freenas > "$3"
	exit $?
fi

# This is a fairly arbitrary test.
if test -r /etc/os-release
then
	redo-ifchange /etc/os-release "`readlink -f /etc/os-release`"
	convert_linux /etc/os-release > "$3"
	exit $?
elif test -r /usr/lib/os-release
then
	redo-ifcreate /etc/os-release
	redo-ifchange /usr/lib/os-release
	convert_linux /usr/lib/os-release > "$3"
	exit $?
elif test _"`uname`" == _"Linux"
then
	convert_linux /dev/null > "$3"
	exit $?
fi

convert_any > "$3"
exit $?
