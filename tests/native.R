if (requireNamespace("Rcpp", quietly = TRUE)) {
  local({
    # R CMD check supplies a relative startup script. sourceCpp starts another
    # R process in its build directory, where that script does not exist.
    previous_tests = Sys.getenv("R_TESTS", unset = NA_character_)
    on.exit(
      if (is.na(previous_tests)) {
        Sys.unsetenv("R_TESTS")
      } else {
        Sys.setenv(R_TESTS = previous_tests)
      }
    )
    Sys.unsetenv("R_TESTS")
    loadNamespace("libopenusd")
    Rcpp::sourceCpp("cpp/consumer.cpp")
    path = tempfile(fileext = ".usdc")
    stopifnot(registered_consumer_roundtrip(path))
    unlink(path)
  })
}
