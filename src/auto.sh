#!/bin/bash

rm -rf build
meson setup build
meson compile -C build
build/mdriver -V