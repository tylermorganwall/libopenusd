#ifndef LIBOPENUSD_API_H
#define LIBOPENUSD_API_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles must be released by the same provider that created them. */
typedef struct libopenusd_stage libopenusd_stage;

/* ABI v1 is immutable. Future incompatible interfaces get a new registered
 * getter. All paths/text use UTF-8; flags are 0 or 1. Functions catch C++
 * exceptions. Failure returns NULL (handles/text) or 0 (status).
 * last_error() is thread-local and valid until the next operation on that
 * thread. free_string() and stage_release() do not clear it.
 *
 * Stage calls may be made from native threads after initialization on R's main
 * thread. Do not mutate or release the same stage concurrently. No function in
 * this table calls R or allocates R objects. Text is owned by the caller and
 * must be released with free_string(), never the caller's malloc implementation.
 */
typedef struct libopenusd_api_v1 {
    unsigned int abi_version;
    size_t struct_size;
    const char *(*version)(void);
    const char *(*last_error)(void);
    libopenusd_stage *(*stage_create)(void);
    libopenusd_stage *(*stage_open)(const char *filename);
    libopenusd_stage *(*stage_from_usda)(const char *text);
    int (*stage_export)(libopenusd_stage *stage, const char *filename, int flatten);
    char *(*stage_to_usda)(libopenusd_stage *stage, int flatten);
    void (*stage_release)(libopenusd_stage *stage);
    void (*free_string)(char *text);
} libopenusd_api_v1;

#ifdef __cplusplus
}
#endif
#endif
