library(libopenusd)

expect_error = function(expression, pattern) {
  error = tryCatch(force(expression), error = identity)
  stopifnot(inherits(error, "error"), grepl(pattern, conditionMessage(error)))
}

stopifnot(identical(usd_version(), "26.08"))
workspace = tempfile("libopenusd-tests-")
dir.create(workspace)

# Exercise real USD schemas, composition, time samples, relationships, and
# external asset paths. The provider must preserve these without private blobs.
source = '#usda 1.0
(
    defaultPrim = "World"
    metersPerUnit = 1
    upAxis = "Y"
    startTimeCode = 1
    endTimeCode = 24
    timeCodesPerSecond = 24
)
def Xform "World" {
    def Mesh "Triangle" {
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
        uniform token subdivisionScheme = "none"
        rel material:binding = </World/Looks/Paint>
    }
    def Scope "Looks" {
        def Material "Paint" {
            token outputs:surface.connect = </World/Looks/Paint/Shader.outputs:surface>
            def Shader "Shader" {
                uniform token info:id = "UsdPreviewSurface"
                color3f inputs:diffuseColor = (0.2, 0.4, 0.6)
                token outputs:surface
                asset inputs:testTexture = @textures/paint.png@
            }
        }
    }
    def Camera "Camera" {
        float focalLength = 35
        double3 xformOp:translate.timeSamples = {1: (0, 2, 8), 24: (2, 2, 8)}
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }
    def PointInstancer "People" {
        rel prototypes = </World/Triangle>
        int[] protoIndices = [0, 0]
        point3f[] positions = [(0, 0, 0), (2, 0, 0)]
    }
    def Volume "Fog" {
        rel field:density = </World/Fog/Density>
        def OpenVDBAsset "Density" {
            asset filePath = @volumes/cloud.vdb@
            token fieldName = "density"
        }
    }
}
'
stage = usd_from_usda(source)
canonical = usd_to_usda(stage)
for (extension in c("usda", "usdc")) {
  path = file.path(workspace, paste0("scene.", extension))
  usd_export(stage, path)
  restored = usd_open(path)
  stopifnot(identical(usd_to_usda(restored), canonical))
  # Flattening needs schema resources and must retain the animated transform.
  flattened = usd_to_usda(restored, flatten = TRUE)
  stopifnot(grepl("xformOp:translate.timeSamples", flattened, fixed = TRUE))
  usd_close(restored)
}
magic = readBin(file.path(workspace, "scene.usdc"), "raw", n = 8L)
stopifnot(identical(rawToChar(magic), "PXR-USDC"))

# Relative references must be composed by OpenUSD, not returned as private
# renderer records. Flattening removes the external dependency on this layer.
writeLines(
  '#usda 1.0\ndef Sphere "Ball" {\n double radius = 7\n}\n',
  file.path(workspace, "ball.usda")
)
writeLines(
  '#usda 1.0\ndef Xform "Instance" (\n references = @ball.usda@</Ball>\n) {}\n',
  file.path(workspace, "reference.usda")
)
reference = usd_open(file.path(workspace, "reference.usda"))
stopifnot(grepl("@ball.usda@", usd_to_usda(reference), fixed = TRUE))
flattened_path = file.path(workspace, "flattened.usdc")
usd_export(reference, flattened_path, flatten = TRUE)
flat = usd_open(flattened_path)
stopifnot(grepl("radius = 7", usd_to_usda(flat), fixed = TRUE))
stopifnot(!grepl("@ball.usda@", usd_to_usda(flat), fixed = TRUE))
usd_close(reference)
usd_close(flat)

# Failures must remain R errors, and subsequent native operations must recover.
expect_error(usd_from_usda("not a USD layer"), "Cannot parse")
expect_error(usd_to_usda(stage, NA), "TRUE or FALSE")
expect_error(usd_to_usda(NULL), "Invalid or closed")
expect_error(
  usd_export(stage, file.path(workspace, "bad.usdz")),
  "Export requires"
)
empty = usd_create()
stopifnot(grepl("#usda 1.0", usd_to_usda(empty), fixed = TRUE))
usd_close(empty)
expect_error(usd_to_usda(empty), "Invalid or closed")

# External pointers serialized by R intentionally do not represent live stages.
rds = file.path(workspace, "handle.rds")
saveRDS(stage, rds)
expect_error(usd_to_usda(readRDS(rds)), "Invalid or closed")
usd_close(stage)

# Only registered symbols may be invoked through .Call.
expect_error(
  .Call("libopenusd_version_R", PACKAGE = "libopenusd"),
  "not available|not in load table"
)
# At least 16 values enter OpenUSD's compressed scalar-array path.
values = rep(c(2147483648, 2147483520, -2147483648, 0), 8L)
source = paste0(
  '#usda 1.0\ndef Scope "Bounds" {\n float[] values = [',
  paste(format(values, scientific = FALSE, trim = TRUE), collapse = ", "),
  ']\n}\n'
)
stage = usd_from_usda(source)
expected = usd_to_usda(stage)
path = tempfile(fileext = ".usdc")
usd_export(stage, path)
restored = usd_open(path)
actual = usd_to_usda(restored)
usd_close(restored)
usd_close(stage)
unlink(path)
stopifnot(identical(actual, expected))

unlink(workspace, recursive = TRUE)
