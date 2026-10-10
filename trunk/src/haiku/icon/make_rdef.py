#!/usr/bin/env python3
"""Writes ../guitarix.rdef from guitarix.hvif (run guitaricon.py first)."""
d = open('guitarix.hvif', 'rb').read()
lines = ['\t$"' + d[i:i + 32].hex().upper() + '"' for i in range(0, len(d), 32)]
open('../guitarix.rdef', 'w').write('''/*
 * Resources of the Haiku build of Guitarix: the signature, the version and the vector icon (see icon/guitaricon.py).
 * Added to the binary by add_resources.sh.
 */

resource app_signature "application/x-vnd.Guitarix";

resource app_version {
	major = 0,
	middle = 47,
	minor = 0,
	variety = B_APPV_FINAL,
	internal = 0,
	short_info = "Guitarix",
	long_info = "Guitarix, a virtual guitar amplifier"
};

resource vector_icon array {
''' + '\n'.join(lines) + '\n};\n')
