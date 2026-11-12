# libopenusd

OpenUSD 26.08 packaged as a native R library, with Python disabled. The package
builds the OpenUSD core into one DLL and registers both R calls and a versioned
C API. It supplies USD stage/file-format handling, composition, and schema
resources; a renderer remains responsible for translating its scene model to
and from USD. There is no rayrender dependency or private serialized R payload.

## Installation

Install `RcppParallel` first. A CMake version of at least 3.22 and R's native
compiler toolchain are required. The pinned source is included: installation
requires no downloads and does not invoke Python. For example:

```sh
CMAKE=/opt/homebrew/bin/cmake LIBOPENUSD_JOBS=6 R CMD INSTALL libopenusd
```

`CMAKE` defaults to the executable on PATH; `LIBOPENUSD_JOBS` defaults to 2 to
limit build memory. Linux uses the same build path. Windows includes
`configure.win` and `Makevars.win.in` for Rtools/MinGW; it has not yet been
validated on a Windows host. RcppParallel supplies TBB headers and its runtime,
so another TBB installation is not required.

Configuration lives in `tools/configure.R`; `configure` and `configure.win` are
launchers for that script. The CMake wrapper in `tools/vendor/cmake/` builds only
the vendored OpenUSD static library. R's standard compilation
and linking rules build the package interface and link that archive into the
package shared library. OpenUSD schema data is installed under `usd/`. The public headers contain only an opaque C interface;
consumers do not link their own copy of OpenUSD or include its C++ SDK headers.
The upstream license and third-party notices are retained. This is a development
package, not a claim of CRAN submission readiness.

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

## Registered native interface

Consumer packages declare:

```
Imports: libopenusd
LinkingTo: libopenusd
```

Load the namespace before retrieving the table, for example with
`importFrom(libopenusd, usd_version)` in NAMESPACE. Then, on R's main thread:

```cpp
#include <libopenusd.h>

const libopenusd_api_v1 *usd = libopenusd_get_api();
libopenusd_stage *stage = usd->stage_open("scene.usdc");
if (!stage) {
    // Copy/report usd->last_error() using the consumer's error handling.
}
// Use stage_export() or stage_to_usda(), then:
usd->stage_release(stage);
```

`libopenusd_get_api()` resolves the registered symbol
`libopenusd_get_api_v1` with `R_GetCCallable()`. No OpenUSD or provider linker
flags are needed. The table contains creation, file opening, USDA import,
file/text export, error access, and matching release functions. This is a stage
I/O API. Typed stage construction is provided separately by
`libopenusd_write_stage_v1`, without a text intermediary.

Failures return NULL/0 and a thread-local error message; C++ exceptions do not
cross the ABI. Free text with `free_string()` and handles with `stage_release()`.
After namespace loading/table acquisition on R's main thread, table operations
can run on native worker threads. Concurrent writes to the same stage are not
supported. Do not unload the provider while handles or function pointers remain.
The full contract is in `inst/include/libopenusd/api.h`.

## Validation

Tests cover USDA/USDC round-trips, meshes, material connections, camera time
samples, point instancing, volume asset references, relative-layer composition,
flattening, error recovery, closed handles, and a separately compiled consumer
using only `R_GetCCallable()`. Source installation, relocation, and fresh-session
checks should accompany any upstream or build-system update.

`usd_inspect()` reads typed custom data, selected default-time attributes, and
child paths. Native R consumers can call `libopenusd_inspect()` from the installed
`libopenusd.h` header after loading the namespace. This separate, registered
`libopenusd_inspect_v1` interface uses R objects and runs on R's main thread; the
existing versioned C stage API remains unchanged. Unsupported metadata types
report an error instead of being silently converted.


## Typed stage authoring

Native R consumers can call `libopenusd_write_stage(document, filename)` from
`libopenusd.h` after loading the namespace. The registered
`libopenusd_write_stage_v1` entry point accepts R objects on R's main thread.
It constructs a stage directly through OpenUSD and exports the requested file
format. A `.usdc` export never creates or parses a USDA text stage. Arrays are
copied directly into native typed storage; the composed stage resides in memory.

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
while retaining type and attributes. This uses no RDS payload or text encoding.
Environments, functions, and external pointers are rejected. Exceptions and
OpenUSD diagnostics are returned as R errors after releasing native resources.

Native operations use at most two threads by default. Set
`LIBOPENUSD_NUM_THREADS` to a positive integer to request another limit.
The limit is scoped to each operation and respects stricter limits imposed by
other packages sharing the TBB runtime. Retrieve buffered warnings and debug
output with `usd_messages()`. After a native invariant failure, restart R before
using the provider again.

## Latest source build

[Download the latest successful main source build](https://github.com/tylermorganwall/libopenusd/releases/tag/source-main).
Open that page and use its **Download the R source tarball** link. The filename
and release notes identify the package version, branch, full commit SHA, and
build run; the notes also include the tarball's SHA-256 checksum. These are
development source packages, not CRAN releases or compiled binaries.

[Build R source package](https://github.com/tylermorganwall/libopenusd/actions/workflows/source-build.yaml)
runs `R CMD build` for every branch push, pull requests targeting main, and
manual runs. Every successful run has a downloadable Actions artifact retained
for 30 days. Successful main-branch pushes and manual runs also publish durable
release assets. Pull requests and other branches cannot replace main's download.

The release page selects a new download only after the uploaded tarball has
been downloaded again and its checksum verified. Older runs cannot replace a
newer main build, and previous assets remain available at their original URLs.
Use the linked package asset rather than GitHub's autogenerated source archives.
