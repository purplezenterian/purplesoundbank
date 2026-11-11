#!/bin/sh
# Fails unless the members' instrument file is byte-identical to the public GitHub page build.
a=$(md5sum "$(dirname "$0")/instrument/purple-soundbank.html" | cut -d' ' -f1)
b=$(md5sum "$1" | cut -d' ' -f1)
[ "$a" = "$b" ] && echo "SAME ($a)" || { echo "DIFFERENT"; exit 1; }
