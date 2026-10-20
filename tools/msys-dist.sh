#!/bin/bash

if [[ ! -x melonRA.exe ]]; then
	echo "Run this script from the directory you built melonRA."
	exit 1
fi

mkdir -p dist

for lib in $(ldd melonRA.exe | grep mingw | sed "s/.*=> //" | sed "s/(.*)//"); do
	cp "${lib}" dist
done

cp melonRA.exe dist
windeployqt dist
