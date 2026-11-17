#!/usr/bin/env bash
# Builds Cadence and packages a zip ready to upload to cPanel (or any Node host).
# Usage: npm run package:cpanel   ->  dist/cadence-cpanel.zip
#
# Layout inside the zip:
#   cadence/app.js         startup file for cPanel "Setup Node.js App"
#   cadence/package.json   minimal, no dependencies (nothing to npm install)
#   cadence/.env.example   settings template
#   cadence/server/        self-contained Next.js server (with its own node_modules)
# The server lives in a subfolder because CloudLinux's Node.js selector won't
# create an app whose root already contains a node_modules folder.
set -euo pipefail
cd "$(dirname "$0")/.."

npm run build

OUT=dist/cadence
rm -rf "$OUT" dist/cadence-cpanel.zip
mkdir -p "$OUT/server/.next"
cp -R .next/standalone/. "$OUT/server/"
cp -R .next/static "$OUT/server/.next/static"
[ -d public ] && cp -R public "$OUT/server/public"
cp deploy/cpanel/app.js "$OUT/app.js"
cp .env.example "$OUT/.env.example"
cat > "$OUT/package.json" <<'JSON'
{
  "name": "cadence",
  "private": true,
  "main": "app.js",
  "engines": { "node": ">=20.9" }
}
JSON
# Never ship local secrets.
rm -f "$OUT"/server/.env "$OUT"/server/.env.local "$OUT"/server/.env.*

(cd dist && zip -qr cadence-cpanel.zip cadence)
echo "Created dist/cadence-cpanel.zip ($(du -h dist/cadence-cpanel.zip | cut -f1))"
