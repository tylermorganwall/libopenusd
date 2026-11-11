# CRAN pre-submission review

Reviewed 2026-11-07 on macOS arm64 with R 4.6.0.

## DESCRIPTION and dependencies

DESCRIPTION passes strict metadata validation. Version 26.8-3 retains upstream
OpenUSD's calendar version 26.08 and the existing packaging revision. Rcpp is
used by the typed inspection and authoring wrappers; RcppParallel supplies the
shared TBB runtime. Both remain necessary. The package provides a compiled
scene-data API; its scope is described accordingly in the concise CRAN comments.

## Native-code corrections

- Console diagnostics use a bounded, thread-safe queue retrieved by
  `usd_messages()`. Worker threads never call the R API. Real file output is
  preserved, including records longer than the diagnostic buffer.
- OpenUSD fatal diagnostics and double-conversion invariant failures throw
  native exceptions. Registered entry points contain them, and a fatal failure
  invalidates subsequent provider operations. Task cleanup and detached work
  contain secondary failures rather than allowing exceptions to escape workers.
- Debug help is queued without exiting R. Process-wide crash handlers, debugger
  attachment, and external crash-report subprocesses are disabled for embedding.
- Each provider operation uses a scoped two-thread limit by default, respecting
  stricter shared TBB controls. A positive `LIBOPENUSD_NUM_THREADS` overrides it.
  Asynchronous destruction is disabled by default to keep cleanup in that scope.
- Diagnostic-suppressing pragmas and compiler flags are removed. Integer format
  mismatches, template-definition visibility, boolean diagnostics, and missing
  final newlines are corrected. ABI packing and necessary intrinsic pragmas remain.
- Windows temporary-file names use independent entropy without reseeding the
  process-global C RNG. This identifies files; it is not statistical sampling.

Changes are already applied in `tools/vendor/OpenUSD-26.08.tar.xz` and recorded
in `tools/vendor/cran-portability.patch`. Applying that patch to the original
archive reproduces every file of the modified archive exactly. Configure checks
an archive checksum and regenerates stale extracted source and build trees.
Original copyright and license notices are retained.

The final shared library has no direct imports of abort/exit, printf/fprintf,
fputs/puts/putchar, C++ cout/cerr, or standard-output/error handles. Buffer
formatting and R console functions remain appropriate. `__cxa_guard_abort`
cancels C++ static initialization; it does not abort the process. Upstream code
outside the compiled component can still contain console/debug facilities;
source-text matches alone do not establish a reachable package call.

Native invariant failures are programming errors, not a general recovery
contract for memory corruption or arbitrary exceptions in third-party noexcept
code. Restart R after a reported invariant failure. Ordinary input/I/O errors
remain recoverable.

## Validation

Final installation followed by `R CMD check --as-cran --no-manual
--install=check:<installation-log> --library=<test-library>`: 0 errors,
0 warnings, 1 NOTE (incoming review: new submission, custom license, size).
The compiled-code check no longer flags standard-output/error handles; a manual
shared-library import audit supplements the installed-package check.
All five R test scripts (author, diagnostics, inspect, native, stage)
and executable examples pass. Standalone native tests cover concurrent queue
overflow, untruncated file output, worker fatal failures with explicit waits,
destructor waits, detached tasks, and rejection of operations after failure.
R's pragma checker reports no findings in the expanded source tree.

## Remaining review items

CRAN incoming checks flag the custom `License: file LICENSE` and approximately
20 MB archive. The package retains upstream's Tomorrow Open Source Technology
License 1.0. The checker's "Non-FOSS" classification requires review; it is not
by itself a legal conclusion. The license has not been relabeled. Windows and
Linux have not been locally tested.

Development native test sources are retained in `dev/`. Detailed logs are in
`/var/folders/19/j71hqsgx1jjg5kb0nxf58b200000gn/T/native-cran-fixes-hji90jbw`.

References: [Writing R Extensions](https://cran.r-project.org/doc/manuals/r-release/R-exts.html#Writing-portable-packages)
and [CRAN Repository Policy](https://cran.r-project.org/web/packages/policies.html).
