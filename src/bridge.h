#ifndef LIBOPENUSD_BRIDGE_H
#define LIBOPENUSD_BRIDGE_H
#include <libopenusd/api.h>
extern "C" const libopenusd_api_v1 *libopenusd_get_api_v1();
int libopenusd_initialize(const char *resource_path);
#endif
