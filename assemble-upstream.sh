#!/bin/sh
# Assemble the upstream payload (engine binary + driver + GUI + voice models)
# from the working ~/audio.cpp and ~/read.py into ./upstream/, ready for
# dpkg-buildpackage. No build step: the engine is a prebuilt static-ish aarch64
# binary and the models are data. Re-run whenever the engine or driver changes.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ACPP="${HEART_ACPP:-$HOME/audio.cpp}"
READPY="${HEART_READPY:-$HOME/read.py}"
UP="$HERE/upstream"

[ -x "$ACPP/build/bin/audiocpp_cli" ] || { echo "no audiocpp_cli under $ACPP (set HEART_ACPP)"; exit 1; }
[ -f "$ACPP/read_heart.py" ]          || { echo "no read_heart.py under $ACPP"; exit 1; }
[ -f "$READPY" ]                      || { echo "no read.py at $READPY (set HEART_READPY)"; exit 1; }

rm -rf "$UP"
mkdir -p "$UP/build/bin" "$UP/models"

cp "$ACPP/build/bin/audiocpp_cli" "$UP/build/bin/audiocpp_cli"
cp "$ACPP/read_heart.py"          "$UP/read_heart.py"
cp "$READPY"                      "$UP/read.py"

# Hand-written heart-nano CPU engine (~2x faster than audio.cpp for heart-nano).
# Auto-selected by read_heart.py for the heart-nano voice when present; "heart"
# always uses audiocpp_cli. Optional: drop a hn-fast-gpu beside it and read_heart
# will prefer it on a V3D GPU.
[ -x "$ACPP/build/bin/hn-fast" ] && cp "$ACPP/build/bin/hn-fast" "$UP/build/bin/hn-fast" \
    || echo "WARNING: hn-fast not found at $ACPP/build/bin/hn-fast (heart-nano will use the slower audio.cpp path)"
[ -x "$ACPP/build/bin/hn-fast-gpu" ] && cp "$ACPP/build/bin/hn-fast-gpu" "$UP/build/bin/hn-fast-gpu" || true

# Voice models (small; bundled). heart = preferred, heart-nano = faster/smaller.
for m in sanoTTS-heart-GGUF sanoTTS-heart-nano-GGUF; do
    mkdir -p "$UP/models/$m"
    cp "$ACPP/models/$m"/*.gguf        "$UP/models/$m/"
    cp "$ACPP/models/$m"/config.json   "$UP/models/$m/"
done

chmod +x "$UP/build/bin/audiocpp_cli"

echo "Assembled upstream payload in $UP"
du -sh "$UP" "$UP/models" "$UP/build/bin/audiocpp_cli" 2>/dev/null || true
