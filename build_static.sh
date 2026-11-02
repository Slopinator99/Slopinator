#!/bin/sh
# Builds the single-file psxrdc2.exe against MSYS2's static Qt (pacman -S mingw-w64-ucrt-x86_64-qt6-static libtiff libwebp).
# Run from an MSYS2 UCRT64 shell. Result: build_static/psxrdc2.exe, no DLLs needed.
set -e
cd "$(dirname "$0")"
export PATH=/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH
cmake -S . -B build_static -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/c/msys64/ucrt64/qt6-static
# Qt's static targets point at the import libraries (.dll.a); take the real static .a instead,
# plus the libraries those static libs need in turn.
python -I - <<'PY'
import re
p='build_static/build.ninja'
s=open(p).read()
s=re.sub(r'(C\$?:/msys64/ucrt64/lib/lib[A-Za-z0-9_-]+)\.dll\.a', r'\1.a', s)
s=s.replace('C$:/msys64/ucrt64/bin/libharfbuzz-0.dll','C$:/msys64/ucrt64/lib/libharfbuzz.a').replace('C:/msys64/ucrt64/bin/libharfbuzz-0.dll','C:/msys64/ucrt64/lib/libharfbuzz.a')
if '-lrpcrt4' not in s:
    i=s.index('LINK_LIBRARIES ='); j=s.index('\n',i)
    s=s[:j]+' -lrpcrt4 -ljpeg -lzstd -ldeflate -llzma -ljbig -lLerc -lsharpyuv -lbz2 -lgraphite2 -lws2_32 -lwsock32 -lcrypt32 -lbcrypt'+s[j:]
open(p,'w').write(s)
PY
cd build_static && ninja
