#!/bin/sh -e
name="$1"
base="`basename \"${name}\"`"

redo-ifchange system-control

case "${name}" in
*@*) 
	template="${name%%@*}"
	if test -e "${template}"@.target
	then
		unit="${name}".target
		unitfile="${template}"@.target
	else
		echo 1>&2 "$0: ${name}: Don't know what to use to build this."
		exit 1
	fi
	;;
*)
	if test -e "${name}".target
	then
		unit="${name}".target
	else
		echo 1>&2 "$0: ${name}: Don't know what to use to build this."
		exit 1
	fi
	unitfile="${unit}"
	;;
esac

redo-ifchange "${unitfile}"

mkdir -p targets.new

./system-control convert-systemd-units --etc-bundle --bundle-root targets.new/ "${unit}"

rm -r -f -- "$3"
mv -- targets.new/"${base}" "$3"
