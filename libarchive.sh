#!/bin/bash
#

if [ $# -ne 3 ]; then
  echo "Usage: $0 output_directory /path/to/clang /path/to/LightDwarf.so" 1>&2
  exit 1
fi

function parse_so {
  local SO=$1
  objcopy --only-keep-debug "$SO" "$SO.debug"
  strip -s "$SO"
  ls -lh "$SO" "$SO.debug"
}

OUT=$1
CLANG=$2
LDWARF=$3

cd /tmp/lol
wget http://libarchive.org/downloads/libarchive-3.3.2.tar.gz && tar xf libarchive-3.3.2.tar.gz
cd libarchive-3.3.2

( mkdir build_org && cd build_org && cmake -DCMAKE_C_COMPILER="$CLANG" -DCMAKE_BUILD_TYPE=RelWithDebInfo .. -DENABLE_TEST=OFF -G Ninja && ninja )
( mkdir build_light && cd build_light && cmake -DCMAKE_C_COMPILER="$CLANG" -DCMAKE_C_FLAGS="-Xclang -load -Xclang $LDWARF" -DENABLE_TEST=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo .. -G Ninja && ninja )

echo "Original"
parse_so build_org/libarchive/libarchive.so.16
echo "Light"
parse_so build_light/libarchive/libarchive.so.16
