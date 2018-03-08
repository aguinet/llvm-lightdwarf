Light DWARF experiment
======================

This is an experiment to generate lightweight DWARF informations, that are
stripped to only contains necessary information for doing FFI for C functions.

This is done through an LLVM pass that removes unnecessary information for
DWARF representation that exists in the LLVM IR, after the optimisations are
run on the IR.

Usage
=====

This pass is based on LLVM 5.0.

To compile it:

.. code:: bash

  $ mkdir build && cd build && cmake -DLLVM_CONFIG=/path/to/llvm-config -DCMAKE_BUILD_TYPE=release ..

To use it:

.. code:: bash

  $ /path/to/clang -Xclang -load -Xclang ./LightDwarf.so -O2 a.c -c -o a.o

Note that you need optimisations for the pass to run!

Experiments
===========

These experiments need the ninja build system (https://ninja-build.org/).

libarchive
----------

.. code:: bash

  $ mkdir /tmp/libarchive && ./libarchive.sh /tmp/libarchive /path/to/clang /path/to/LightDwarf.so

On my system (Debian Linux x64), this gives:

.. code::

  Original
  -rwxr-xr-x 1 aguinet aguinet 727K Mar  8 21:26 build_org/libarchive/libarchive.so.16
  -rwxr-xr-x 1 aguinet aguinet 1.8M Mar  8 21:26 build_org/libarchive/libarchive.so.16.debug
  Light
  -rwxr-xr-x 1 aguinet aguinet 727K Mar  8 21:26 build_light/libarchive/libarchive.so.16
  -rwxr-xr-x 1 aguinet aguinet 536K Mar  8 21:26 build_light/libarchive/libarchive.so.16.debug


where ``*.so`` files are stripped from debug informations, and ``*.debug`` files represent only the debug informations.

zlib
----

.. code:: bash

  $ mkdir /tmp/zlib && ./zlib.sh /tmp/zlib /path/to/clang /path/to/LightDwarf.so

On my system (Debian Linux x64), this gives:

.. code::

  Original
  -rwxr-xr-x 1 aguinet aguinet  99K Mar  8 21:27 build_org/libz.so.1.2.11
  -rwxr-xr-x 1 aguinet aguinet 161K Mar  8 21:27 build_org/libz.so.1.2.11.debug
  Light
  -rwxr-xr-x 1 aguinet aguinet 99K Mar  8 21:27 build_light/libz.so.1.2.11
  -rwxr-xr-x 1 aguinet aguinet 61K Mar  8 21:27 build_light/libz.so.1.2.11.debug

where ``*.so`` files are stripped from debug informations, and ``*.debug`` files represent only the debug informations.
