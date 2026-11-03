#' Read and Write OpenUSD Stages
#'
#' Open, compose, and export USD scenes using the native OpenUSD library.
#' Stage handles retain their layers until closed or garbage collected.
#'
#' @param filename Path to a USD file. The extension selects the output format:
#'   `.usda` for text or `.usdc` for binary. Reading also supports USDZ archives.
#' @param text One character string containing a complete USDA layer.
#' @param stage A stage handle returned by these functions.
#' @param flatten Default `FALSE`. Export only the root layer, preserving its
#'   composition arcs. Use `TRUE` to export composed opinions into a single layer.
#' @return `usd_open()`, `usd_create()`, and `usd_from_usda()` return an external
#'   pointer of class `usd_stage`. `usd_to_usda()` returns a character string.
#'   `usd_export()` invisibly returns the output path. `usd_close()` returns
#'   invisible `NULL`.
#' @export
#' @details
#' Handles belong to the current R process and cannot be restored with
#' `saveRDS()`. Export a USD file and reopen it in the next session instead.
#'
#' Export does not copy textures, referenced layers, volumes, or other assets.
#' Root-layer export preserves authored relative asset paths, which must remain
#' valid relative to the destination. Anonymous layers imported from text have
#' no file location; use absolute asset paths or open a file with `usd_open()`
#' when relative references are required. Flattening resolves composition but
#' does not embed external assets. USDZ packaging is not provided by this API.
#'
#' Compiled consumers can include `libopenusd.h` and obtain the versioned
#' `R_GetCCallable()` function table documented in the installed headers.
#' @examples
#' # Write a native binary USD file and reopen it for a later rendering step.
#' scene = usd_from_usda('#usda 1.0\ndef Sphere "Ball" {\n double radius = 2\n}\n')
#' filename = tempfile(fileext = ".usdc")
#' usd_export(scene, filename)
#' restored = usd_open(filename)
#' cat(usd_to_usda(restored))
#' usd_close(scene)
#' usd_close(restored)
#' unlink(filename)
usd_open = function(filename) {
  filename = normalizePath(path.expand(filename), mustWork = TRUE)
  .Call(C_libopenusd_stage_open_R, filename)
}

#' @rdname usd_open
#' @export
usd_create = function() {
  .Call(C_libopenusd_stage_create_R)
}

#' @rdname usd_open
#' @export
usd_from_usda = function(text) {
  .Call(C_libopenusd_stage_from_usda_R, text)
}

#' @rdname usd_open
#' @export
usd_to_usda = function(stage, flatten = FALSE) {
  .Call(C_libopenusd_stage_to_usda_R, stage, flatten)
}

#' @rdname usd_open
#' @export
usd_export = function(stage, filename, flatten = FALSE) {
  if (
    !is.character(filename) ||
      length(filename) != 1L ||
      is.na(filename) ||
      !nzchar(filename)
  ) {
    stop("`filename` must be one non-empty path.")
  }
  filename = path.expand(filename)
  directory = normalizePath(dirname(filename), mustWork = TRUE)
  filename = file.path(directory, basename(filename))
  if (!tolower(tools::file_ext(filename)) %in% c("usd", "usda", "usdc")) {
    stop("Export requires a .usd, .usda, or .usdc filename.")
  }
  .Call(C_libopenusd_stage_export_R, stage, filename, flatten)
  invisible(filename)
}

#' @rdname usd_open
#' @export
usd_close = function(stage) {
  invisible(.Call(C_libopenusd_stage_close_R, stage))
}

#' Report the Bundled OpenUSD Version
#'
#' Return the upstream library release used by the native stage interface.
#'
#' @return A character string containing the OpenUSD release version.
#' @export
#' @examples
#' # Distinguish the upstream release from the R packaging revision.
#' usd_version()
#' packageVersion("libopenusd")
usd_version = function() {
  .Call(C_libopenusd_version_R)
}
