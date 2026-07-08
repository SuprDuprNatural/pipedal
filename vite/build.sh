#!/bin/bash
npm run build && \
# remove any existing .gz files.
if ls dist/assets/*.js.gz 1> /dev/null 2>&1; then
    rm dist/assets/*.js.gz
fi
# generate a .gz file for each JS asset (the entry bundle is main-*.js, not
# index-*.js — the old glob matched nothing and silently gzipped no assets)

if ls dist/assets/*.js 1> /dev/null 2>&1; then
    for file in dist/assets/*.js; do
    # generate a .gz file
        gzip -c $file > $file.gz
    done
fi
