#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo"
./scripts/build-linux.sh
ctest --test-dir build/linux --output-on-failure
./build/linux/native --build-check
./build/linux/phoenix_simulation_tests --trace > build/linux/continuation.trace
./build/linux/phoenix_psychic_tests --trace > build/linux/psychic-continuation.trace
node --test tests/system/web_launcher.mjs
npm ci --prefix service --ignore-scripts --no-audit --no-fund
# A fresh local CA exercises successful verified TLS and refusal of an untrusted CA.
fixture=$(mktemp -d "$repo/build/linux/tls-integration.XXXXXX")
trap 'rm -rf -- "$fixture"' EXIT
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$fixture/key.pem" \
    -out "$fixture/certificate.pem" -days 1 -subj /CN=localhost \
    -addext subjectAltName=DNS:localhost,IP:127.0.0.1 > "$fixture/generation.log" 2>&1
PHOENIX_NATIVE_BUILD_DIR="$repo/build/linux" \
PHOENIX_NATIVE_PROBE="$repo/build/linux/phoenix_network_probe" \
PHOENIX_SESSION_PROBE="$repo/build/linux/phoenix_session_probe" \
PHOENIX_FLEET_PROBE="$repo/build/linux/phoenix_fleet_probe" \
PHOENIX_LOBBY_PROBE="$repo/build/linux/phoenix_lobby_tests" \
PHOENIX_TEST_CERT="$fixture/certificate.pem" PHOENIX_TEST_KEY="$fixture/key.pem" \
    npm test --prefix service
