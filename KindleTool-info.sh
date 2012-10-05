#!/bin/sh -x

dmesg | grep -A 2 'Amazon Kindle' | grep SerialNumber | cut -d: -f3 | xargs -i sh -c "echo -n '{} '; ./KindleTool/KindleTool/kindletool info {}"

