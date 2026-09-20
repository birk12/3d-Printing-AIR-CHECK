#!/usr/bin/env bash
# Render every drawing in cad/drawings and export the meshes in cad/stl.
#
# Until v1.4.1 the cameras lived in the shell history of whoever last made a
# picture, so a changed part could not be re-rendered without guessing the
# angle back.  They live here now.
#
#   ./cad/render.sh            everything
#   ./cad/render.sh open       one drawing by name
set -euo pipefail

cd "$(dirname "$0")/openscad"
OUT=../drawings
STL=../stl
mkdir -p "$OUT" "$STL"
SC="aircheck_case.scad"
IMG="--imgsize=1600,1200 --colorscheme=Tomorrow"

# The see-through views import the meshes, so they have to exist first.
meshes() {
    for p in front back door stand; do
        echo "stl: $p"
        openscad -o "$STL/aircheck_$p.stl" -D "part=\"$p\"" "$SC" 2>/dev/null
    done
}

# name | part | camera (cx,cy,cz,rx,ry,rz,dist) | projection
VIEWS=(
    "open_view|open|68,87,17,55,0,315,480|p"
    "assembly_iso|assembly|68,87,17,60,0,315,520|p"
    "exploded|exploded|68,87,40,62,0,315,620|p"
    "exploded_full|exploded_full|68,87,55,60,0,215,820|p"
    "xray_front|xray|68,87,17,230,0,160,520|p"
    "xray_back|xray|68,87,17,50,0,205,520|p"
    "front_iso|assembly|68,87,17,205,0,20,520|p"
    "front_view|assembly|68,87,17,180,0,0,420|o"
    "back_view|assembly|68,87,17,0,0,0,420|o"
    "top_view|assembly|68,87,17,270,0,0,420|o"
    "bottom_view|assembly|68,87,17,90,0,0,420|o"
    "side_view|assembly|68,87,17,90,0,90,420|o"
)

one() {
    local name=$1 part=$2 cam=$3 proj=$4
    echo "png: $name"
    openscad -o "$OUT/$name.png" $IMG --camera="$cam" --projection="$proj" \
             -D "part=\"$part\"" "$SC" 2>/dev/null
}

meshes
for v in "${VIEWS[@]}"; do
    IFS='|' read -r name part cam proj <<<"$v"
    if [ $# -gt 0 ] && [ "$name" != "$1" ] && [ "${name%_view}" != "$1" ]; then continue; fi
    one "$name" "$part" "$cam" "$proj"
done
