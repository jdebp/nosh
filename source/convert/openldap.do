#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for OpenLDAP.
# This is invoked by all.do .
#

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$1: Defaulted $2 to $3." ; fi ; }

redo-ifchange general-services

set_if_unset slapd SERVICES "ldap:/// ldapi:///"
set_if_unset slapd USER "openldap"
set_if_unset slapd GROUP "openldap"

system-control print-service-env slapd >> "$3"
