# Nintendont Autobooter Provenance

## Retained MIT Base

The forwarder began from V10lator's commit
`21aad7997cd733872a29bd9c8069115f204205b1`, which retained the MIT license
from FIX94 and V10lator. The immediately following upstream commit changed the
repository license; no files introduced by that later commit are included in
this vendored source tree.

- Base commit: https://github.com/V10lator/nintendont-autoboot-forwarder/commit/21aad7997cd733872a29bd9c8069115f204205b1
- Preserved upstream license: `LICENSE-UPSTREAM`; UIF additions: `LICENSE`
- Retained base: build files and the forwarder sources subsequently modified by
  UInjectForge

## Excluded And Replaced Components

- The base snapshot's separate `app_booter` code and generated
  `app_booter.bin` are not included. `app_booter_clean/` is UIF's independently
  written DOL relocation and handoff implementation.
- The former `source/CommonConfig.h` copy is not included. UIF passes its own
  548-byte serialized interoperability payload through the opaque
  `uif_nintendont_config` API, which exposes only the fields this forwarder
  needs.
- Explicit Wii disc partition fallback handling is isolated in
  `uif_disc_partition`. It is independently organized around the public Wii DI
  IPC command and disc-partition formats instead of retaining Nintendont loader
  source structure.

## Interoperability References

- UIF serializer: `src/UInjectForge.Application/Pipeline/Strategies/NintendontConfigBuilder.cs`
- devkitPro libogc DI conventions: https://github.com/devkitPro/libogc/blob/master/libdi/di.c
- Wii disc partition format: https://www.gc-forever.com/yagcd/chap13.html

The public command numbers, fixed binary offsets, byte lengths, and bit masks
used by these helpers are interoperability facts. The owned helpers preserve
unknown config bytes and keep the hardware-proven direct mapped-partition path
ahead of the explicit DI fallback.

## Verification

This standalone repository builds with Docker using the commands in README.md.
The normal DOL from the initial export reproduced SHA-256
`886863E56F01C0CCCFB05DA5596E9BC1907A26F93103D6B7EEE2D6B4E2666E22`.

Historical integration checks in the parent UInjectForge repository:

1. `dotnet test tests/UInjectForge.Application.Tests/UInjectForge.Application.Tests.csproj -c Release --filter FullyQualifiedName~NintendontAutobooterSourceContractTests`
2. `pwsh Scripts/Verify-DependencyPolicy.ps1`
3. `pwsh Scripts/build_nintendont_autobooter_docker.ps1`

The standalone GitHub workflow records source and toolchain identity.
After a source rebuild, Wii U hardware should retest the normal exact ISO/GCM
GCN route because automated checks cannot prove the final vWii handoff.
