# Provenance of Elena's avatar assets

## Origin

Kit: `elena-chan-avatar-kit-v3-platinum/` ("Avatar Kit v3 (Platinum)", `manifest.json`
version `3.0-platinum`), a color revision of v2.1-fit: same character, same 12 expressions,
same poses, same framing (the figure at 87.5% within the same 320 × 240 canvas). **Only the
hair** changes, from warm ivory to cold platinum with gray-lilac shadows, through the kit's
reproducible `recolor_platinum.py` script. v2.1-fit is obsolete as a source. That folder is
not tracked by Git; the files needed to build live here in the board, so a clean clone is
enough.

`speaking.gif` comes from the kit's later revision, `3.1-platinum-mouth-fix`: the v3.0 one
had two overlapping mouths. The other 11 GIFs in v3.1 are byte-identical to those in v3.0.

**Only** the 12 light GIFs from `gifs/` were copied. `gifs-hq/`, `frames/`, `keyframes/`,
`mouths/`, `preview.png`, `sprite_sheet.*` and the viseme headers (`hg_mouth_frames.h`,
`hg_face_layout.h`) were not: they are not inputs to this binary.

| File | Bytes | Frames per loop | Canvas |
| --- | --- | --- | --- |
| `gifs/neutral.gif` | 48110 | 2 | 320 × 240 |
| `gifs/happy.gif` | 64260 | 3 | 320 × 240 |
| `gifs/sad.gif` | 46516 | 2 | 320 × 240 |
| `gifs/thinking.gif` | 71290 | 3 | 320 × 240 |
| `gifs/sleeping.gif` | 65394 | 3 | 320 × 240 |
| `gifs/pet.gif` | 70919 | 3 | 320 × 240 |
| `gifs/shake.gif` | 69761 | 3 | 320 × 240 |
| `gifs/speaking.gif` | 90833 | 3 | 320 × 240 |
| `gifs/listening.gif` | 69932 | 3 | 320 × 240 |
| `gifs/look_left.gif` | 73739 | 3 | 320 × 240 |
| `gifs/look_right.gif` | 70730 | 3 | 320 × 240 |
| `gifs/email.gif` | 70707 | 3 | 320 × 240 |
| **Total** | **812191** | **34** | |

The 12 GIFs add up to 34 frames per loop (2–3 per file). The firmware's C decoder, run on
the host, produces 68 frames per configuration: two loops of each GIF, with the LZW cache on
and off, identical frame by frame between both configurations and to the first loop.

How this was measured (these are our own checks, not the kit's figures):

- `python3 scripts/elena_avatar_assets.py manifest`: size, dimensions and image descriptor
  count of each file, reading each GIF separately.
- `main/display/lvgl_display/gif/gifdec.c` compiled for the host with the minimal adapters in
  `scripts/tests/fixtures/avatar_gif/` (`lvgl.h`, `esp_log.h`, `avatar_gif_host.c`), with
  `LV_GIF_CACHE_DECODE_DATA` at 0 and 1.
- Independent comparison with Pillow 13.0.0 (`n_frames`, `loop`, `duration`) and with the
  kit's own `gifs_manifest.json`: the three sources agree on v3.1, including the total of
  812191 bytes. The first frame of all 12 GIFs, converted to RGB565 by the firmware's decoder,
  matches Pillow's pixel by pixel when truncated to 5/6/5 bits.

Durations according to each GIF (measured by reading each frame's Graphic Control
Extension): three-frame expressions use 300, 220 and 460 ms; `neutral` and `sad`, 300 and
680 ms; `speaking`, 230, 150 and 210 ms. They loop forever (`loop = 0`). The firmware honors
these durations and sets no FPS of its own.

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
