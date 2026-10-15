#!/usr/bin/env bash
# Renders the showcase (vitrine): stills and animated GIFs staged in tools/vitrine/*.txt.
#
#   tools/vitrine/montar.sh [build/chargen_viewer] [pasta de saida]
#
# Needs the raylib viewer (-DCHARGEN_VIEWER=ON), python3 + Pillow for the backdrops, ffmpeg and
# gifsicle for the GIFs. Headless machines: run it under xvfb-run.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
viewer="$(realpath "${1:-$here/../../build/chargen_viewer}")"
out="$(realpath -m "${2:-$here/saida}")"
cd "$here"
python3 fundos.py .
rm -rf saida && mkdir -p saida
for d in $(grep -h '^frames' *.txt | awk '{print $4}'); do mkdir -p "$d"; done
args=()
for s in grupo bestiario grupo_acao bestiario_acao duelo roleta expressoes; do args+=(--scene "$s.txt"); done
"$viewer" "${args[@]}"
gif() {  # frames dir, fps, output, crop (WxH+X+Y or "")
    local vf="split[a][b];[a]palettegen=max_colors=128:stats_mode=full[p];[b][p]paletteuse=dither=bayer:bayer_scale=4"
    [ -n "$4" ] && vf="crop=$4,$vf"
    ffmpeg -v error -y -framerate "$2" -i "$1/%04d.png" -vf "$vf" "$3.raw.gif"
    gifsicle -O3 --lossy=30 "$3.raw.gif" -o "$3"
    rm -f "$3.raw.gif"
}
gif saida/grupo_acao_quadros 25 saida/grupo_acao.gif "960:340:0:150"
gif saida/bestiario_acao_quadros 25 saida/bestiario_acao.gif "960:400:0:110"
gif saida/duelo_quadros 25 saida/duelo.gif ""
gif saida/roleta_quadros 20 saida/roleta.gif "960:360:0:130"
gif saida/expressoes_quadros 20 saida/expressoes.gif ""
rm -rf saida/*_quadros
if [ "$out" != "$here/saida" ]; then mkdir -p "$out" && cp saida/* "$out"/; fi
ls -la "$out"
