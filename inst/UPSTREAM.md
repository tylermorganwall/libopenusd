# OpenUSD source provenance

- Release: v26.08
- Source: https://github.com/PixarAnimationStudios/OpenUSD/tree/v26.08
- Archive: https://codeload.github.com/PixarAnimationStudios/OpenUSD/tar.gz/refs/tags/v26.08
- Original SHA-256: 4bccbb95cddda1dbeef2f74a08b9456352f2aa91bfd4578c0c613009c7950149

The source archive in tools/vendor retains the core CMake project, cmake,
pxr, extras, and upstream license/provenance documents. Upstream testenv
data fixtures are excluded; test headers and C++ sources referenced by upstream
CMake files are retained. No upstream implementation files are patched.

OpenUSD is compiled as a position-independent static monolithic library and
linked into the R DLL. Python, imaging, command-line tools, code generation,
examples, tutorials, Exec, and validation tools are disabled. Pre-generated
schema definitions are retained. The namespace and plugin environment variable
are isolated from other OpenUSD distributions loaded in the same process.
