library(libopenusd)
stage = usd_from_usda(
  '#usda 1.0
(defaultPrim = "World")
def Xform "World" (customData = {
 dictionary probe = {
  double precise = 1.0000000000000002
  int[] ids = [1, 2, 3]
  bool enabled = true
  uchar[] bytes = [0, 255]
 }
}) {
 custom asset source = @missing.png@
 def Scope "Child" {}
}'
)
path = tempfile(fileext = ".usdc")
usd_export(stage, path)
result = usd_inspect(path, properties = c("source", "absent"))[[1]]
stopifnot(
  identical(result$path, "/World"),
  identical(result$custom_data$probe$precise, 1 + .Machine$double.eps),
  identical(result$custom_data$probe$ids, 1:3),
  isTRUE(result$custom_data$probe$enabled),
  identical(result$custom_data$probe$bytes, as.raw(c(0, 255))),
  identical(result$properties$source$path, "missing.png"),
  is.null(result$properties$absent),
  identical(usd_inspect(path, "/World/*")[[1]]$path, "/World/Child"),
  length(usd_inspect(path, "/Missing")) == 0
)
usd_close(stage)
unlink(path)
