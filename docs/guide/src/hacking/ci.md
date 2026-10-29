# CI and releases

GitHub Actions (`.github/workflows/ci.yml`) runs the same scripts as a
local build. Its runners install the pinned toolchain with
`scripts/setup-toolchain.sh` and cache it.

| Job | What it does |
|---|---|
| `test` | the host tests (JUnit report) and lint over them |
| `build` | the firmware for each target (a matrix of the six boards), lint; keeps the UF2, BIN, ELF, disassembly, map and build-info |
| `docs` | this guide (`scripts/docs.sh`) |
| `package` | the release ZIP |
| `release` | `v*` tags only: a GitHub Release with the ZIP, the guide's ZIP and `CHANGES.md` as its notes; a tag with a hyphen (`v0.11.3-rc1`) makes a pre-release |
| `pages` | release tags without a hyphen only: publishes this guide to GitHub Pages |

## The release ZIP

`scripts/package.sh` collects every built target: the UF2 and BIN per
board, `sdcard-<target>/FIRMWARE.UF2`, the build-info files,
`FLASHING.txt`, `THIRD-PARTY.md` and `licenses/`, `CHANGES.md` (the commits
since the previous tag), this guide as `guide/`, `SHA256SUMS`, and the
source of the built commit.

## Releasing

Push an annotated tag:

```sh
git tag -a vX.Y.Z -m "dvi-feather-terminal X.Y.Z"
git push origin vX.Y.Z
```

Planned work and known issues are tracked with `bd` (beads): `bd ready`,
`bd list`.
