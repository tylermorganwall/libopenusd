# libopenusd 26.8-0

- Overall: Provides OpenUSD 26.08 with registered R functions and a versioned
  native API for stage import, composition, and export, without Python.
- Overall: Supports typed stage authoring through `libopenusd_write_stage_v1`,
  including native arrays, dictionaries, relationships, connections, and time
  samples, without a text intermediary.
- `usd_inspect()` Reads typed prim metadata and selected attributes without text
  conversion, preserving numeric precision and resolved asset paths. The
  registered `libopenusd_inspect_v1` entry point exposes the same queries to
  native R consumers.
- `usd_messages()` Retrieves buffered OpenUSD warnings and debug messages,
  including diagnostics from native workers.
- Overall: Supports CMake 3.22 and later for the native library build. Finds a
  compatible CMake on PATH or in standard macOS locations when the first executable
  is too old, while respecting an explicit `CMAKE` setting.
- Overall: Finds RcppParallel's TBB DLL on Windows when no import library is shipped.
- Overall: Uses the GNU make tools supplied by Rtools for the vendored Windows
  build, avoiding a requirement for a separate `mingw32-make` executable.
- Overall: Fixes Windows GCC compilation by matching OpenUSD's constructor
  implementation to its compiler-specific declarations, using Windows error-message
  APIs, resolving Windows library symbols, and recognizing UCRT size formats
  when checking buffered diagnostics.
- Overall: Contains native fatal diagnostics at the provider boundary, preserves
  R's signal handlers, and keeps debug settings from terminating the R process.
  Native cleanup completes within the operation by default.
- Overall: Limits native operations to two threads by default and respects
  `LIBOPENUSD_NUM_THREADS` without permanently limiting the shared scheduler.
- Overall: Corrects native integer-format mismatches, retains compiler warnings,
  and disables C and C++ assertions in nested release builds to match R's native
  compilation defaults.
- Overall: Uses R's standard compilation and linking rules for the package shared
  library, with CMake confined to the vendored OpenUSD build. Configuration and
  source preparation run through `tools/configure.R` without Python. Preserves
  R's full compiler commands, including wrappers and standard-library flags,
  and refreshes the vendored build when compiler commands change.
- Overall: Reduces the bundled sources by removing unused imaging/viewer code,
  upstream tests, tutorials, Python bindings, and documentation assets while
  preserving native scene-data functionality.
- Overall: Fixes additional Windows header conflicts and extracts the bundled
  library using R's internal archive reader, without requiring an external xz.
- Overall: Fixes sanitizer failures in native hashing and macOS initialization,
  reclaims unused layer identities, and corrects uninitialized scalar removal.
  Updates deprecated APIs and resolves compiler portability warnings.
- Overall: Includes the required type-traits header for tracing with newer libc++.
- Overall: Fixes strict Clang warnings in native token macros, registration, and
  destructor definitions while preserving C++17 support. Avoids a deprecated
  standard-library binding path when scheduling tasks without arguments.
- Overall: Prevents out-of-range integer conversions when compressing floating-point
  arrays near the 32-bit integer limits.
- Overall: Fixes sanitizer errors in callback dispatch, trace-node alignment,
  empty crate reads, and crate spec initialization. Rejects oversized native
  arrays before allocation.
- Overall: Links Windows path utilities and keeps invalid-input errors safe when
  Rcpp and the package use different C++ standard libraries.
