#ifndef LIBOPENUSD_OPERATION_H
#define LIBOPENUSD_OPERATION_H
#include "pxr/base/arch/rDiagnostics.h"
#include <tbb/global_control.h>
#include <cstdlib>
#include <cerrno>
#include <climits>
// Scoped control respects stricter limits from other users of RcppParallel's
// shared scheduler and releases our limit when this operation ends.
class LibOpenUsdOperation {
    static unsigned long thread_count() {
        LibOpenUsdCheckFailure();
        const char* value = std::getenv("LIBOPENUSD_NUM_THREADS");
        if (!value) return 2;
        char* end = nullptr;
        errno = 0;
        unsigned long count = std::strtoul(value, &end, 10);
        if (*value < '0' || *value > '9' || *end || errno || count < 1 || count > INT_MAX)
            throw std::invalid_argument("LIBOPENUSD_NUM_THREADS must be a positive integer.");
        return count;
    }
    tbb::global_control limit;
public:
    LibOpenUsdOperation() : limit(tbb::global_control::max_allowed_parallelism, thread_count()) {}
};
#endif
