#ifndef LIBOPENUSD_R_API_H
#define LIBOPENUSD_R_API_H

#include <R_ext/Rdynload.h>
#include <Rinternals.h>
#include <R_ext/Error.h>
#include <libopenusd/api.h>

/* Load the libopenusd namespace before calling this, on R's main thread.
 * Retain the returned immutable table for subsequent native/worker calls.
 * Consumers must not unload libopenusd while holding the table or a stage.
 */
static inline const libopenusd_api_v1 *libopenusd_get_api(void) {
    typedef const libopenusd_api_v1 *(*getter_type)(void);
    getter_type getter = (getter_type) R_GetCCallable(
        "libopenusd", "libopenusd_get_api_v1");
    const libopenusd_api_v1 *api = getter();
    if (!api || api->abi_version != 1 || api->struct_size < sizeof(*api)) {
        Rf_error("Incompatible libopenusd native API; rebuild the consuming package.");
    }
    return api;
}

/* Main-thread R API. Return value must be protected before further allocation. */
static inline SEXP libopenusd_inspect(SEXP filename, SEXP selectors, SEXP properties) {
    typedef SEXP (*inspect_type)(SEXP, SEXP, SEXP);
    inspect_type inspect = (inspect_type) R_GetCCallable("libopenusd", "libopenusd_inspect_v1");
    return inspect(filename, selectors, properties);
}

/* Main-thread typed stage authoring. Numeric arrays and metadata go directly to
 * OpenUSD; no text serialization or temporary USDA stage is involved. */
static inline SEXP libopenusd_write_stage(SEXP document, SEXP filename) {
    typedef SEXP (*write_type)(SEXP, SEXP);
    write_type write = (write_type) R_GetCCallable("libopenusd", "libopenusd_write_stage_v1");
    return write(document, filename);
}

#endif
