#' Inspect Typed USD Metadata
#'
#' Read prim custom data and selected properties through native OpenUSD queries.
#' Values retain their numeric precision and array lengths.
#' @param filename Path to a USD stage.
#' @param selectors Default `""`. Prim paths to inspect. An empty string selects
#'   the default prim; a path ending in `/*` selects its immediate children.
#' @param properties Default `character()`. Attribute names to read at default time.
#' @return A list of prim records with paths, types, custom data, properties, and
#'   child paths. Asset values contain authored and resolved paths. Missing prims
#'   are omitted and missing properties return `NULL`.
#' @export
#' @details
#' Supports dictionaries, boolean/integer/float/double/string/token scalars and
#' arrays, unsigned-byte arrays, and asset paths. Other value types fail explicitly.
#' This API and its registered `libopenusd_inspect_v1` native entry point use R
#' objects and must only be called on R's main thread.
#' @examples
#' # Read a custom scalar without converting through a text serialization.
#' scene = usd_from_usda('#usda 1.0\ndef Xform "World" (customData = { int count = 4 }) {}')
#' path = tempfile(fileext = ".usdc")
#' usd_export(scene, path)
#' usd_inspect(path, "/World")[[1]]$custom_data$count
#' usd_close(scene)
#' unlink(path)
usd_inspect = function(filename, selectors = "", properties = character()) {
  filename = normalizePath(path.expand(filename), mustWork = TRUE)
  .Call(C_libopenusd_inspect_R, filename, selectors, properties)
}
