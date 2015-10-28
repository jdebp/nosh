#!/bin/sh -e
#[ "`uname`" = "FreeBSD" ] || kqueue=="-I /usr/include/kqueue"
cppflags="-I . ${kqueue}"
ldflags="-g -pthread"
if type >/dev/null clang++
then
	cxx="clang++"
	cxxflags="-g -pthread -std=gnu++11 -Os -Wall -Wextra -Wshadow -Wcast-qual -Wsynth -Woverloaded-virtual -Wcast-align -integrated-as"
elif type >/dev/null g++
then
	cxx="g++"
	cxxflags="-g -pthread -std=gnu++11 -Os -Wall -Wextra -Wshadow -Wcast-qual -Wsynth -Woverloaded-virtual -Wcast-align"
elif type >/dev/null owcc
then
	cxx="owcc"
	cxxflags="-g -pthread -Os -Wall -Wextra -Wc,-xs -Wc,-xr"
elif type >/dev/null owcc.exe
then
	cxx="owcc.exe"
	cxxflags="-g -pthread -Os -Wall -Wextra -Wc,-xs -Wc,-xr"
else
	echo 1>&2 "Cannot find clang++, g++, or owcc."
	exit 100
fi
case "`basename "$1"`" in
cxx)
	echo "$cxx" > "$3"
	;;
cppflags)
	echo "$cppflags" > "$3"
	;;
cxxflags)
	echo "$cxxflags" > "$3"
	;;
ldflags)
	echo "$ldflags" > "$3"
	;;
*)
	echo 1>&2 "$1: No such target."
	exit 111
	;;
esac
true
