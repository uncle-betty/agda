#!/bin/bash

rm -f tmp-1.txt tmp-2.txt

grep '### visit ' dump.txt | cut -d " " -f 3 | cut -d : -f 3 >tmp-1.txt
grep '### visit ' dump.txt | cut -d " " -f 5 | cut -d : -f 3 >tmp-2.txt

cat tmp-1.txt tmp-2.txt | sort | uniq | cut -c 3- >info-addr.txt

rm -f tmp-1.txt tmp-2.txt
