.PHONY: all

all: data.cdb

data.cdb: data
	tinydns-data

# https://cr.yp.to/dnsroot.html

.SUFFIXES: .data .zone

.zone.data: convert.awk
	@echo  > $@.tmp '##########################################################################'
	@echo >> $@.tmp '#' $@ 'is a generated file; do not edit it directly.'
	@echo >> $@.tmp '#' Delete $@.zone 'to cause a re-fetch from the primary.'
	@echo >> $@.tmp '##########################################################################'
	awk -F : -f convert.awk -v root=$* $< >> $@.tmp
	for d in ${TLDS} ; do \
		sed -e 's/^[Z&.]'$$d':/# skipped &/' $@.tmp > $@.tmp.tmp || return $$? ; \
		mv -f -- $@.tmp.tmp $@.tmp || return $$? ; \
	done
	mv -f -- $@.tmp $@

# Really the root data should be more special-cased than this, to remove overlaps and prefer the information obtained from the subdomain owners.
root.data: root.zone convert.awk
	@echo  > $@.tmp '##########################################################################'
	@echo >> $@.tmp '#' $@ 'is a generated file; do not edit it directly.'
	@echo >> $@.tmp '#' Delete $@.zone 'to cause a re-fetch from the primary.'
	@echo >> $@.tmp '##########################################################################'
	awk -F : -f convert.awk -v root=. $< >> $@.tmp
	for d in ${TLDS} ; do \
		sed -e 's/^[Z&.]'$$d':/# skipped &/' $@.tmp > $@.tmp.tmp || return $$? ; \
		mv -f -- $@.tmp.tmp $@.tmp || return $$? ; \
	done
	mv -f -- $@.tmp $@

TLDS=ee nu se dns.opennic.glue opennic.glue gopher bbs libre arpa in-addr.arpa ip6.arpa d.f.ip6.arpa uri.arpa urn.arpa

data: split-horizon split-horizon-machine-local split-horizon-site-local main root.data ${TLDS:@v@${v}.data@}
	@echo >> $@.tmp '##########################################################################'
	@echo >> $@.tmp '#' $@ 'is a generated file; do not edit it directly.'
	@echo >> $@.tmp '# Put your data in the file named main.'
	@echo >> $@.tmp '# Run make when you are done.'
	@echo >> $@.tmp '##########################################################################'
	cat >> $@.tmp main
	@echo >> $@.tmp '##########################################################################'
	@echo >> $@.tmp '#' $@ 'is a generated file; do not edit it directly.'
	@echo >> $@.tmp '# Change the files named split-horizon* if you want these altered.'
	@echo >> $@.tmp '##########################################################################'
	cat >> $@.tmp split-horizon split-horizon-machine-local split-horizon-site-local 
	@echo >> $@.tmp '##########################################################################'
	@echo >> $@.tmp '#' $@ 'is a generated file; do not edit it directly.'
	@echo >> $@.tmp '# These have been and will be fetched from other parties.'
	@echo >> $@.tmp '##########################################################################'
	for d in '' ${TLDS} ; do \
		printf >> $@.tmp '.%s:%s:%s:::%s\n' "$$d" 127.53.0.1 lo lo "$$d" '192.168.100.53' si si || return $$? ; \
	done
	cat >> $@.tmp root.data ${TLDS:@v@${v}.data@}
	mv -f -- $@.tmp $@

#########################################################
# https://jpmens.net/2021/05/18/dns-open-zone-data/
#########################################################

root.zone:
	tcp-socket-connect lax.xfr.dns.icann.org. 53 axfr-get . $@ $@.tmp

arpa.zone:
	tcp-socket-connect lax.xfr.dns.icann.org. 53 axfr-get arpa. $@ $@.tmp

in-addr.arpa.zone:
	tcp-socket-connect lax.xfr.dns.icann.org. 53 axfr-get in-addr.arpa. $@ $@.tmp

ip6.arpa.zone:
	tcp-socket-connect lax.xfr.dns.icann.org. 53 axfr-get ip6.arpa. $@ $@.tmp

d.f.ip6.arpa.zone:
	tcp-socket-connect lax.xfr.dns.icann.org. 53 axfr-get d.f.ip6.arpa. $@ $@.tmp

uri.arpa.zone:
	tcp-socket-connect lax.xfr.dns.icann.org. 53 axfr-get uri.arpa. $@ $@.tmp

urn.arpa.zone:
	tcp-socket-connect lax.xfr.dns.icann.org. 53 axfr-get urn.arpa. $@ $@.tmp

# https://www.internet.ee/domains/ee-zone-file
ee.zone:
	tcp-socket-connect zone.internet.ee. 53 axfr-get ee. $@ $@.tmp

# https://internetstiftelsen.se/en/zone-data/
se.zone:
	tcp-socket-connect zonedata.iis.se. 53 axfr-get se. $@ $@.tmp

# https://internetstiftelsen.se/en/zone-data/
nu.zone:
	tcp-socket-connect zonedata.iis.se. 53 axfr-get nu. $@ $@.tmp

# https://wiki.opennic.org/opennic/t2slaved/zonefile
opennic.glue.zone:
	tcp-socket-connect util-01.opennicproject.org. 53 axfr-get opennic.glue. $@ $@.tmp

# https://wiki.opennic.org/opennic/t2slaved/zonefile
dns.opennic.glue.zone:
	tcp-socket-connect util-01.opennicproject.org. 53 axfr-get dns.opennic.glue. $@ $@.tmp

# https://wiki.opennic.org/opennic/t2slaved/zonefile
gopher.zone:
	tcp-socket-connect smtp.sourpuss.net. 53 axfr-get gopher. $@ $@.tmp

# https://wiki.opennic.org/opennic/t2slaved/zonefile
bbs.zone:
	tcp-socket-connect smtp.sourpuss.net. 53 axfr-get bbs. $@ $@.tmp

# https://wiki.opennic.org/opennic/t2slaved/zonefile
libre.zone:
	tcp-socket-connect smtp.sourpuss.net. 53 axfr-get libre. $@ $@.tmp
