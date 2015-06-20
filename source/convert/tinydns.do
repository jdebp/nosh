#!/bin/sh -e
#
# Special setup for tinydns.
# This is invoked by general-services.do .
#

s="`system-control find tinydns`"

set_if_unset() { if test -z "`rcctl get \"$1\" \"$2\"`" ; then rcctl set "$1" "$2" "$3" ; echo "$s: Defaulted $2 to $3." ; fi ; }

set_if_unset tinydns IP 127.53.0.1
set_if_unset tinydns ROOT "$s/service/root"

rcctl get tinydns >> "$3"

mkdir -p -m 0755 "$s/service/root"

for i in alias childns ns mx host
do
	test -x "$s/service/root/add-$i" && continue
	echo '#!/command/execlineb -S0' > "$s/service/root/add-$i"
	echo "tinydns-edit data data.new add $i \$@" >> "$s/service/root/add-$i"
	chmod +x "$s/service/root/add-$i"
done

test -r "$s/service/root/data" || echo '.' > "$s/service/root/data"
test -r "$s/service/root/Makefile" || echo 'data.cdb : data ; tinydns-data' > "$s/service/root/Makefile"
