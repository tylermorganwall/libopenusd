#' @useDynLib libopenusd, .registration = TRUE, .fixes = "C_"
#' @importFrom Rcpp evalCpp
#' @importFrom RcppParallel RcppParallelLibs
NULL

.onLoad = function(libname, pkgname) {
  resources = system.file(
    "usd",
    package = pkgname,
    lib.loc = libname,
    mustWork = TRUE
  )
  .Call(C_libopenusd_initialize_R, resources)
}
