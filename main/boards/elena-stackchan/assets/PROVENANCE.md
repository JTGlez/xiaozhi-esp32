# Procedencia de los recursos del avatar de Elena

## Origen

Kit: `elena-chan-avatar-kit-v3-platinum/` («Avatar Kit v3 (Platinum)»,
`manifest.json` versión `3.0-platinum`), revisión cromática de v2.1-fit: mismo
personaje, mismas 12 expresiones, mismas poses, mismo encuadre (figura al 87,5 %
dentro del mismo lienzo 320 × 240). Cambia **únicamente el cabello**, de marfil
cálido a platinado frío con sombras gris-lila, mediante el script reproducible
`recolor_platinum.py` del propio kit. v2.1-fit queda obsoleto como fuente. Esa
carpeta no está seguida por Git; los archivos necesarios para compilar viven
aquí dentro de la placa, de modo que una clonación limpia basta.

`speaking.gif` viene de la revisión posterior del kit, `3.1-platinum-mouth-fix`: la de v3.0
tenía dos bocas superpuestas. Los otros 11 GIFs de v3.1 son idénticos byte a byte a los de
v3.0.

Se copiaron **solo** los 12 GIFs ligeros de `gifs/`. No se copiaron `gifs-hq/`,
`frames/`, `keyframes/`, `mouths/`, `preview.png`, `sprite_sheet.*`, ni los cabezales de
visemas (`hg_mouth_frames.h`, `hg_face_layout.h`): no son entradas del binario de esta
entrega.

| Archivo | Bytes | Cuadros por vuelta | Lienzo |
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

Los 12 GIFs suman 34 cuadros por vuelta (2–3 por archivo). El decodificador C del
firmware ejecutado en el ordenador produce 68 cuadros por configuración: dos vueltas de
cada GIF, con la caché LZW activada y desactivada, cuadro a cuadro idénticas entre ambas
configuraciones y a la primera vuelta.

Cómo se midió (no son cifras del kit, son comprobaciones propias):

- `python3 scripts/elena_avatar_assets.py manifest` — tamaño, dimensiones y conteo de
  descriptores de imagen de cada archivo, leyendo cada GIF por separado.
- `main/display/lvgl_display/gif/gifdec.c` compilado para el host con los adaptadores
  mínimos de `scripts/tests/fixtures/avatar_gif/` (`lvgl.h`, `esp_log.h`,
  `avatar_gif_host.c`), con `LV_GIF_CACHE_DECODE_DATA` a 0 y a 1.
- Contraste independiente con Pillow 13.0.0 (`n_frames`, `loop`, `duration`) y con
  `gifs_manifest.json` del propio kit: las tres fuentes coinciden en v3.1, incluido el
  total de 812191 bytes. El primer cuadro de los 12 GIFs convertido a RGB565 por el
  decodificador del firmware coincide píxel a píxel con el de Pillow truncando a
  5/6/5 bits, en los 12 archivos.

Duraciones según el GIF (medidas leyendo el Graphic Control Extension de cada
cuadro): las expresiones de tres cuadros usan 300, 220 y 460 ms; `neutral` y `sad`,
300 y 680 ms; `speaking`, 230, 150 y 210 ms. Bucle infinito (`loop = 0`). El firmware
respeta esas duraciones; no se fija un FPS propio.

## Alias

`aliases.json` (24 entradas) es exactamente la tabla fija de
`main/boards/elena-stackchan/avatar_controller.cc`. Se generó desde ese código y no
cambió con el recolor:

```sh
python3 scripts/elena_avatar_assets.py aliases \
    --out main/boards/elena-stackchan/assets/aliases.json
```

`scripts/tests/test_elena_avatar_assets.py::test_aliases_share_files` compara ambas tablas
entrada por entrada y empaqueta los alias como entradas de `emoji_collection` que apuntan
al mismo archivo GIF: ningún GIF se duplica por alias y nada se esconde en `extra_files`.
Los alias son claves de búsqueda; nunca se convierten en rutas.

## Respaldo estático

`neutral.rgb565` es el primer cuadro de `neutral.gif` en RGB565 little endian, sin
cabecera: 153600 bytes (320 × 240 × 2). Lo produce el decodificador real, no una
conversión aparte:

```sh
python3 scripts/elena_avatar_assets.py fallback \
    --gif main/boards/elena-stackchan/assets/gifs/neutral.gif \
    --out main/boards/elena-stackchan/assets/neutral.rgb565
```

La prueba `test_fallback_matches_neutral_rgb565` lo regenera con caché LZW activada y
desactivada y exige igualdad byte a byte con el archivo commiteado. Para convertirlo en
un cabezal C estático:

```sh
python3 scripts/elena_avatar_assets.py header --out build/avatar_fallback.h
```

## Hashes

`SHA256SUMS` usa el formato de `sha256sum` y lista los 12 GIFs **y**
`neutral.rgb565`. Cualquier comprobación:

```sh
cd main/boards/elena-stackchan/assets && sha256sum gifs/*.gif neutral.rgb565 > SHA256SUMS
python3 scripts/elena_avatar_assets.py validate
```

`validate` rechaza hashes distintos, archivos de más o de menos, lienzos que no sean
320 × 240, alias inválidos o apuntando a GIFs inexistentes, un respaldo de tamaño
distinto de 153600 bytes y un respaldo sin entrada en `SHA256SUMS` o con hash distinto.
Antes de esta entrega un respaldo corruptado pasaba la validación; la prueba
`test_validate_rejects_corrupted_fallback` cierra ese hueco. Las pruebas
`test_missing_file_or_invalid_alias_fails`, `test_validate_rejects_wrong_canvas`,
`test_validate_rejects_fallback_of_wrong_size` y
`test_fallback_rejects_short_decoder_output` verifican cada guardián.

## Licencia

El kit **no declara ninguna licencia**: ni su `README.md`, ni sus archivos sueltos, ni su
carpeta `source/` incluyen aviso de licencia para la ilustración. La identidad de Elena
(cabello marfil, platinado en v3, laurel, toga) es suya, y sus archivos difieren byte a
byte de las animaciones del proyecto de referencia.

El proyecto de referencia (`dax8it/nous-girl-stackchan`, cuya copia consultada está en
`vendor/firmwares/nous-girl-stackchan/`) licencia su código y sus documentos con MIT, y
declara el arte aparte: `faces/starter/` es CC0, mientras que `faces/tribute/` **no está
bajo ninguna licencia abierta** — solo uso personal y no comercial. El kit de Elena usa
los nombres, el alias `_emote_aliases.json` y las convenciones de esa guía
(`docs/SPRITES.md`), pero no se documenta que su ilustración derive de ese arte sin
licencia, ni quién la creó. El recolor v3 parte del arte de v2.1-fit y no añade ninguna
licencia.

Consecuencia registrada sin suposiciones:

- No se afirma aquí que estos GIFs estén bajo MIT, CC0 ni ninguna otra licencia: **no hay
  licencia concedida documentada para el arte**.
- Se conservan en el árbol de la placa por procedencia interna del proyecto, con sus
  hashes, para poder compilarlos.
- Los forks de este proyecto son públicos. Antes de publicar o distribuir esta entrega
  fuera del proyecto propio hace falta autorización del autor del arte. Si esa
  autorización no llega, la alternativa es sustituir los 12 GIFs por los de
  `faces/starter/` (CC0) del proyecto de referencia y mantener el resto del trabajo.

## Pendiente de verificación física

Nada de esto se ha comprobado aún en el CoreS3: tamaño real de `assets.bin` dentro de la
partición de recursos, consumo del decodificador en el ESP32-S3 (el host reserva 384000
bytes de lienzo e índices por GIF) y dibujo del respaldo cuando falla el heap del GIF.
