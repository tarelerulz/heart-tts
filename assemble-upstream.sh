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

# GPU engine files for hn-fast --engine gpu (staged by ~/bin/build-pios-hn-fast.sh): libhngpu.so (V3D runner),
# shaders/ (prebuilt SPIR-V + QPU kernels), hn-gpu-plan.bin (the decoder's step list + weights). Optional: without
# them the "gpu" engine reports itself unavailable and heart-nano stays on cpu-neon.
GPUSRC="${HEART_GPUSRC:-$HOME/src/heart-nano-gpu/gpu}"
mkdir -p "$UP/gpu"
if [ -f "$GPUSRC/libhngpu.so" ] && [ -f "$GPUSRC/hn-gpu-plan.bin" ] && [ -d "$GPUSRC/shaders" ]; then
    cp "$GPUSRC/libhngpu.so" "$GPUSRC/hn-gpu-plan.bin" "$UP/gpu/"
    cp -r "$GPUSRC/shaders" "$UP/gpu/shaders"
else
    echo "WARNING: GPU files not found in $GPUSRC (run build-pios-hn-fast.sh); the gpu engine will be unavailable"
fi

# Voice models (small; bundled). heart = preferred, heart-nano = faster/smaller.
for m in sanoTTS-heart-GGUF sanoTTS-heart-nano-GGUF; do
    mkdir -p "$UP/models/$m"
    cp "$ACPP/models/$m"/*.gguf        "$UP/models/$m/"
    cp "$ACPP/models/$m"/config.json   "$UP/models/$m/"
done

# Kept-loaded Heart server (NEON decoder build); read_heart.py starts it on demand.
[ -x "$ACPP/build/bin/audiocpp_server" ] && cp "$ACPP/build/bin/audiocpp_server" "$UP/build/bin/audiocpp_server" \
    || echo "WARNING: audiocpp_server not found; heart will start a process per sentence"
# C++ finish (gain/gate/consonant lift), built from src/heart-finish.cpp by the root build script.
[ -x "$HERE/src/heart-finish" ] && cp "$HERE/src/heart-finish" "$UP/build/bin/heart-finish" \
    || echo "WARNING: src/heart-finish not built; the finish falls back to numpy"
chmod +x "$UP/build/bin/audiocpp_cli"

echo "Assembled upstream payload in $UP"
du -sh "$UP" "$UP/models" "$UP/build/bin/audiocpp_cli" 2>/dev/null || true
