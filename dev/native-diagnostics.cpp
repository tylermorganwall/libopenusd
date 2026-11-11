// Build against the provider's static OpenUSD archive, bridge and diagnostics.
// Pass the absolute installed usd resource directory, optionally followed by
// "destructor" or "detached", to test each worker failure boundary separately.
#include "bridge.h"
#include "pxr/base/arch/rDiagnostics.h"
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/work/dispatcher.h>
#include <pxr/base/work/detachedTask.h>
#include <future>
#include <memory>
#include <tbb/global_control.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

int main(int argc, char** argv) {
    if (argc < 2 || !libopenusd_initialize(argv[1])) return 1;
    auto* api = libopenusd_get_api_v1();
    auto* stage = api->stage_create();
    if (!stage) return 2;
    api->stage_release(stage);

    char message[2048];
    while (LibOpenUsdDiagnosticNext(message, sizeof(message))) {}
    // File output must remain byte-for-byte complete beyond queue message size.
    FILE* file = std::tmpfile();
    if (!file) return 3;
    std::string long_text(5000, 'x');
    if (LibOpenUsdFprintf(file, "%s", long_text.c_str()) != 5000) return 4;
    std::rewind(file);
    std::string readback(5000, '\0');
    if (std::fread(&readback[0], 1, readback.size(), file) != 5000 || readback != long_text) return 5;
    std::fclose(file);
    if (LibOpenUsdDiagnosticNext(message, sizeof(message))) return 6;

    // Null file targets explicitly select the queue, never process stdio.
    LibOpenUsdFprintf(nullptr, "%s", "buffered file-target diagnostic");
    if (!LibOpenUsdDiagnosticNext(message, sizeof(message)) ||
        std::strcmp(message, "buffered file-target diagnostic")) return 11;
    if (LibOpenUsdDiagnosticNext(message, sizeof(message))) return 12;

    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) workers.emplace_back([] {
        for (int j = 0; j < 50; ++j) LibOpenUsdPrintf("worker diagnostic %d", j);
    });
    for (auto& worker : workers) worker.join();
    int count = 0;
    bool overflow = false;
    while (LibOpenUsdDiagnosticNext(message, sizeof(message))) {
        ++count;
        overflow = overflow || std::strstr(message, "72 additional") != nullptr;
    }
    if (count != 129 || !overflow) return 7;

    tbb::global_control limit(tbb::global_control::max_allowed_parallelism, 2);
    bool caught = false;
    if (argc > 2 && std::strcmp(argv[2], "detached") == 0) {
        struct Finish {
            std::promise<void> promise;
            ~Finish() { promise.set_value(); }
        };
        auto completed = std::make_shared<Finish>();
        auto ready = completed->promise.get_future();
        pxr::WorkRunDetachedTask([completed] {
            TF_FATAL_ERROR("native detached regression failure");
        });
        completed.reset();
        ready.wait();
    } else try {
        pxr::WorkDispatcher tasks;
        tasks.Run([] { TF_FATAL_ERROR("native worker regression failure"); });
        if (argc == 2) tasks.Wait();
    } catch (const std::exception& error) {
        caught = std::strstr(error.what(), "native worker regression failure") != nullptr;
    }
    if (argc == 2 && !caught) return 8;
    // A fatal invariant poisons the provider even if a native destructor caught it.
    if (api->stage_create() != nullptr) return 9;
    if (!std::strstr(api->last_error(), "restart R")) return 10;
    return 0;
}
