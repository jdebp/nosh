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

redo-ifchange "${unitfile}"

install -d -m 0755 targets.new

rm -r -f targets.new/"${base}"

./system-control convert-systemd-units --no-systemd-quirks --no-generation-comment --etc-bundle --bundle-root targets.new/ "${unit}"

if grep -q "envdir env" targets.new/"${base}"/service/start targets.new/"${base}"/service/run targets.new/"${base}"/service/stop
then
	install -d -m 0755 targets.new/"${base}"/service/env
fi

rm -r -f -- "$3"
mv -- targets.new/"${base}" "$3"
