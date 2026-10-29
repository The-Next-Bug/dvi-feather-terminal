# CI and releases

Two CI systems run the same jobs from the same scripts: GitLab CI
(`.gitlab-ci.yml`) and GitHub Actions (`.github/workflows/ci.yml`). The
GitLab pipeline runs in a build image that carries the pinned toolchain;
GitHub's runners install it with `scripts/setup-toolchain.sh` and cache it.
On GitHub, a `v*` tag makes a GitHub Release with the ZIP and the guide's
ZIP.

| Stage | Job | What it does |
|---|---|---|
| test | `test` | the host tests (JUnit report) and lint over them |
| build | `build` | the firmware for each target (a matrix of the six boards), lint; keeps the UF2, BIN, ELF, disassembly, map and build-info |
| build | `docs` | this guide (`scripts/docs.sh`) |
| package | `package` | the release ZIP; automatic on the default branch and tags |
| release | `release` | tags only: uploads the ZIP to the project's package registry and creates a GitLab Release with `CHANGES.md` |

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
