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

Each avatar is a folder of `avatars/`, selected at build time with `AVATAR=<name>`; the
default is `elena`. Its `avatar.json` names a renderer, colors and art, and `AvatarStage`
gives every avatar one UI: a transparent top bar with a status bubble on the left and the
clock, connection, mute and battery indicators on the right, and a second strip below it for
operational notices. Conversation captions never show, and previews retain their owners.
The renderer draws in a layer below that UI; parent `docs/avatars.md` explains how to add one.

A boot screen covers the UI until `Application` reports that it is ready. Unlike other boards,
this one waits for its server: a failed TCP probe of the stored WebSocket URL keeps the boot
screen, with an error and a countdown, and the avatar does not load. An MQTT configuration
skips the probe. If the server fails after boot, the avatar stays and the bubble turns red.

Elena's 320×240 GIFs are built by `scripts/elena_avatar_refit.py`. One GIF decoder is used at
a time; unavailable resources fall back once to neutral, then to the static RGB565 image in
application flash. The generated header includes expected GIF sizes and CRCs, so damaged
payloads never enter the decoder's unbounded raw-memory API. Resources, integrity data and
aliases are build dependencies. The artwork was generated with AI (ChatGPT) and is
MIT-licensed; its provenance is in `avatars/elena/PROVENANCE.md`.

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
