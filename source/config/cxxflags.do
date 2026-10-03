#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
if test -n "${USE_INHERITED_COMPILER_VARIABLES}"
then
	# Bypass for some packagers.
	# If you set the wrong libraries, features, include paths, or language level you are on your own.
	cppflags="${CPPFLAGS}"
	cxxflags="${CXXFLAGS}"
	ldflags="${LDFLAGS}"
	cxx="${CXX}"
else
	# Normal auto-detection path.
	cppflags="-I . ${kqueue}"
	ldflags="-g -pthread"
	if cxx=$(command -v clang++)
	then
		cxxflags='-g -pthread -Os -Weverything -Wno-conversion -Wno-sign-conversion -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-missing-prototypes -Wno-weak-vtables -Wno-packed -Wno-padded -Wno-documentation-unknown-command -Wno-zero-length-array -Wno-non-virtual-dtor -Wno-global-constructors -Wno-exit-time-destructors -Wno-reserved-id-macro -integrated-as'
		cppflags="${cppflags}"' -D _LIBCPP_REMOVE_TRANSITIVE_INCLUDES -Weverything'
		major_version="`clang++ --version|sed -ne 's/^.*version *\([[:digit:]]*\)\..*$/\1/p'`"
		if test "${major_version}" -lt 3
		then
			std='gnu++98'
		elif test "${major_version}" -lt 14
		then
			std='gnu++11'
			cxxflags="${cxxflags}"' -Wno-c++98-compat -Wno-c++98-compat-pedantic'
		else
			std='gnu++17'
			cxxflags="${cxxflags}"' -Wno-c++98-compat -Wno-c++98-compat-pedantic'
		fi
		if test "${major_version}" -gt 3
		then
			cxxflags="${cxxflags}"' -Wno-suggest-destructor-override -Wno-suggest-override -Wno-disabled-macro-expansion'
		fi
		if test "${major_version}" -gt 14
		then
			cxxflags="${cxxflags}"' -Wno-unsafe-buffer-usage'
		fi
		cxxflags="${cxxflags}"' -std='"${std}"
	elif test _"`uname`" = _OpenBSD && cxx=$(command -v eg++)
	then
		cxxflags='-g -pthread -std=gnu++11 -Os -Wall -Wextra -Wshadow -Wcast-qual -Wsynth -Woverloaded-virtual -Wcast-align'
	elif cxx=$(command -v g++)
	then
		cxxflags='-g -pthread -std=gnu++11 -Os -Wall -Wextra -Wshadow -Wcast-qual -Wsynth -Woverloaded-virtual -Wcast-align'
	elif cxx=$(command -v owcc)
	then
		cxxflags='-g -pthread -Os -Wall -Wextra -Wc,-xs -Wc,-xr'
	elif cxx=$(command -v owcc.exe)
	then
		cxx='owcc.exe'
		cxxflags='-g -pthread -Os -Wall -Wextra -Wc,-xs -Wc,-xr'
	else
		echo 1>&2 "Cannot find clang++, g++, or owcc."
		exit 100
	fi
fi
case "`basename "$1"`" in
(cxx)
	echo "$cxx" > "$3"
	;;
(cppflags)
	echo "$cppflags" > "$3"
	;;
(cxxflags)
	echo "$cxxflags" > "$3"
	;;
(ldflags)
	echo "$ldflags" > "$3"
	;;
(*)
	echo 1>&2 "$1: No such target."
	exit 111
	;;
esac
true
