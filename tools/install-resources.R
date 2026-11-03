# Run from src after the native build. Only schema/plugin data enters the R
# installation; upstream build products and SDK headers stay in the build tree.
source = "usd-install/lib/usd"
destination = "../inst/usd"
if (!file.exists(file.path(source, "plugInfo.json"))) {
  stop("Missing OpenUSD plugin index.")
}
unlink(destination, recursive = TRUE)
dir.create(destination, recursive = TRUE)
files = list.files(source, full.names = TRUE)
if (!all(file.copy(files, destination, recursive = TRUE))) {
  stop("Cannot install USD resources.")
}
