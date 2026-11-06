# libopenusd 26.8-3

## New features

- Overall: Adds registered typed stage authoring through `libopenusd_write_stage_v1`. Native arrays, dictionaries, relationships, connections, and time samples are authored directly in OpenUSD and exported without a text intermediary.

# libopenusd 26.8-2

## New features

- `usd_inspect()` reads typed prim metadata and selected attributes without text conversion. The registered `libopenusd_inspect_v1` entry point exposes the same query to native R consumers, preserving numeric precision and resolved asset paths.

## Bugfixes

- Overall: Installation finds a compatible CMake later on PATH or in standard macOS locations when the first executable is too old. An explicit `CMAKE` setting remains authoritative.

# libopenusd 26.8-1

## New features

- Overall: Provides OpenUSD 26.08 with registered R functions and a versioned native API for stage import, composition, and export, without Python.
