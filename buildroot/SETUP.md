# Buildroot Setup for SPARC V8 / LEON3 SMP Emulator

This folder contains everything needed to build the Linux image that runs
inside the emulator. It targets the **Gaisler Buildroot 2024.02-1.1** release
with a small set of patches and a custom `br2-external` board configuration.

## Prerequisites

- Standard build host (Ubuntu/Debian recommended)
- Buildroot host dependencies: `make gcc g++ libncurses-dev`
- A doom1.wad shareware WAD file (for prboom/Doom)

## 1 — Download Gaisler Buildroot

Gaisler Buildroot is available from the Gaisler website. Navigate to:

> **https://www.gaisler.com/index.php/downloads/linux**

Log in (free registration required) and download the
**Gaisler Buildroot 2024.02-1.1** source archive. Then:

```sh
tar xf gaisler-buildroot-2024.02-1.1.tar.gz
cd gaisler-buildroot-2024.02-1.1
```

If you have anonymous FTP access, it may also be available at:
```sh
wget https://gaisler.com/anonftp/linux/buildroot/gaisler-buildroot-2024.02-1.1.tar.gz
```

## 2 — Apply local.mk

Copy `local.mk` from this folder into the buildroot root. It enables SDL2
fbcon video support and ensures prboom is compiled with `WORDS_BIGENDIAN=1`:

```sh
cp /path/to/sparcv8/buildroot/local.mk .
```

## 3 — Apply Linux kernel patches

Copy the three patches into the Gaisler LEON common Linux patch directory and
renumber them to follow the existing series:

```sh
PATCHDIR=board/gaisler/leon-common/patches/linux/5.10.216

cp /path/to/sparcv8/buildroot/patches/linux/0061-sparc32-leon-grpci2-guard-against-possible-null-pointer-dev-of_node.patch  $PATCHDIR/
cp /path/to/sparcv8/buildroot/patches/linux/0063-fix-replace-spin_lock_irqsave-with-a-trylock.patch                          $PATCHDIR/
cp /path/to/sparcv8/buildroot/patches/linux/0064-Bugfix-use-ARRAY_SIZE-instead-of-sizeof-to-avoid-rea.patch                  $PATCHDIR/
```

| Patch | What it fixes |
|---|---|
| `0061` | Null-pointer dereference in `sparc_dma_alloc_resource` when `dev->of_node` is NULL (crashes during PCI DMA mapping with GRPCI2) |
| `0063` | SMP deadlock in `leon_cross_call` — replaces `spin_lock_irqsave` with a trylock loop so spinning CPUs can still receive IPIs |
| `0064` | Off-by-one in `grvga_probe` — `sizeof(grvga_modedb)` returns byte count, not entry count; replaced with `ARRAY_SIZE` |

## 4 — Apply prboom patches

Copy the prboom patch and the updated `prboom.mk` into the buildroot package directory:

```sh
cp /path/to/sparcv8/buildroot/patches/prboom/0003-fix-s16le-byteorder-on-big-endian.patch  package/prboom/
cp /path/to/sparcv8/buildroot/patches/prboom/prboom.mk                                      package/prboom/prboom.mk
```

| File | What it fixes |
|---|---|
| `0003` patch | On big-endian (SPARC), `*(short*)ptr = v` stores bytes big-endian, but the emulated AC97's `mem_read16` expects S16_LE bytes. The patch introduces a `PUT_S16LE` macro that calls `SDL_Swap16` on big-endian hosts, producing white noise otherwise. |
| `prboom.mk` | Adds a `PRBOOM_BIG_ENDIAN_FIXUP` post-configure hook that patches `config.h` to define `WORDS_BIGENDIAN 1`. Without this, prboom reads WAD header integers in wrong byte order and fails to open the WAD ("failed to read directory"). The sed regex matches autoconf's `#  undef` (with spaces). |

## 5 — Link the br2-external board

Copy or symlink the `br2-external` directory from this folder into the
buildroot extensions directory:

```sh
cp -r /path/to/sparcv8/buildroot/br2-external extensions/br2-external-example
# or: ln -s /path/to/sparcv8/buildroot/br2-external extensions/br2-external-example
```

## 6 — Add the doom1.wad shareware WAD

Place the Doom shareware WAD at:

```sh
mkdir -p output/target/usr/share/games/doom/
cp doom1.wad output/target/usr/share/games/doom/
```

The shareware `doom1.wad` (v1.9) is freely distributable. Its SHA-1 is
`5b2e249b9b4cb44daebf4673c0ac7003e5f4f93d`.

## 7 — Configure and build

```sh
make BR2_EXTERNAL=extensions/br2-external-example my-board_defconfig
make -j$(nproc)
```

The resulting ELF image is at `output/images/vmlinux` and can be passed
directly to the emulator:

```sh
bin/sparcv8_leon_smp -i /path/to/gaisler-buildroot-2024.02-1.1/output/images/vmlinux -n 2
```

## What the br2-external board config enables

| File | Purpose |
|---|---|
| `configs/my-board_defconfig` | Buildroot defconfig: overlays, post-build script, kernel config fragments, busybox fragments, `kbd` package |
| `board/my-board/kernel.config` | Enables framebuffer (`FB_GRVGA`, `FRAMEBUFFER_CONSOLE`), PS/2 keyboard (`SERIO_APBPS2`, `KEYBOARD_ATKBD`) |
| `board/my-board/busybox.config` | Enables `timeout` applet |
| `board/my-board/overlay/etc/profile.d/audio.sh` | Sets `AUDIODEV=hw:0,0` so SDL1 opens the AC97 hardware device directly (bypasses ALSA plug layer which fails for unknown reasons) |
| `board/my-board/overlay/etc/profile.d/keymap.sh` | Loads Norwegian keyboard map on login |
| `board/my-board/overlay/etc/asound.conf` | ALSA default PCM config (not used by SDL1/prboom due to `AUDIODEV` override, kept for other tools) |
| `board/my-board/overlay/usr/games/boom.cfg` | Sets `samplerate 48000` to match the AC97 DMA tick rate (default 22050 Hz causes underruns and noise) |

## Running Doom

```sh
# On the emulated LEON3 Linux:
SDL_VIDEODRIVER=fbcon SDL_NOMOUSE=1 /usr/games/prboom -iwad /usr/share/games/doom/doom1.wad
```
