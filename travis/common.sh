#!/bin/bash

function build_sketches()
{
    local arduino=$1
    local srcpath=$2
    local platform=$3
		local variant=$4
    find -L "$srcpath" -type f -name '*.ino' | while read sketch; do
        local sketchdir=`dirname "$sketch"`
				local sketchname=`basename "$sketch" .ino`
        if [[ -f "$sketchdir/.$platform.skip" ]]; then
            echo -e "\n\n ------------ Skipping $sketchname ($platform:$variant) ------------ \n\n";
            continue
        fi
        echo -e "\n\n ------------ Building $sketchname ($platform:$variant) ------------ \n\n";
				[ ! -d "$SKETCHBOOK/$sketchname" ] && ln -s "$sketchdir" "$SKETCHBOOK/$sketchname"
				cd "$SKETCHBOOK/$sketchname"
				$arduino --verify --board "esp8266com:$platform:$variant" "${sketchname}.ino"
        local result=$?
        if [ $result -ne 0 ]; then
            echo "Build failed!"
            return $result
        fi
    done
}
