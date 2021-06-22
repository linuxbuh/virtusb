#!/bin/sh

cd /cygdrive/c/WINDOWS/inf
for i in $(ls oem*.inf)
do
	if grep virtusb $i >/dev/null 2>&1 || grep vusbvhci $i >/dev/null 2>&1
	then
		j=$(basename $i .inf).PNF
		rm -fv $i $j
	fi
done

cd /cygdrive/c/WINDOWS/system32/drivers
rm -fv virtusb.sys vusbvhci.sys

