# Rollback to the Linux port stable checkpoint

Annotated tag **`linux-port-stable-2026-06-02`** marks the state where:

- `m88` (SDL) and `m88-qt` (Qt6) both run with good audio (FM/ADPCM), tempo, and display
- Linux-specific fixes: `GetTime()` sound timing, `opna` second-track ADPCM, `M88PaceFrame`, no `UpdateCounter` on Linux, `srcbuf` click fix

## Restore source to this tag

```bash
cd /path/to/m88
git fetch --tags   # if using a remote
git checkout linux-port-stable-2026-06-02
cmake -S . -B build -DM88_BUILD_QT_FRONTEND=ON
cmake --build build -j --target m88 m88-qt
```

To keep working on a branch instead of detached HEAD:

```bash
git checkout -b my-work linux-port-stable-2026-06-02
```

## Compare current tree to the tag

```bash
git diff linux-port-stable-2026-06-02
git log linux-port-stable-2026-06-02..HEAD --oneline
```
