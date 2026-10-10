#!/bin/sh
# Builds the Haiku package of Guitarix (x86_64, gcc 13). Run on Haiku, from anywhere:
#   src/haiku/make-package.sh [output-dir]
# Needs the build packages: gtk3_devel gtkmm3_devel glibmm_devel libsigc++_devel boost1.83_devel lilv_devel lv2
# liblrdf_devel raptor_devel fftw_devel libsndfile_devel curl_devel eigen3 gperf sassc ladspa_sdk_devel
set -e

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
out=${1:-$top/..}
version=0.47.0
revision=1
arch=$(uname -m | sed 's/x86_64/x86_64/')
stage=$top/build-package
root=$stage/root

cd "$top"
export CXX=g++ CC=gcc

./waf configure --prefix=/boot/system --no-avahi --no-bluez --no-nsm --no-lv2 --no-faust \
	--includeresampler --includeconvolver --disable-nls --install-roboto-font \
	--fontdir=/boot/system/data/fonts/ttfonts --no-font-cache-update --no-desktop-update
./waf build -j6

rm -rf "$stage"
mkdir -p "$stage/inst"
./waf install --destdir="$stage/inst" > "$stage/install.log"

# the Haiku layout: the program in apps/, a link in bin/, a Deskbar entry, the data in data/guitarix
src=$stage/inst/boot/system
mkdir -p "$root"/apps "$root"/bin "$root"/lib "$root"/data/guitarix "$root"/data/deskbar/menu/Applications \
	"$root"/data/documentation/packages/guitarix
cp "$src/bin/guitarix" "$root/apps/Guitarix"
cp -a "$src"/lib/libgxw* "$root/lib/"
cp -r "$src/data/guitarix/gx_head" "$src/data/guitarix/pixmaps" "$root/data/guitarix/"
cp -r "$src/data/fonts" "$root/data/"
ln -s ../apps/Guitarix "$root/bin/guitarix"
ln -s ../../../../apps/Guitarix "$root/data/deskbar/menu/Applications/Guitarix"
cp COPYING README changelog "$root/data/documentation/packages/guitarix/"

# icon, signature and version into the program
"$here/add_resources.sh" "$root/apps/Guitarix"

# plain files are not programs
find "$root/data" -type f -exec chmod 644 {} +

cat > "$stage/.PackageInfo" <<INFO
name			guitarix
version			$version-$revision
architecture	$arch
summary			"A virtual guitar amplifier and effects processor"
description		"Guitarix is a virtual guitar amplifier with a large collection of amps, cabinets and effects, \
neural amp models and convolution. This is the Haiku port: it uses the Media Kit for audio and the MIDI2 Kit for MIDI \
instead of JACK. It works best with real-time audio enabled in the sound driver settings."
packager		"ablyssx74"
vendor			"Guitarix developers; Haiku port by ablyssx74"
copyrights {
	"2009-2026 Hermann Meyer, James Warden, Andreas Degert and others"
}
licenses {
	"GNU GPL v2"
}
urls {
	"https://github.com/ablyssx74/guitarix"
}
provides {
	guitarix = $version
	app:Guitarix = $version
	cmd:guitarix = $version
	lib:libgxw = 0.1.0 compat >= 0
	lib:libgxwmm = 0.1.0 compat >= 0
}
requires {
	haiku
	lib:libatk_1.0
	lib:libatkmm_1.6
	lib:libboost_iostreams
	lib:libcairo
	lib:libcairo_gobject
	lib:libcairomm_1.0
	lib:libcurl
	lib:libfftw3f
	lib:libgcc_s
	lib:libgdk_3
	lib:libgdk_pixbuf_2.0
	lib:libgdkmm_3.0
	lib:libgio_2.0
	lib:libgiomm_2.4
	lib:libglib_2.0
	lib:libglibmm_2.4
	lib:libgmodule_2.0
	lib:libgobject_2.0
	lib:libgthread_2.0
	lib:libgtk_3
	lib:libgtkmm_3.0
	lib:libharfbuzz
	lib:libintl
	lib:liblilv_0
	lib:liblrdf
	lib:libpango_1.0
	lib:libpangocairo_1.0
	lib:libpangomm_1.4
	lib:libsigc_2.0
	lib:libsndfile
	lib:libstdc++
}
INFO

rm -f "$out/guitarix-$version-$revision-$arch.hpkg"
package create -C "$root" -i "$stage/.PackageInfo" "$out/guitarix-$version-$revision-$arch.hpkg"
echo "Built $out/guitarix-$version-$revision-$arch.hpkg"
