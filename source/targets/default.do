#!/bin/sh -e
name="$1"
base="`basename \"${name}\"`"

redo-ifchange system-control

case "${base}" in
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

test -n "${unitfile}" && redo-ifchange "${unitfile}"

mkdir -p targets.new

if test -n "${unitfile}"
then
	./system-control convert-systemd-units --no-systemd-quirks --etc-bundle --bundle-root targets.new/ "${unit}"
fi

rm -r -f -- "$3"
mv -- targets.new/"${base}" "$3"
