# Provenance of Elena's avatar assets

## Origin

Kit: `elena-chan-avatar-kit-v3-platinum/` ("Avatar Kit v3 (Platinum)", `manifest.json`
version `3.0-platinum`), a color revision of v2.1-fit: same character, same 12 expressions,
same poses, same framing (the figure at 87.5% within the same 320 × 240 canvas). **Only the
hair** changes, from warm ivory to cold platinum with gray-lilac shadows, through the kit's
reproducible `recolor_platinum.py` script. v2.1-fit is obsolete as a source. That folder is
not tracked by Git; the files needed to build live here in the board, so a clean clone is
enough.

`scripts/elena_avatar_refit.py` builds the 12 GIFs from the kit's final PNG `frames/`
(revision `3.1-platinum-mouth-fix`, whose `speaking` frames have one mouth) and its master
sheet, `source/elena_12_expressions.png`. The kit crops each panel at 320 source pixels
although the art reaches about 360, which cut the thought bubble and the music note. The
script restores that strip from the sheet and moves the art 16 px left, so the figure sits
at x=4, y=30, keeping the kit's 87.5% scale. The eyes, mouth and recolor are the kit's.

The script also saturates the art's dusty reds (`RED_GAIN`), which looked washed out on this
panel. Skin and blush are left alone. Each GIF uses one 64-color palette taken from all its frames. The script also sets frames and
timing for this board: `listening` is a single frame, because the microphone is being encoded
while it shows and a redraw there competes with the encoder. The kit's `gifs-hq/`, `mouths/`,
`preview.png`, `sprite_sheet.*` and viseme headers are not inputs to this binary.

```sh
python3 scripts/elena_avatar_refit.py --kit <kit folder>
```

| File | Bytes | Frames per loop | Canvas |
| --- | --- | --- | --- |
| `gifs/neutral.gif` | 59094 | 2 | 320 × 240 |
| `gifs/happy.gif` | 118223 | 4 | 320 × 240 |
| `gifs/sad.gif` | 116216 | 4 | 320 × 240 |
| `gifs/thinking.gif` | 126500 | 4 | 320 × 240 |
| `gifs/sleeping.gif` | 60727 | 2 | 320 × 240 |
| `gifs/pet.gif` | 123991 | 4 | 320 × 240 |
| `gifs/shake.gif` | 125113 | 4 | 320 × 240 |
| `gifs/speaking.gif` | 157148 | 5 | 320 × 240 |
| `gifs/listening.gif` | 32338 | 1 | 320 × 240 |
| `gifs/look_left.gif` | 126372 | 4 | 320 × 240 |
| `gifs/look_right.gif` | 121973 | 4 | 320 × 240 |
| `gifs/email.gif` | 161340 | 5 | 320 × 240 |
| **Total** | **1329035** | **43** | |

`scripts/tests/fixtures/avatar_gif/v3-frames.json` holds hashes of every frame as composed by
Pillow, and `test_every_v3_frame_matches_independent_compositor` requires the firmware's own
decoder (`gifdec.c` compiled for the host, with `LV_GIF_CACHE_DECODE_DATA` at 0 and 1) to
produce the same pixels over two loops.

Durations are in `GIFS` in the script. They loop forever (`loop = 0`). The firmware honors
them and sets no FPS of its own.

## Aliases

`aliases.json` (24 entries) is exactly the fixed table in
`main/boards/elena-stackchan/avatar_controller.cc`. It was generated from that code and did
not change with the recolor:

```sh
python3 scripts/elena_avatar_assets.py aliases \
    --out main/boards/elena-stackchan/assets/aliases.json
```

`scripts/tests/test_elena_avatar_assets.py::test_aliases_share_files` compares both tables
entry by entry and packages the aliases as `emoji_collection` entries pointing at the same
GIF file: no GIF is duplicated by an alias and nothing hides in `extra_files`. Aliases are
lookup keys; they never become paths.

## Static fallback

`neutral.rgb565` is the first frame of `neutral.gif` in little-endian RGB565, with no header:
153600 bytes (320 × 240 × 2). The real decoder produces it, not a separate conversion:

```sh
python3 scripts/elena_avatar_assets.py fallback \
    --gif main/boards/elena-stackchan/assets/gifs/neutral.gif \
    --out main/boards/elena-stackchan/assets/neutral.rgb565
```

`test_fallback_matches_neutral_rgb565` regenerates it with the LZW cache on and off and
requires byte equality with the committed file. To turn it into a static C header:

```sh
python3 scripts/elena_avatar_assets.py header --out build/avatar_fallback.h
```

## Hashes

`SHA256SUMS` uses the `sha256sum` format and lists the 12 GIFs **and** `neutral.rgb565`. To
check them:

```sh
cd main/boards/elena-stackchan/assets && sha256sum gifs/*.gif neutral.rgb565 > SHA256SUMS
python3 scripts/elena_avatar_assets.py validate
```

`validate` rejects mismatched hashes, extra or missing files, canvases other than 320 × 240,
invalid aliases or aliases pointing at missing GIFs, a fallback other than 153600 bytes, and a
fallback with no `SHA256SUMS` entry or a different hash. A corrupted fallback used to pass
validation; `test_validate_rejects_corrupted_fallback` closes that gap.
`test_missing_file_or_invalid_alias_fails`, `test_validate_rejects_wrong_canvas`,
`test_validate_rejects_fallback_of_wrong_size` and `test_fallback_rejects_short_decoder_output`
verify each guard.

## Art and license

**Notice: this art was generated with AI.** According to its author, Elena is an original
character of this project, created with ChatGPT. Its files differ byte by byte from the
animations of the reference project (`dax8it/nous-girl-stackchan`), from which the kit takes
only names, aliases and conventions (`docs/SPRITES.md`).

The art is free to use: its author releases it under the MIT License, as in the `LICENSE` file
of the parent `elena-chan` repository.

## Pending physical verification

None of this has been checked on the CoreS3 yet: the real size of `assets.bin` within the
assets partition, decoder usage on the ESP32-S3 (the host reserves 384000 bytes of canvas and
indices per GIF) and drawing the fallback when the GIF heap fails.
