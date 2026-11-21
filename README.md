# heart-tts — fast "Read Aloud" text-to-speech for the Raspberry Pi

A small Wayland **Read Aloud** window and a `heart-say` command that speak text
aloud on-device with the native [audio.cpp](https://github.com/ShugoAI) sanoTTS
engine and the **heart** / **heart-nano** voices. Both run many times faster than
real time on a Pi 4 / 400, so speech starts almost immediately. The DSP is tuned
for clarity (gain, noise gate, consonant lift, loudness leveler).

> Download the `.deb` and `SHA256SUMS` from the Releases page, verify with
> `sha256sum -c SHA256SUMS`, then `sudo apt install ./heart-tts_<ver>_arm64.deb`.

## What you get

- **`read-aloud`** — a floating, always-on-top window. Highlight (or copy) text
  in any app, click **READ**, and it is spoken through your current audio output.
  Pick the voice (`heart` / `heart-nano`) and playback speed.
- **`heart-say`** — speak from a terminal: `heart-say Hello there`, or with no
  words it speaks the clipboard/selection. `HEART_VOICE=heart-nano HEART_SPEED=1.5`.
- **`heart-tts-doctor`** — checks the engine, models, runtime deps, and runs a
  synthesis self-test.

Playback goes through **PipeWire** (`pw-play`) so it follows your default output
and mixes with other audio (e.g. a browser), falling back to ALSA `aplay`.

## Install / use

```sh
sudo apt install ./heart-tts_0.1.1_arm64.deb
read-aloud                 # or the "Read Aloud" app icon
heart-say "hello world"    # or highlight text and run read-aloud
heart-tts-doctor           # check the install
```

Needs a wlroots Wayland session (Raspberry Pi OS default: labwc / Wayfire) to
read the highlighted selection.

## Build

```sh
cd ~/heart-tts-deb
./assemble-upstream.sh     # pulls engine + driver + voice models from ~/audio.cpp and ~/read.py
dpkg-buildpackage -us -uc -b
```

Build host needs: `dpkg-dev debhelper` (`sudo apt install build-essential debhelper`).
The payload is prebuilt (a native aarch64 engine binary + GGUF model data), so
there is nothing to compile.

## Speed (auto-selected per voice + hardware)

The driver picks the fastest engine it can for each voice:

- **heart-nano** → **`hn-fast`**, a hand-written CPU engine (~2× faster than the
  stock path, byte-identical audio). If a `hn-fast-gpu` binary and a V3D GPU are
  present it uses that instead; otherwise plain CPU. No GPU binary is shipped (it
  needs a custom V3D Vulkan driver), but the hook is there.
- **heart** → the audio.cpp engine.

`heart-tts-doctor` prints which engine each voice will use.

## Voices / models

The **heart** (preferred, fuller sound) and **heart-nano** (smaller, faster)
voice models are bundled. They come from the sanoTTS project
([ampixa/sanoTTS](https://huggingface.co/ampixa/sanoTTS)).

## Licensing

Distributed under **GPL-3**. The bundled voice models are GPL-3 (sanoTTS,
© Ampixa). The `audio.cpp` engine binary is **Apache-2.0** (© ShugoAI LLC),
which is redistributable under GPL-3 as part of this package. See
`debian/copyright`.

## Dependencies

`python3`, `python3-numpy`, `python3-tk`, `libespeak-ng1`, `espeak-ng-data`,
`ffmpeg`, `wl-clipboard`, `alsa-utils`, `pipewire-bin` (for `pw-play`),
`libgomp1`, `libstdc++6` — all declared in `debian/control`.
