#!/bin/sh
# Packaged Read-Aloud worker: run the sanoTTS heart engine on the system python3
# (needs python3-numpy). read_heart.py finds the engine binary and the voice
# models relative to itself (/opt/heart-tts). Prints GEN / PLAY / DONE / NOTEXT.
PREFIX=/opt/heart-tts
PY=python3
exec "$PY" "$PREFIX/read_heart.py" "${1:-heart}" "${2:-1.0}"
