#!/usr/bin/env bash
# Runs the ui in development: the backend on [ui] api_port, vite on 5173 proxying to it.
# The gateway is not started here, `make query` does that.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

if [ ! -d ui/web/node_modules ]; then
    npm --prefix ui/web install
fi

python3 -m pip install --quiet --disable-pip-version-check -r ui/api/requirements.txt

python3 -m uvicorn main:app --app-dir ui/api --reload --host 127.0.0.1 \
        --port "$(python3 -c 'import sys; sys.path.insert(0, "ui/api"); import settings; print(settings.load().api_port)')" &
backend=$!
trap 'kill $backend 2>/dev/null || true' EXIT

npm --prefix ui/web run dev
