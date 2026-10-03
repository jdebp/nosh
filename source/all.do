#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:

install -d -m 0755 command manual object library
redo-ifdelete -- command manual object library

# These are the choke points of the build system.
# They have high degrees of fan-in; so we explicitly ensure that they are up-to-date before building the many things that fan into them.
# This also ensures that if the build fails in these sub-trees, it only tries one path before failing instead of failing via multiple paths.
# They have high degrees of fan-out; so we build them serially in order that we do not waste job slots on blocked overlapping sub-trees.
redo-ifchange -- config/all
redo-ifchange -- library/builtins.a library/manager.a library/utils.a object/main-exec.o
redo-ifchange -- command/build-helper "version.xml"

exec redo-ifchange -- all-commands all-misc all-targets all-services
