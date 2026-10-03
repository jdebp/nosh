#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:

command1_lists="../package/commands1 ../package/extra-manpages1"
command8_lists="../package/commands8 ../package/extra-manpages8"

install -d -m 0755 command manual object library slashdoc
redo-ifdelete -- command manual object library slashdoc

(

cat ../package/commands1 ../package/commands8 |
while read -r i
do
	printf 'command/%s\n' "$i"
	printf >> "$3" '%s\n' "$i"
done

printf 'command/%s\n' getty getty-noreset
printf >> "$3" '%s\n' getty getty-noreset
printf '%s\n' ${command1_lists} ${command8_lists}
printf >> "$3" '%s\n' ${command1_lists} ${command8_lists}

cat ${command1_lists} |
while read -r i
do
	printf '%s/%s%s\n' 'manual' "$i" '.1' 'slashdoc' "$i" '.html'
	printf >> "$3" '%s%s\n' "$i" '.1' "$i" '.html'
done
cat ${command8_lists} |
while read -r i
do
	printf '%s/%s%s\n' 'manual' "$i" '.8' 'slashdoc' "$i" '.html'
	printf >> "$3" '%s%s\n' "$i" '.8' "$i" '.html'
done
for section in 3 4 5 7
do
	echo ../package/extra-manpages${section}
	cat ../package/extra-manpages${section} |
	while read -r i
	do
		printf '%s/%s%s\n' 'manual' "$i" ".${section}" 'slashdoc' "$i" '.html'
		printf >> "$3" '%s%s\n' "$i" ".${section}" "$i" '.html'
	done
done

) |
xargs -r redo-ifchange --
