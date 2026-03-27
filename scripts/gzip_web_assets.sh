#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

assets=(
  "data/webinterface/index.html"
  "data/webinterface/app.css"
  "data/webinterface/app.js"
  "data/webinterface/runtimeui.json"
)

for asset in "${assets[@]}"; do
  if [[ -f "$asset" ]]; then
    gzip -n -9 -c "$asset" > "$asset.gz"
  fi
done

for asset in data/webinterface/i/*.svg; do
  if [[ -f "$asset" ]]; then
    gzip -n -9 -c "$asset" > "$asset.gz"
  fi
done

if [[ -f "data/webinterface/cfgdocs.fr.json" ]]; then
  gzip -n -9 -c "data/webinterface/cfgdocs.fr.json" > "data/webinterface/cfgdocs.jz"
fi

if [[ -f "data/webinterface/cfgmods.fr.json" ]]; then
  gzip -n -9 -c "data/webinterface/cfgmods.fr.json" > "data/webinterface/cfgmods.jz"
fi
