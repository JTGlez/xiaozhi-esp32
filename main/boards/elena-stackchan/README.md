# Elena StackChan (CoreS3)

ESP32-S3 board identity and variant: `elena-stackchan`. Hardware/pins and touch,
PMIC, backlight and camera setup derive from `m5stack/core-s3` at `4632dc5`.
The CoreS3 codec is reused by explicit source/include paths; its board factory
is excluded. This board adds no servo support.

From the parent project use the `stackchan-build` skill:

```sh
make firmware-image PURPOSE="elena avatar conversation"
make firmware-image BOARD=m5stack/core-s3 BOARD_VARIANT=m5stack-core-s3 PURPOSE="generic comparison"
```

The avatar's 320×240 GIFs are built by `scripts/elena_avatar_refit.py`. A transparent top
strip holds the state text on the left and the battery, mute and connection indicators on
the right. Conversation captions and the clock stay hidden. Operational notices use a second
strip below it, and they and explicit previews retain their owners. One GIF decoder is used at a time; unavailable resources
fall back once to neutral, then to the static RGB565 image in application flash.
The generated header includes expected GIF sizes and CRCs, so damaged payloads
never enter the decoder's unbounded raw-memory API. Resources, integrity data
and aliases are build dependencies. The artwork was generated with AI (ChatGPT) and is MIT-licensed; its provenance is in `assets/PROVENANCE.md`.

Events are optional through `Display`; other boards retain their interface
behavior. The controller resolves activity on the main task; graphics and
controller access use the recursive LVGL port lock. The 30-second recovery
requires idle playback; it does not cancel requests or audio. Callback generations
are local: indistinguishable late server messages cannot be reliably assigned to
a previous turn without changing the protocol.

Initial migration from generic CoreS3 requires an explicit reviewed write through
`stackchan-flash`, preserving NVS. The local server chooses OTA files by the exact
reported `board.type`: use `elena-stackchan_<version>.bin` after migration, never a
renamed generic image. Parent `docs/avatar-validation.md` records measured checks
and outstanding hardware acceptance. A successful build alone is not a hardware test.
