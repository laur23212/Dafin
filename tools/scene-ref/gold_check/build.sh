#!/bin/sh
# Builds the stand-alone checks (Linux, Qt 6 Core, FreeType, HarfBuzz): ./build.sh gold_check | draft_check
cd "$(dirname "$0")" && Q=/usr/include/x86_64-linux-gnu/qt6 && R=$(cd ../../.. && pwd) &&
g++ -std=c++20 -O2 -fPIC -Wall -DSCENE_FONTS="\"$R/scene-fonts\"" -I "$R/src" -I $Q -I $Q/QtCore -I /usr/include/freetype2 "$1.cpp" -o "$1" \
  -lQt6Core -lfreetype /usr/lib/x86_64-linux-gnu/libharfbuzz.so.0
