#!/bin/bash

AGDA=${HOME}/.local/bin/agda

rm -f info-symb.txt tmp.txt
nm ${AGDA} >tmp.txt

for addr in $(cat info-addr.txt); do
    grep "${addr} T" tmp.txt | sed -e 's/^0*//' | cut -d " " -f 1,3 >>info-symb.txt
done

rm -f tmp.txt
