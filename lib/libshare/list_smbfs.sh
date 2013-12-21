#!/bin/sh

i=1
net conf listshares | sort --ignore-case | \
while read share; do
    path=`net conf showshare $share | grep path | sed 's@.* = @@'`
    guestok=`net conf showshare $share | grep 'guest ok' | sed 's@.* = @@'`
    readonly=`net conf showshare $share | grep 'read only' | sed 's@.* = @@'`

    printf "%6s: %-80s %-73s guestok=%-3s ro=%-3s\n" $i $share $path $guestok $readonly
    i=`expr $i + 1`
done
