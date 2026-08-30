# UInjectForge Nintendont Autobooter

A Wii VC forwarder that launches Nintendont with the embedded GCN game.
Nintendont itself must already be installed on the user's SD card.

Based on V10lator's MIT-era commit
`21aad7997cd733872a29bd9c8069115f204205b1`, with the UIF changes described in
[PROVENANCE.md](PROVENANCE.md). Original attribution is preserved in
[LICENSE-UPSTREAM](LICENSE-UPSTREAM); UIF additions use [LICENSE](LICENSE).
Dependency notices are in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Build

Run from a fresh checkout with Docker installed:

```powershell
docker build -t uif-nintendont-source .
docker run --rm --network none -v "${PWD}:/project" -w /project uif-nintendont-source sh build.sh
```

Outputs: `nintendont_loader.dol` (normal) and `nintendont_loader_dbg.dol`
(diagnostic). Do not substitute the diagnostic DOL for the normal release.
UIF bundles its own autobooter; users normally do not need this separate download.

## Releases

Attach DOL files with checksums, source commit, toolchain digest and all notices.
A source build is not hardware validation. Test a normal GCN launch after a
binary change; retain known-working releases for comparison.

Exported from UIF commit `629c424039246f5346259a4b448bb10236883c3a`.
The source is isolated from UIF's private history. Generated headers, objects,
ELFs, DOLs and test game packages are not committed.
