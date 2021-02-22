#!/bin/bash

ITERS=40

(
    for x in $(seq ${ITERS}); do
        echo 'IOTCM "Issue.agda" None Indirect (Cmd_load "Issue.agda" [])'
    done
    cat
) | (
    LD_PRELOAD=./dump.so \
    agda --interaction --no-libraries --caching --ignore-interfaces \
    +RTS -M5M -A1M -RTS 2>&1 | \
    tee out.txt
)
