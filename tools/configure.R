# Configure the native dependency using the same architecture and compiler as R.
# Respect an explicit override; otherwise inspect every PATH entry before
# trying standard macOS installations that GUI R sessions may not inherit.
override = Sys.getenv("CMAKE")
if (nzchar(override)) {
  candidates = c(unname(Sys.which(override)), path.expand(override))
} else {
  directories = strsplit(Sys.getenv("PATH"), .Platform$path.sep, fixed = TRUE)[[
    1L
  ]]
  candidates = file.path(
    directories,
    if (.Platform$OS.type == "windows") "cmake.exe" else "cmake"
  )
  if (Sys.info()[["sysname"]] == "Darwin") {
    candidates = c(
      candidates,
      "/opt/homebrew/bin/cmake",
      "/usr/local/bin/cmake",
      "/Applications/CMake.app/Contents/bin/cmake"
    )
  }
}
cmake = ""
for (candidate in unique(candidates[nzchar(candidates)])) {
  if (!file.exists(candidate)) {
    next
  }
  output = tryCatch(
    suppressWarnings(system2(
      candidate,
      "--version",
      stdout = TRUE,
      stderr = TRUE
    )),
    error = function(error) character()
  )
  if (!is.null(attr(output, "status"))) {
    next
  }
  version_line = grep("^cmake version [0-9]+[.][0-9]+", output, value = TRUE)
  if (!length(version_line)) {
    next
  }
  version = sub("^cmake version ([0-9.]+).*$", "\\1", version_line[1L])
  if (utils::compareVersion(version, "3.27") >= 0) {
    cmake = normalizePath(candidate, winslash = "/", mustWork = TRUE)
    break
  }
}
if (!nzchar(cmake)) {
  stop(
    if (nzchar(override)) {
      paste0(
        "CMAKE=",
        override,
        " does not identify a working CMake >= 3.27. Update or unset CMAKE."
      )
    } else {
      "CMake >= 3.27 is required. Install it or set CMAKE to a newer executable."
    }
  )
}
message("Using CMake ", version, ": ", cmake)

#' @param name The compiler or flags variable to query from R.
#' @return The configured value as one character string.
#' @keywords internal
r_config = function(name) {
  r = file.path(
    R.home("bin"),
    if (.Platform$OS.type == "windows") "R.exe" else "R"
  )
  value = system2(r, c("CMD", "config", name), stdout = TRUE)
  if (!is.null(attr(value, "status"))) {
    stop("Cannot query R compiler setting: ", name)
  }
  paste(value, collapse = " ")
}

package_root = normalizePath(".", winslash = "/", mustWork = TRUE)
source_dir = file.path(package_root, "src", "OpenUSD")
if (!dir.exists(source_dir)) {
  utils::untar("tools/vendor/OpenUSD-26.08.tar.xz", exdir = "src")
  if (!file.rename("src/OpenUSD-26.08", source_dir)) {
    stop("Cannot extract OpenUSD sources.")
  }
}

parallel_root = system.file(package = "RcppParallel", mustWork = TRUE)
tbb_include = file.path(parallel_root, "include")
# Select the runtime matching RcppParallel's headers, avoiding unrelated system
# TBB installations. The provider package loads its runtime before our DLL.
windows = .Platform$OS.type == "windows"
pattern = if (windows) {
  "^(lib)?tbb(12)?\\.(dll\\.a|a)$"
} else {
  "^libtbb\\.(so|dylib)$"
}
tbb_candidates = list.files(
  parallel_root,
  pattern,
  recursive = TRUE,
  full.names = TRUE
)
if (length(tbb_candidates) != 1L) {
  stop(
    "Expected one TBB link library in RcppParallel; found: ",
    paste(tbb_candidates, collapse = ", ")
  )
}

# R's compiler setting can include architecture flags. Pass the executable and
# flags separately to CMake; system2 quoting below preserves spaces in paths.
cxx = strsplit(r_config("CXX"), "[[:space:]]+")[[1L]]
cc = strsplit(r_config("CC"), "[[:space:]]+")[[1L]]
build_args = c(
  "-S",
  "src",
  "-B",
  "src/build",
  "-G",
  if (windows) "MinGW Makefiles" else "Unix Makefiles",
  "-DCMAKE_BUILD_TYPE=Release",
  paste0("-DCMAKE_CXX_COMPILER=", cxx[1L]),
  paste0("-DCMAKE_C_COMPILER=", cc[1L]),
  paste0(
    "-DCMAKE_CXX_FLAGS=",
    paste(
      c(cxx[-1L], r_config("CXXFLAGS"), r_config("CPPFLAGS")),
      collapse = " "
    )
  ),
  paste0(
    "-DCMAKE_C_FLAGS=",
    paste(c(cc[-1L], r_config("CFLAGS"), r_config("CPPFLAGS")), collapse = " ")
  ),
  "-DCMAKE_CXX_FLAGS_RELEASE=",
  "-DCMAKE_C_FLAGS_RELEASE=",
  paste0("-DCMAKE_INSTALL_PREFIX=", package_root, "/src/usd-install"),
  paste0("-DTBB_ROOT_DIR=", parallel_root),
  paste0("-DTBB_INCLUDE_DIR=", tbb_include),
  paste0("-DTBB_tbb_LIBRARY_RELEASE=", tbb_candidates),
  paste0("-DR_INCLUDE_DIR=", R.home("include")),
  paste0(
    "-DRCPP_INCLUDE_DIR=",
    system.file("include", package = "Rcpp", mustWork = TRUE)
  ),
  paste0("-DR_SHARED_SUFFIX=", .Platform$dynlib.ext)
)
if (Sys.info()[["sysname"]] == "Darwin") {
  build_args = c(
    build_args,
    paste0(
      "-DCMAKE_OSX_ARCHITECTURES=",
      if (R.version$arch == "aarch64") "arm64" else R.version$arch
    )
  )
}
if (windows) {
  build_args = c(build_args, paste0("-DR_LIBRARY=", R.home("bin"), "/R.dll"))
}
status = system2(cmake, shQuote(build_args))
if (status != 0L) {
  stop("OpenUSD CMake configuration failed.")
}

jobs = Sys.getenv("LIBOPENUSD_JOBS", "2")
if (!grepl("^[1-9][0-9]*$", jobs)) {
  stop("LIBOPENUSD_JOBS must be a positive integer.")
}
template = readLines(if (windows) "src/Makevars.win.in" else "src/Makevars.in")
replacements = c(
  CMAKE = cmake,
  JOBS = jobs,
  SUFFIX = .Platform$dynlib.ext,
  R_ARCH_BIN = Sys.getenv("R_ARCH_BIN"),
  RSCRIPT = if (windows) "Rscript.exe" else "Rscript"
)
for (key in names(replacements)) {
  template = gsub(
    paste0("@", key, "@"),
    replacements[[key]],
    template,
    fixed = TRUE
  )
}
writeLines(template, if (windows) "src/Makevars.win" else "src/Makevars")
