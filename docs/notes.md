# Notes

Design notes, gotchas, and open items.

## `config-backup.toml` is not read at runtime

The repository contains `config-backup.toml` (receiver positions, calibration
offsets, etc.), but `main.cpp` does **not** load it — all configuration is via
command-line flags. It is kept as a reference/backup of a known-good receiver
setup. If TOML-driven configuration is wired up later, `tomlplusplus` is already
linked in.

## Packaging inside a Dropbox folder

Building the `.deb` in place fails when the repository lives in a Dropbox-synced
directory. During packaging, `dpkg-shlibdeps` creates and deletes a transient
`debian/` control directory; Dropbox's sync daemon catches it mid-flight and
renames it (e.g. `debian (Case Conflict)`), and that phantom folder leaks into
the package's payload.

The fix — implemented in `build.sh deb` — is to build the package in a
temporary directory under `/tmp`, outside the synced tree, and copy only the
finished `.deb` back in. Plain compilation is unaffected because it does not
create/delete files as rapidly.

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
