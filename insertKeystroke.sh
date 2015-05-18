#!/bin/sh -xe

echo $* | od -t u1 | cut -c8- | tr -d '\n' | sed -e 's/  */ /g' -e 's/^ //' -e 's/ /\n/g' | xargs -i echo "lipc-set-prop -i -- com.lab126.framework insertKeystroke {}"


