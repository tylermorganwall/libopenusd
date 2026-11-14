# libopenusd

libopenusd provides OpenUSD 26.08 through R functions and a versioned C API.
It supplies USD stage and file-format handling, composition, and schema resources;
a renderer remains responsible for translating its scene model to and from USD.

## Installation

Source installation requires CMake 3.22 or newer and R's native compiler toolchain.

```r
install.packages("remotes")
remotes::install_github("tylermorganwall/libopenusd")
```

## R interface

```r
library(libopenusd)
scene = usd_from_usda('#usda 1.0\ndef Sphere "Ball" {\n double radius = 2\n}\n')
usd_export(scene, "scene.usdc")
usd_close(scene)

# In another session, reopen the standard USD file.
scene = usd_open("scene.usdc")
cat(usd_to_usda(scene))
usd_close(scene)
```

`usd_create()` creates an empty in-memory stage. `usd_export()` writes USDA,
USDC, or USD selected by the extension. `flatten = TRUE` composes referenced
layers into one layer; the default preserves the root layer and its composition
arcs. `usd_open()` also reads USDZ through OpenUSD. USDZ packaging is not exposed.

Export preserves standard USD data, including geometry, material networks,
instancing, camera time samples, and asset references. It does **not** collect
or copy external textures, volumes, or referenced layers. Relative paths must
remain valid at the export destination. Anonymous text layers have no filesystem
anchor; use absolute asset paths or open an authored file for relative references.
Stage handles are process-local and must not be persisted with `saveRDS()`.

`usd_inspect()` reads typed custom data, selected default-time attributes, and
child paths.

## Registered native interface (package developers only)

This section is for developers adding USD support to an R package.

Downstream packages declare:

```
Imports: libopenusd
LinkingTo: libopenusd
```

For a downstream package named `usdexample`, add these entries to `NAMESPACE`:

```
importFrom(libopenusd, usd_version)
useDynLib(usdexample, .registration = TRUE, .fixes = "C_")
```

Save the following C code as `src/scene.c`. It opens a 3D scene as a USD stage
and returns a handle to R. The finalizer releases the stage when R collects
the handle.

```c
#include <libopenusd.h>
#include <R_ext/Visibility.h>

static const libopenusd_api_v1 *usd = NULL;

static void release_scene(SEXP handle)
{
    libopenusd_stage *stage = R_ExternalPtrAddr(handle);
    if (stage != NULL) {
        usd->stage_release(stage);
        R_ClearExternalPtr(handle);
    }
}

SEXP load_usd_scene(SEXP filename)
{
    if (TYPEOF(filename) != STRSXP || XLENGTH(filename) != 1 ||
        STRING_ELT(filename, 0) == NA_STRING) {
        Rf_error("filename must be a single, non-missing string");
    }
    usd = libopenusd_get_api();

    /* Set up cleanup before opening the scene. */
    SEXP handle = PROTECT(R_MakeExternalPtr(NULL, R_NilValue, R_NilValue));
    R_RegisterCFinalizerEx(handle, release_scene, TRUE);

    libopenusd_stage *stage = usd->stage_open(
        Rf_translateCharUTF8(STRING_ELT(filename, 0)));
    if (stage == NULL) {
        Rf_error("Cannot load USD scene: %s", usd->last_error());
    }
    R_SetExternalPtrAddr(handle, stage);
    UNPROTECT(1);
    return handle;
}

static const R_CallMethodDef call_methods[] = {
    {"load_usd_scene", (DL_FUNC) &load_usd_scene, 1},
    {NULL, NULL, 0}
};

void attribute_visible R_init_usdexample(DllInfo *dll)
{
    R_registerRoutines(dll, NULL, call_methods, NULL, NULL);
    R_useDynamicSymbols(dll, FALSE);
    R_forceSymbols(dll, TRUE);
}
```

After installing `usdexample`, load the `scene.usdc` file created in the R
example above:

```r
library(usdexample)
scene = .Call(usdexample:::C_load_usd_scene, normalizePath("scene.usdc"))
```

`libopenusd_get_api()` resolves the registered symbol
`libopenusd_get_api_v1` with `R_GetCCallable()`. The table contains creation,
file opening, USDA import, file/text export, error access, and matching release
functions. This is a stage I/O API. Typed stage construction is provided separately by
`libopenusd_write_stage_v1`.

Failures return NULL/0 and a thread-local error message; C++ exceptions do not
cross the ABI. Free text with `free_string()` and handles with `stage_release()`.
After namespace loading/table acquisition on R's main thread, table operations
can run on native worker threads. Concurrent writes to the same stage are not
supported. The full contract is in `inst/include/libopenusd/api.h`.

Downstream R packages can call `libopenusd_inspect()` from the installed
`libopenusd.h` header after loading the namespace. This registered
`libopenusd_inspect_v1` interface uses R objects and runs on R's main thread.
Unsupported metadata types report an error instead of being silently converted.

## Typed stage authoring

Downstream R packages can call `libopenusd_write_stage(document, filename)` from
`libopenusd.h` after loading the namespace. The registered
`libopenusd_write_stage_v1` entry point accepts R objects on R's main thread.
It constructs a stage through OpenUSD and exports the requested file format.

The document is a named list with `default_prim` (an absolute prim path), `fps`,
`end_time`, and `prims` (a nested list of nodes). Stages use Y-up, meters, and a
starting time code of zero. Each node has a `kind` and `name`:

- `prim`: `type`, `body` (nested nodes), and `metadata` (named list).
- `attribute`: `type`, `value` (NULL for no default), `custom`, and `uniform`.
  Optional `times` and `samples` author time samples, `connections` supplies
  absolute property paths, and `interpolation` supplies mesh interpolation.
- `relationship`: `targets`, a character vector of absolute USD paths.

Supported attribute types are bool, int, float, double, string, token, asset,
int2, float2, texCoord2f, float3, point3f, normal3f, color3f, vector3f, double3,
quatf, quatd, quath, their array forms, and scalar matrix4d. Tuple matrices have
one tuple per row; matrix4d uses the matrix's actual row/column layout.
Quaternions use real, x, y, z order. Missing and nonfinite property values fail
validation. Prim metadata supports nested named dictionaries; `apiSchemas`
authors a prepended token list.

For exact R metadata, a list with class `usd_r_object` and fields `value`,
`fields`, and `templates` requests typed R encoding. The result retains R type,
attributes, native array data, and explicit missing-value masks. `fields = TRUE`
encodes a named list as dictionary fields; `templates = TRUE` omits field data
while retaining type and attributes. Environments, functions, and external
pointers are not supported.

Native operations use at most two threads by default. Set
`LIBOPENUSD_NUM_THREADS` to a positive integer to request another limit.
Retrieve buffered warnings and debug output with `usd_messages()`. After a native
invariant failure, restart R before using the provider again.
