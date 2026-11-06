library(libopenusd)
write_stage = function(document, filename) {
  symbol = getNativeSymbolInfo(
    "libopenusd_write_stage_R",
    PACKAGE = getLoadedDLLs()[["libopenusd"]]
  )
  .Call(symbol, document, filename)
}
attribute = function(type, name, value = NULL, ...) {
  list(
    kind = "attribute",
    type = type,
    name = name,
    value = value,
    custom = FALSE,
    uniform = FALSE,
    ...
  )
}
scene = list(
  default_prim = "/World",
  fps = 24,
  end_time = 1,
  prims = list(list(
    kind = "prim",
    type = "Xform",
    name = "World",
    metadata = list(customData = list(label = "native stage")),
    body = list(
      attribute("point3f[]", "points", matrix(c(1, 2, 3, 4, 5, 6), ncol = 3)),
      attribute(
        "matrix4d",
        "xformOp:transform",
        diag(4),
        times = c(0, 1),
        samples = list(diag(4), diag(c(2, 3, 4, 1)))
      ),
      attribute("token[]", "xformOpOrder", "xformOp:transform"),
      attribute(
        "float",
        "inputs:value",
        0.25,
        connections = "/World.outputs:value"
      ),
      attribute("float", "outputs:value"),
      list(kind = "relationship", name = "target", targets = "/World")
    )
  ))
)
path = tempfile(fileext = ".usdc")
write_stage(scene, path)
stopifnot(identical(rawToChar(readBin(path, "raw", 8)), "PXR-USDC"))
stage = usd_open(path)
text = usd_to_usda(stage)
usd_close(stage)
stopifnot(
  grepl("(1, 3, 5), (2, 4, 6)", text, fixed = TRUE),
  grepl("xformOp:transform.timeSamples", text, fixed = TRUE),
  grepl("inputs:value.connect = </World.outputs:value>", text, fixed = TRUE),
  grepl("target = </World>", text, fixed = TRUE),
  identical(usd_inspect(path)[[1]]$custom_data$label, "native stage")
)
unlink(path)

invalid = scene
invalid$prims[[1]]$body[[1]]$value[1] = NA_real_
error = tryCatch(write_stage(invalid, path), error = identity)
stopifnot(inherits(error, "error"), !file.exists(path))
write_stage(scene, path)
unlink(path)
