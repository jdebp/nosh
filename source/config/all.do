#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:

redo-ifchange config/cxx config/cxxflags config/cppflags config/ldflags config/ar config/arflags
grep '^' config/cxx config/cxxflags config/cppflags config/ldflags config/ar config/arflags >> "$3"
redo-ifchange version.h version.xml systemd_names_escape_char.h config/hasevdev.h config/haskenv.h config/haslogincap.h config/hasnmount.h config/haspam.h config/hasshadow.h config/hasutmpx.h config/hasutmp.h config/hasupdwtmpx.h config/hasutmpxaddr.h config/hasutmpxexit.h config/hasutmpxsession.h config/hasutmpxss.h config/hasvis.h config/haswscons.h
grep '^' version.h version.xml config/hasevdev.h config/haskenv.h config/haslogincap.h config/hasnmount.h config/haspam.h config/hasshadow.h config/hasutmpx.h config/hasutmp.h config/hasupdwtmpx.h config/hasutmpxaddr.h config/hasutmpxexit.h config/hasutmpxsession.h config/hasutmpxss.h config/hasvis.h config/haswscons.h >> "$3"
