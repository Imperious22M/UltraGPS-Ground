# Notes

Design notes, gotchas, and open items.

## `config-backup.toml` is not read at runtime

The repository contains `config-backup.toml` (receiver positions, calibration
offsets, etc.), but `main.cpp` does **not** load it — all configuration is via
command-line flags. It is kept as a reference/backup of a known-good receiver
setup. If TOML-driven configuration is wired up later, `tomlplusplus` is already
linked in.

## Packaging builds in an isolated directory

`build.sh deb` builds the package in a temporary directory under `/tmp` and
copies only the finished `.deb` back into `./build/`, instead of packaging in
place. During packaging, `dpkg-shlibdeps` rapidly creates and deletes a
transient `debian/` control directory; building in an isolated location keeps
that transient tree from being disturbed by external processes that watch or
modify files in the working tree, which can otherwise corrupt the resulting
package. Plain compilation is unaffected because it does not create and delete
files as rapidly.

## Logging model

Two independent controls, easy to confuse:

- **Compile-time** — `-DVERBOSE_LOGGING=ON` activates the `SPDLOG_*` macros
  (and forces a debug build). Off by default, so those macros cost nothing.
- **Runtime** — the `--verbose` flag raises the log level for direct `spdlog`
  calls.

## Possible next steps

- Upgrade the docs theme to [Material for MkDocs](https://squidfunk.github.io/mkdocs-material/)
  by adding `mkdocs-material` to `docs/requirements.txt` and setting
  `theme.name: material` in `mkdocs.yml` (search, dark mode, better nav).
- Document the on-the-wire command protocol (normal vs. continuous) once it is
  finalised.
- Consider a systemd unit so the ground station can run as a service; the `.deb`
  could ship it.
