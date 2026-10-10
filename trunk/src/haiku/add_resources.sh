#!/bin/sh
# Adds the icon, signature and version (guitarix.rdef) to the built Guitarix binary. Run on Haiku:
#   add_resources.sh path/to/guitarix
set -e
here=$(cd "$(dirname "$0")" && pwd)
rc -o /tmp/guitarix.rsrc "$here/guitarix.rdef"
xres -o "$1" /tmp/guitarix.rsrc
mimeset -f "$1"
rm -f /tmp/guitarix.rsrc
