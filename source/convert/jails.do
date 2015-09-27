#!/bin/sh -e
#
# Convert the FreeBSD jails list external configuration formats.
# This is invoked by general-services.do .
#

conf=/etc/jail.conf
if ! test -e "${conf}"
then
	redo-ifcreate "${conf}"
	exit $?
fi

redo-ifchange "${conf}"
