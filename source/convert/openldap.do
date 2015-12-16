#!/bin/sh -e
#
# Special setup for OpenLDAP.
# This is invoked by general-services.do .
#

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$1: Defaulted $2 to $3." ; fi ; }

set_if_unset slapd SERVICES "ldap:/// ldapi:///"
set_if_unset slapd USER "openldap"
set_if_unset slapd GROUP "openldap"

system-control print-service-env slapd >> "$3"
