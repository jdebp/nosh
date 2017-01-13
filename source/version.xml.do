#!/bin/sh -e
cwd="`cd .. && /bin/pwd`"
base_plus_version="`basename "${cwd}"`"
base="`echo "${base_plus_version}" | sed -e 's/-[[:digit:]][[:alnum:].]*$//'`"
if test _"${base}" != _"${base_plus_version}"
then
	version="`echo "${base_plus_version}" | sed -e 's/^.*-\([[:digit:]][[:alnum:].]*\)$/\1/'`"
	echo > $3 '<refmiscinfo class="version">' "${version}" '</refmiscinfo>'
else
	echo > $3 '<refmiscinfo class="version">' 'unknown' '</refmiscinfo>'
fi
exit 0
