library(libopenusd)

local({
  # Thread settings fail as ordinary R errors without damaging the provider.
  previous = Sys.getenv("LIBOPENUSD_NUM_THREADS", unset = NA_character_)
  on.exit(
    if (is.na(previous)) {
      Sys.unsetenv("LIBOPENUSD_NUM_THREADS")
    } else {
      Sys.setenv(LIBOPENUSD_NUM_THREADS = previous)
    }
  )
  for (value in c("0", "-1", "abc", "2x", "99999999999999999999")) {
    Sys.setenv(LIBOPENUSD_NUM_THREADS = value)
    failure = tryCatch(usd_create(), error = identity)
    stopifnot(inherits(failure, "error"))
    stopifnot(grepl("positive integer", conditionMessage(failure)))
  }
  Sys.setenv(LIBOPENUSD_NUM_THREADS = "1")
  stage = usd_create()
  stopifnot(grepl("#usda", usd_to_usda(stage), fixed = TRUE))
  usd_close(stage)
})

local({
  # TF_DEBUG=help formerly called exit(0) while loading the DLL. Verify the
  # child reaches a marker after loading and retrieves help through the queue.
  error_file = tempfile()
  on.exit(unlink(error_file))
  expression = paste0(
    ".libPaths(",
    paste(deparse(.libPaths()), collapse = ""),
    "); ",
    "Sys.setenv(TF_DEBUG = 'help'); library(libopenusd); ",
    "stopifnot(any(grepl('TF_DEBUG', usd_messages(), fixed = TRUE))); ",
    "stopifnot(identical(usd_messages(), character())); cat('alive\n')"
  )
  result = system2(
    file.path(R.home("bin"), "Rscript"),
    c("--vanilla", "-e", shQuote(expression)),
    env = "R_TESTS=",
    stdout = TRUE,
    stderr = error_file
  )
  stopifnot(is.null(attr(result, "status")), identical(result, "alive"))
  stopifnot(file.info(error_file)$size == 0)
})
