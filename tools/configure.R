#' @param input_archive Full, CRAN-patched OpenUSD 26.08 source archive.
#' @param output_archive Destination for the native-only source archive.
#' @return Paths removed while preparing the archive.
#' @keywords internal
prepare_openusd_archive = function(input_archive, output_archive) {
  input_archive = normalizePath(input_archive, winslash = "/", mustWork = TRUE)
  output_archive = file.path(
    normalizePath(dirname(output_archive), winslash = "/", mustWork = TRUE),
    basename(output_archive)
  )
  if (identical(input_archive, output_archive)) {
    stop("Use a separate destination to preserve the input archive.")
  }
  members = sub(
    "^\\./",
    "",
    utils::untar(input_archive, list = TRUE, tar = "internal")
  )
  if (
    !length(members) ||
      any(!grepl("^OpenUSD-26[.]08(/|$)", members)) ||
      any(grepl("(^|/)\\.\\.(/|$)", members)) ||
      any(grepl("\\", members, fixed = TRUE))
  ) {
    stop("Expected an OpenUSD-26.08 archive with relative member paths.")
  }
  # Stage the result beside its destination. Validation must finish before
  # a previously prepared archive can be replaced.
  work_dir = tempfile("openusd-prepare-", tmpdir = dirname(output_archive))
  if (!dir.create(work_dir)) {
    stop("Cannot create archive preparation directory.")
  }
  previous_dir = getwd()
  on.exit(
    {
      setwd(previous_dir)
      unlink(work_dir, recursive = TRUE)
    },
    add = TRUE
  )
  if (utils::untar(input_archive, exdir = work_dir, tar = "internal") != 0L) {
    stop("Cannot extract OpenUSD sources.")
  }
  if (any(nzchar(Sys.readlink(file.path(work_dir, members))), na.rm = TRUE)) {
    stop("OpenUSD source archive must not contain symbolic links.")
  }
  result = prune_openusd_sources(file.path(work_dir, "OpenUSD-26.08"))
  setwd(work_dir)
  files = sort(
    list.files(
      "OpenUSD-26.08",
      recursive = TRUE,
      all.files = TRUE,
      full.names = TRUE,
      no.. = TRUE
    ),
    method = "radix"
  )
  prepared = file.path(work_dir, "prepared.tar.xz")
  utils::tar(
    prepared,
    files = files,
    compression = "xz",
    compression_level = 9L,
    tar = "internal"
  )
  if (!file.rename(prepared, output_archive)) {
    stop("Cannot replace the output archive: ", output_archive)
  }
  manifest = paste0(output_archive, ".removed.tsv")
  utils::write.table(
    result$removed,
    manifest,
    sep = "\t",
    quote = FALSE,
    row.names = FALSE,
    col.names = FALSE
  )
  message("Prepared native-only OpenUSD archive: ", output_archive)
  message("Removed ", nrow(result$removed), " files; manifest: ", manifest)
  invisible(result$removed)
}

#' @param source_dir Extracted, CRAN-patched OpenUSD 26.08 source directory.
#' @return A list indicating changes and recording removed file paths and sizes.
#' @keywords internal
prune_openusd_sources = function(source_dir) {
  # These components are disabled by the configuration below. Keep core
  # schemas, platform headers and runtime plugin resources.
  excluded_roots = c(
    "extras",
    "pxr/imaging",
    "pxr/usdImaging",
    "pxr/exec",
    "pxr/usdValidation",
    "pxr/external",
    "pxr/bin",
    "pxr/base/bin",
    "pxr/usd/bin",
    "pxr/usd/usdMtlx",
    "pxr/usd/plugin/usdAbc",
    "pxr/usd/plugin/usdDraco",
    "pxr/usd/plugin/sdrOsl"
  )
  excluded_directories = c(
    "test",
    "tests",
    "testenv",
    "examples",
    "tutorials",
    "docs",
    "userDoc",
    "images",
    "codegenTemplates",
    "doxygen",
    "__pycache__"
  )
  codegen_resources = paste0(
    "    RESOURCE_FILES\n",
    paste0(
      "        codegenTemplates/",
      c(
        "__init__.py",
        "api.h",
        "CMakeLists.txt",
        "generatedSchema.classes.txt",
        "generatedSchema.module.h",
        "module.cpp",
        "moduleDeps.cpp",
        "plugInfo.json:no_subst",
        "schema.usda",
        "schemaUserDoc.usda",
        "schemaClass.cpp",
        "schemaClass.h",
        "tokens.cpp",
        "tokens.h",
        "wrapSchemaClass.cpp",
        "wrapTokens.cpp"
      ),
      collapse = "\n"
    ),
    "\n\n"
  )
  cmake_removals = list(
    "CMakeLists.txt" = "add_subdirectory(extras)\n",
    "pxr/CMakeLists.txt" = "add_subdirectory(external)\n",
    "pxr/usd/plugin/CMakeLists.txt" = "add_subdirectory(sdrOsl)\n",
    "pxr/usd/ar/CMakeLists.txt" = "        testenv/TestArURIResolver_plugin.h\n",
    "pxr/usd/usd/CMakeLists.txt" = c(
      "        testenv/TestUsdResolverChangedResolver.h\n",
      codegen_resources
    )
  )
  marker = "# libopenusd: omit components removed from the native-only source bundle.\n"
  updates = list()
  # Validate every edit before removing anything. Prepared bundles are
  # accepted without rewriting files or invalidating existing build caches.
  for (relative in names(cmake_removals)) {
    path = file.path(source_dir, relative)
    if (!file.exists(path)) {
      stop("Missing OpenUSD build file: ", relative)
    }
    contents = readChar(path, file.info(path)$size, useBytes = TRUE)
    prepared = startsWith(contents, marker)
    for (pattern in cmake_removals[[relative]]) {
      matches = gregexpr(pattern, contents, fixed = TRUE)[[1L]]
      if (prepared) {
        if (matches[1L] > 0L) stop("Incomplete OpenUSD pruning: ", relative)
      } else {
        if (length(matches) != 1L || matches[1L] < 0L) {
          stop("OpenUSD 26.08 pruning rule no longer matches: ", relative)
        }
        contents = sub(pattern, "", contents, fixed = TRUE)
      }
    }
    if (!prepared) updates[[relative]] = paste0(marker, contents)
  }
  paths = list.files(
    source_dir,
    recursive = TRUE,
    all.files = TRUE,
    include.dirs = TRUE,
    no.. = TRUE
  )
  excluded = rep(FALSE, length(paths))
  for (root in excluded_roots) {
    excluded = excluded | paths == root | startsWith(paths, paste0(root, "/"))
  }
  excluded = excluded |
    grepl(
      paste0("(^|/)(", paste(excluded_directories, collapse = "|"), ")(/|$)"),
      paths
    ) |
    paths == "cmake/macros/test.pre.js"
  filenames = basename(paths)
  notice = grepl("LICENSE|NOTICE|COPYRIGHT", toupper(filenames))
  excluded = excluded |
    (!notice &
      (grepl("[.](py|pyc|pyo|ipynb|dox|md|rst|osl)$|[.]py[.]in$", filenames) |
        grepl("^(wrap|examples_).*\\.cpp$", filenames) |
        filenames == "module.cpp" |
        grepl(".template.", filenames, fixed = TRUE)))
  removed_paths = paths[excluded]
  info = file.info(file.path(source_dir, removed_paths))
  removed = data.frame(
    size = info$size[!info$isdir],
    path = removed_paths[!info$isdir]
  )
  if (length(removed_paths)) {
    targets = file.path(source_dir, removed_paths)
    if (unlink(targets, recursive = TRUE) != 0L || any(file.exists(targets))) {
      stop("Cannot remove unused OpenUSD sources.")
    }
  }
  for (relative in names(updates)) {
    path = file.path(source_dir, relative)
    modified = file.info(path)$mtime
    writeChar(updates[[relative]], path, eos = NULL, useBytes = TRUE)
    Sys.setFileTime(path, modified)
  }
  list(
    changed = length(removed_paths) > 0L || length(updates) > 0L,
    removed = removed
  )
}

# Maintainers recreate the trimmed archive using R alone. Ordinary installation
# continues with configuration below; no archive is rewritten during install.
arguments = commandArgs(trailingOnly = TRUE)
if (length(arguments)) {
  if (length(arguments) != 3L || arguments[1L] != "--prune-vendor") {
    stop(
      "Usage: Rscript tools/configure.R [--prune-vendor INPUT.tar.xz OUTPUT.tar.xz]"
    )
  }
  prepare_openusd_archive(arguments[2L], arguments[3L])
  quit(status = 0L)
}

# Configure the native dependency using the same architecture and compiler as R.
# Respect an explicit override; otherwise inspect every PATH entry before
# trying standard macOS installations that GUI R sessions may not inherit.
cmake_minimum = "3.22"
override = Sys.getenv("CMAKE")
if (nzchar(override)) {
  candidates = c(unname(Sys.which(override)), path.expand(override))
} else {
  directories = strsplit(
    Sys.getenv("PATH"),
    .Platform$path.sep,
    fixed = TRUE
  )[[
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
cmake_versions = character()
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
  cmake_versions = c(cmake_versions, paste0(candidate, " (", version, ")"))
  if (utils::compareVersion(version, cmake_minimum) >= 0) {
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
        " does not identify a working CMake >= ",
        cmake_minimum,
        ". Update or unset CMAKE."
      )
    } else {
      paste0(
        "CMake >= ",
        cmake_minimum,
        " is required. Install it or set CMAKE to a newer executable."
      )
    },
    if (length(cmake_versions)) {
      paste0(" Detected: ", paste(unique(cmake_versions), collapse = "; "), ".")
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
build_source_dir = normalizePath(
  "tools/vendor/cmake",
  winslash = "/",
  mustWork = TRUE
)
source_archive = "tools/vendor/OpenUSD-26.08.tar.xz"
source_digest = unname(tools::md5sum(source_archive))
source_stamp = file.path(source_dir, "libopenusd-source.md5")
# These are generated build trees. Invalidate them when the vendored archive
# changes so local rebuilds cannot silently retain an unpatched native library.
if (
  dir.exists(source_dir) &&
    (!file.exists(source_stamp) ||
      !identical(readLines(source_stamp, warn = FALSE), source_digest))
) {
  unlink(
    c(source_dir, "src/build", "src/usd-install", "inst/usd"),
    recursive = TRUE
  )
}
if (!dir.exists(source_dir)) {
  status = utils::untar(source_archive, exdir = "src", tar = "internal")
  if (status != 0L) {
    stop("Cannot extract OpenUSD sources.")
  }
  if (!file.rename("src/OpenUSD-26.08", source_dir)) {
    stop("Cannot extract OpenUSD sources.")
  }
}
prepared = prune_openusd_sources(source_dir)
if (prepared$changed) {
  unlink(c("src/build", "src/usd-install", "inst/usd"), recursive = TRUE)
}
writeLines(source_digest, source_stamp)

# Rtools provides GNU make and a Unix shell, just like R's package build.
# MinGW Makefiles instead searches for mingw32-make, absent on win-builder.
generator = "Unix Makefiles"
# CMake caches bind to both their source directory and generator.
cache_path = "src/build/CMakeCache.txt"
if (file.exists(cache_path)) {
  cache = readLines(cache_path, warn = FALSE)
  cache_home = sub(
    "^CMAKE_HOME_DIRECTORY:INTERNAL=",
    "",
    grep("^CMAKE_HOME_DIRECTORY:INTERNAL=", cache, value = TRUE)
  )
  cache_generator = sub(
    "^CMAKE_GENERATOR:INTERNAL=",
    "",
    grep("^CMAKE_GENERATOR:INTERNAL=", cache, value = TRUE)
  )
  if (
    !identical(cache_home, build_source_dir) ||
      !identical(cache_generator, generator)
  ) {
    unlink(c("src/build", "src/usd-install", "inst/usd"), recursive = TRUE)
  }
}

parallel_root = system.file(package = "RcppParallel", mustWork = TRUE)
tbb_include = file.path(parallel_root, "include")
# Select the runtime matching RcppParallel's headers, avoiding unrelated system
# TBB installations. The provider package loads its runtime before our DLL.
windows = .Platform$OS.type == "windows"
if (windows) {
  # CRAN's Windows binary ships tbb.dll without an import archive. MinGW can
  # link directly to it. Use the provider's resolver to select the active R
  # architecture and the same runtime that RcppParallel loads.
  tbb_candidates = RcppParallel::tbbLibraryPath("tbb")
} else {
  tbb_candidates = list.files(
    parallel_root,
    "^libtbb\\.(so|dylib)$",
    recursive = TRUE,
    full.names = TRUE
  )
}
if (length(tbb_candidates) != 1L || !file.exists(tbb_candidates)) {
  stop(
    "Expected one existing TBB link library from RcppParallel; found: ",
    paste(tbb_candidates, collapse = ", ")
  )
}
tbb_candidates = normalizePath(tbb_candidates, winslash = "/", mustWork = TRUE)
message("Using TBB: ", tbb_candidates)

# R's compiler setting can include architecture flags. Pass the executable and
# flags separately to CMake; system2 quoting below preserves spaces in paths.
cxx = strsplit(r_config("CXX"), "[[:space:]]+")[[1L]]
cc = strsplit(r_config("CC"), "[[:space:]]+")[[1L]]
build_args = c(
  "-S",
  build_source_dir,
  "-B",
  "src/build",
  "-G",
  generator,
  "-DCMAKE_BUILD_TYPE=Release",
  paste0("-DOPENUSD_SOURCE_DIR=", source_dir),
  "-DBUILD_SHARED_LIBS=OFF",
  "-DPXR_BUILD_MONOLITHIC=ON",
  "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
  # Ship only the native scene-data library. Keep all package choices here,
  # rather than in shell launchers or the vendored CMake wrapper.
  paste0(
    "-DPXR_ENABLE_",
    c(
      "PYTHON_SUPPORT",
      "GL_SUPPORT",
      "METAL_SUPPORT",
      "MATERIALX_SUPPORT",
      "PRECOMPILED_HEADERS"
    ),
    "=OFF"
  ),
  paste0(
    "-DPXR_BUILD_",
    c(
      "TESTS",
      "EXAMPLES",
      "TUTORIALS",
      "USD_TOOLS",
      "IMAGING",
      "USD_IMAGING",
      "USDVIEW",
      "DOCUMENTATION",
      "PYTHON_DOCUMENTATION",
      "EXEC",
      "USD_VALIDATION"
    ),
    "=OFF"
  ),
  "-DPXR_VALIDATE_GENERATED_CODE=OFF",
  "-DPXR_FIND_TBB_IN_CONFIG=OFF",
  "-DPXR_SET_INTERNAL_NAMESPACE=libopenusd_v26_08",
  "-DPXR_OVERRIDE_PLUGINPATH_NAME=LIBOPENUSD_PLUGIN_PATH",
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
  # Nested builds must also disable C/C++ assertions in installed R packages.
  "-DCMAKE_CXX_FLAGS_RELEASE=-DNDEBUG",
  "-DCMAKE_C_FLAGS_RELEASE=-DNDEBUG",
  paste0("-DCMAKE_INSTALL_PREFIX=", package_root, "/src/usd-install"),
  paste0("-DTBB_ROOT_DIR=", parallel_root),
  paste0("-DTBB_INCLUDE_DIR=", tbb_include),
  paste0("-DTBB_tbb_LIBRARY_RELEASE=", tbb_candidates)
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
  build_args = c(
    build_args,
    # Tell FindTBB this is a shared runtime even though its link input is the
    # DLL itself, not a separate .dll.a import library.
    paste0("-DTBB_tbb_SHARED_LIBRARY_RELEASE=", tbb_candidates)
  )
}
status = system2(cmake, shQuote(build_args))
if (status != 0L) {
  stop("OpenUSD CMake configuration failed.")
}

jobs = Sys.getenv("LIBOPENUSD_JOBS", "2")
if (!grepl("^[1-9][0-9]*$", jobs)) {
  stop("LIBOPENUSD_JOBS must be a positive integer.")
}
# Static schema and file-format registrars must survive the final R link.
# Apply whole-archive only to OpenUSD, not to TBB or the system libraries.
openusd_archive = "build/OpenUSD/libusd_m.a"
if (Sys.info()[["sysname"]] == "Darwin") {
  openusd_libs = paste0("-Wl,-force_load,", openusd_archive)
  platform_libs = "-framework Foundation -lm"
} else {
  openusd_libs = paste(
    "-Wl,--whole-archive",
    openusd_archive,
    "-Wl,--no-whole-archive"
  )
  platform_libs = if (windows) {
    "exports.def -lws2_32 -ldbghelp"
  } else {
    "-ldl -pthread -lm"
  }
}
tbb_libs = shQuote(tbb_candidates)
if (!windows) {
  tbb_libs = paste(
    tbb_libs,
    paste0("-Wl,-rpath,", shQuote(dirname(tbb_candidates)))
  )
}

template = readLines(if (windows) "src/Makevars.win.in" else "src/Makevars.in")
replacements = c(
  CMAKE = cmake,
  JOBS = jobs,
  OPENUSD_LIBS = openusd_libs,
  TBB_INCLUDE = shQuote(tbb_include),
  TBB_LIBS = tbb_libs,
  PLATFORM_LIBS = platform_libs,
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
