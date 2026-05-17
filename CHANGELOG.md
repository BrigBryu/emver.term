# Changelog

## v1.0.0 - 2026-05-17

### Added
- Added a standalone `ember-term` binary with `widget`, `fullscreen`, and `print-frame` modes.
- Added three terminal scenes: `fire`, `lava`, and `spiral`.
- Added CLI controls for `--scene`, `--palette`, `--fps`, `--width`, `--height`, `--chars`, `--mode`, `--fullscreen`, and `--benchmark`.
- Added `make test` smoke coverage for help output, one-frame rendering, benchmark output, and invalid-argument handling.
- Added release preview GIFs and release-facing documentation for first-time users.

### Changed
- Reworked the project from a Neovim plugin fork into a terminal-first C application.
- Kept the original Lua and Neovim files only as scene-behavior reference material instead of part of the active runtime.
- Standardized the release docs around build-from-source and downloadable binary workflows.

### Performance
- Moved the hot rendering path into C using flat arrays, reusable kernels, and row-level dirty tracking.
- Added a built-in benchmark mode so terminal scenes can be measured directly outside Neovim.
