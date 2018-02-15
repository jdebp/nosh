#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
case "`mysqld --version`" in
*MariaDB*)	ext=mariadb ;;
*Percona*)	ext=percona ;;
*)		ext=mysql ;;
esac
redo-ifchange "$1.${ext}"
ln -s -f "`basename \"$1\"`.${ext}" "$3"
