#!/bin/sh -e
redo-ifchange session-manager
ln -s -f session-manager "$3"
