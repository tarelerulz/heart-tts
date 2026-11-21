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

# Voice models (small; bundled). heart = preferred, heart-nano = faster/smaller.
for m in sanoTTS-heart-GGUF sanoTTS-heart-nano-GGUF; do
    mkdir -p "$UP/models/$m"
    cp "$ACPP/models/$m"/*.gguf        "$UP/models/$m/"
    cp "$ACPP/models/$m"/config.json   "$UP/models/$m/"
done

chmod +x "$UP/build/bin/audiocpp_cli"

echo "Assembled upstream payload in $UP"
du -sh "$UP" "$UP/models" "$UP/build/bin/audiocpp_cli" 2>/dev/null || true
