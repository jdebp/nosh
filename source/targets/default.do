#!/bin/sh -e
name="$1"
base="`basename \"${name}\"`"

ifchange_follow() {
	local i l
	for i
	do	
		while test -n "$i"
		do
			redo-ifchange "$i"
			l="`readlink \"$i\"`" || break
			case "$l" in
			/*)	i="$l" ;;
			*)	i="`dirname \"$i\"`/$l" || break ;;
			esac
		done
	done
}

# ###
# Work out what files are going to be used and declare build dependencies from them.
# ###

case "${base}" in
*@*) 
	template="${name%%@*}"
	if test -e "${template}"@.target
	then
		unit="${name}".target
		ifchange_follow "${template}"@.target
	else
		echo 1>&2 "$0: ${name}: Don't know what to use to build this."
		exit 1
	fi
	;;
*)
	if test -e "${name}".target
	then
		unit="${name}".target
		ifchange_follow "${name}".target
	else
		echo 1>&2 "$0: ${name}: Don't know what to use to build this."
		exit 1
	fi
	;;
esac

# ###
# Decide the parameters for the conversion tool, and the location of the service and (if any) its logging service.
# ###

escape=""
etc="--etc-bundle"

# ###
# Compile the source into a bundle.
# ###

install -d -m 0755 targets.new

rm -r -f targets.new/"${base}"

redo-ifchange system-control

./system-control convert-systemd-units --no-systemd-quirks --no-generation-comment ${etc} --bundle-root targets.new/ "${unit}"

# ###
# Add in the environment directory and UCSPI ruleset infrastructure, if required.
# ###

if grep -q "envdir env" targets.new/"${base}"/service/start targets.new/"${base}"/service/run targets.new/"${base}"/service/stop
then
	install -d -m 0755 targets.new/"${base}"/service/env
fi

# ###
# Populate the bundles with extra relationships, files, and directories peculiar to the services.
# ###

# ###
# Finalize
# ###

rm -r -f -- "$3"
mv -- targets.new/"${base}" "$3"
