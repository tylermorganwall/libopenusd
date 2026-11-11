# OpenUSD source

`OpenUSD-26.08.tar.xz` contains Pixar OpenUSD v26.08, with the local R embedding
changes recorded in `cran-portability.patch`. Original copyright and license
notices are retained. The patch is already applied in the archive: clean builds
must use this archive, not a previous unpatched `src/OpenUSD` extraction.

Local changes buffer console diagnostics in a bounded worker-safe queue, preserve
file output, replace fatal process termination with C++ failures contained by the
provider, and disable process-wide crash handlers and debugger subprocesses.
Fatal failures invalidate subsequent provider operations. Task-dispatcher cleanup
contains secondary exceptions during unwinding. No R API is called by workers.
The package cannot promise recovery from memory corruption or arbitrary failures
inside third-party noexcept functions; these remain native programming errors.

Compiler-warning suppression is removed, integer format mismatches are corrected,
and source files end with a newline. Packing, visibility and intrinsic pragmas
are retained. The library's algorithms, file formats and upstream license remain.

Windows temporary-file names use independent entropy rather than reseeding the
process-global C RNG. This randomness identifies files, not statistical samples.
Singleton template data definitions are visible to inline callers, and boolean
operations preserve required evaluation without bitwise-boolean diagnostics.

Asynchronous destruction is disabled by default so cleanup stays within the
provider call and its scoped thread limit. Detached tasks also contain failures
and invalidate the provider; they never throw an uncaught worker exception.

Console targets use a null FILE pointer understood only by the diagnostic
adapter. Real file targets retain their output and flush behavior. No adapter
consults or writes the process standard streams, including debug output.

Windows GCC and Clang builds use the compiler constructor/destructor attributes
selected by `attributes.h`. The source excludes the separate PE section
dispatcher for these compilers so it does not reference undeclared MSVC-path
types or run a second initialization mechanism.

Windows error messages use `strerror_s` with every compiler, including GCC and
Clang. Diagnostic format checking follows MinGW's stdio format dialect so UCRT
size formats such as `%zu` are recognized without disabling compiler warnings.

## Native-only source bundle

The archive retains the core native USD libraries and their schema/plugin
resources. It omits upstream tests, examples, tutorials, documentation artwork,
imaging/viewer code, the disabled Exec and validation components, optional
Alembic/Draco/OSL/MaterialX plugins, and Python bindings and scripts. Native core
sources, cross-platform headers, LICENSE.txt and NOTICE.txt are retained.
Five CMake files omit traversal into removed components, two test-only library
headers, and installation of Python code-generation templates. The R package's executable tests remain.

Recreate this subset from a full OpenUSD 26.08 archive after applying
`cran-portability.patch`, running from the R package root:

```sh
Rscript tools/configure.R --prune-vendor /path/to/patched-OpenUSD-26.08.tar.xz tools/vendor/OpenUSD-26.08.tar.xz
```

The archive-preparation action records removed paths in a sibling `.removed.tsv`
file, excluded from the R source package. The same R pruning action runs on the
extracted sources during configuration and leaves already prepared sources
unchanged. Python is not used for preparation or installation. The source subset
is for libopenusd's configured native build; disabled upstream features are not
shipped.

## R-hub compiler and sanitizer fixes

The native bundle uses standard unordered containers and `readdir`, includes
Windows sharing declarations, and avoids redefining Windows SDK constants.
Diagnostic macros accept omitted optional arguments in pedantic C++17 builds.
Pointer formats, object lifetimes, small-array sorting, trace-node alignment,
and uninitialized values are corrected without disabling compiler warnings.
Unused private crash-handler helpers are removed; platform-specific helpers
are only compiled on the platforms that use them.

Unaligned hash input uses OpenUSD's existing aligned-copy path. Cached Sdf
identities with no remaining handles are deleted when their registry forgets
them. Live handles retain their detached identities until the final release.

On macOS, only the constructor/destructor table descriptors carry Clang's
`no_sanitize("address")` attribute. This preserves the contiguous linker-table
layout by preventing redzones between descriptors. The dispatcher, callbacks,
and other library globals remain instrumented. See the [Clang documentation](https://clang.llvm.org/docs/AddressSanitizer.html#disabling-instrumentation-with-attribute-no-sanitize-address).

The native-only configuration supports CMake 3.22, including the 3.22.1
version installed on Ubuntu 22.04 R-hub runners. The minimum-version range
retains upstream's CMake 3.27 policy behavior on newer CMake installations.
Optional upstream build modes outside this package's configuration are not
covered by this lower requirement. Windows symbol lookup explicitly converts
the function pointer returned by GetProcAddress to the library API's void pointer.

The tracing event header includes `<type_traits>` directly for `std::aligned_storage`,
rather than relying on transitive standard-library includes removed in newer libc++.
