# UIF Nintendont Autoboot Forwarder

This directory vendors the MIT-licensed V10lator/FIX94 Nintendont autoboot
forwarder snapshot:

- Source snapshot: `violator-nintendont-autoboot-forwarder-21aad7997cd733872a29bd9c8069115f204205b1`
- Original copyright: FIX94 2017
- Additional copyright: V10lator 2023
- License: MIT, preserved in `LICENSE-UPSTREAM`; UIF additions use `LICENSE`

UIF uses the permissive portions of this snapshot as the source base for the
bundled GameCube Nintendont autobooter DOL. Do not replace this with the current
GPL branch or with the no-license Nintendont source snapshot.

Important: the upstream snapshot included a GPL `app_booter` subcomponent and a
generated `app_booter.bin`. Those files are intentionally not vendored here.
UIF provides a clean replacement booter stub in `app_booter_clean/`.

## UIF Changes

- The copied full Nintendont config header was removed. UIF now treats the
  version 10 config as an opaque 548-byte interoperability payload through
  `source/uif_nintendont_config.*`. The helper validates only magic, version,
  and controller count, then updates only the autoboot, storage, game-path, and
  cheat-path fields required by this forwarder.
- Explicit DATA-partition fallback handling is independently implemented in
  `source/uif_disc_partition.*`. The normal hardware-proven direct mapped
  partition remains the first path.
- `PROVENANCE.md` identifies the retained MIT base, excluded components, owned
  replacements, and public protocol references.

The forwarder already uses the desired config search order for UIF:

1. `sd:/nintendont/configs/<GAMEID4>.bin`
2. embedded `nincfg.bin`
3. `sd:/nincfg.bin`

UIF uses the root-SD placement as the normal build contract because it preserves
the established wrapper filesystem (`files/game.iso` only). Embedded placement is
kept as an explicit diagnostic option and is not forced by the built-in autobooter.

Configs are validated before use by checking magic bytes, wire version 10, and
the one-byte maximum-controller field. Invalid configs fall through to the next
source instead of being passed to Nintendont.

Before launching Nintendont, it forces Wii VC internal-disc autoboot behavior by
setting config bit 7, clearing config bit 10, and setting the game path to
`"di"`. Every unrelated payload byte is preserved.

The baseline success path now also matches the retained working forwarder where
it matters before that handoff:

- SD startup now follows the retained working launcher contract explicitly:
  `startup`, `isInserted`, then `fatMount` with the same cache parameters.
- `VIDEO_Init()` runs before SD and Wii DVD initialization, matching both
  retained working forwarder lineages.
- The Wii VC-mapped FST remains mounted while the embedded configuration is read;
  UIF no longer unmounts and remounts it between configuration sources.
- Autoboot still uses an empty `argv[0]` and the raw 548-byte config payload at
  `argv[1]`.

The Docker build writes the current release DOL's size, SHA-256, source tree
hash, base commit, and toolchain image to the adjacent build-info manifest.

## Black-Screen Investigation Notes

UIF's first hardening pass found two forwarder behaviors that can make a failed
GCN inject look like a permanent black screen:

- If the forwarder cannot mount/open/read the Wii VC embedded `game.iso`, release
  builds previously returned without an on-screen message. UIF now retries
  automatically, then shows a recovery screen with Retry, Open Nintendont
  without autoboot, and Return to Wii U Menu options.
- `WDVD_FST_Mount()` marked the FST as mounted even when the read failed. UIF now
  keeps the mounted state tied to the actual read result and rejects impossible
  FST sizes before using them.
- The clean app booter now mirrors the `__argv` header to the loaded DOL
  entrypoint handoff area before jumping. This keeps the permissive replacement
  booter aligned with libogc-style argv discovery without restoring the excluded
  GPL app booter.
- The forwarder now avoids several PowerPC/toolchain hazards that could prevent
  a useful recovery path: the Makefile uses the valid GCC linker-plugin flag,
  FST cleanup avoids double-freeing the buffer after failed reads, WDVD reads use
  an aligned bounce buffer for unaligned or short read destinations, and the
  per-game config path writes the four-character game ID without an unaligned
  word store.
- The forwarder first mounts the DATA partition already mapped by Wii VC, which
  matches the known working launch path. An explicit DATA partition open and
  second FST mount attempt remain as a fallback for wrapper variants that need it.
- The FST reader now bounds-checks name-table offsets and directory ranges, and
  the forwarder validates the loaded Nintendont DOL section table before handing
  control to the booter stub.
- The forwarder now closes the temporary `game.iso` file handle after reading
  the embedded Game ID and clears any stale open-file state during FST unmount.
  Without this, the later embedded `nincfg.bin` open could fail because the FST
  layer allows only one open file at a time.
- Debug builds emit visible `UIF-Sxx`/`UIF-Exx` stage codes. Both debug and
  release builds append the same stage diagnostics to
  `sd:/uinjectforge-nintendont-boot.log` after SD mount succeeds. This provides
  launch-stage evidence even when the console returns to the Wii U Menu.

## Build Status

The final build requires:

1. devkitPro/devkitPPC and the normal Wii libogc toolchain, or Docker.
2. Wii U hardware validation for runtime behavior.

See `docs/nintendont-autobooter-build.md` for install and build setup details.

To build with Docker, run:

`Scripts/build_nintendont_autobooter_docker.ps1`

To build with a native devkitPro environment, run:

`Scripts/build_nintendont_autobooter.ps1`

Add `-Debug` to either script to build `nintendont_loader_dbg.dol` in this
directory without replacing UIF's bundled release DOL.

The expected output DOL should be copied to:

`src/UInjectForge.Application/Assets/NintendontAutobooter/uif-nintendont-autobooter.dol`

UIF will package that DOL automatically when the built-in autobooter mode is
selected.
