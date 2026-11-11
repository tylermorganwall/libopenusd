#include "bridge.h"
#include "operation.h"
#include <pxr/pxr.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

#ifdef PXR_PYTHON_SUPPORT_ENABLED
#error "libopenusd must be built without Python support"
#endif

struct libopenusd_stage {
    pxr::UsdStageRefPtr stage;
};

namespace {
thread_local std::string last_error;

// Contain both OpenUSD diagnostics and C++ exceptions at the native ABI boundary.
// TfErrorMark prevents failed parsing from also printing uncaught errors to stderr.
template <typename Operation>
int usd_call(Operation operation) noexcept {
    last_error.clear();
    try {
        LibOpenUsdOperation scope;
        pxr::TfErrorMark errors;
        try {
            operation();
            LibOpenUsdCheckFailure();
            if (!errors.IsClean()) {
                throw std::runtime_error("OpenUSD reported an error.");
            }
        } catch (const std::exception &error) {
            // Prefer OpenUSD's precise location/parse diagnostic to a generic
            // failure from a null stage or false return value.
            last_error = error.what();
            for (const auto &diagnostic : errors) {
                last_error += "\n" + diagnostic.GetCommentary();
            }
            errors.Clear();
            return 0;
        } catch (...) {
            errors.Clear();
            throw;
        }
        return 1;
    } catch (const std::exception &error) {
        last_error = error.what();
    } catch (...) {
        last_error = "Unknown OpenUSD exception.";
    }
    return 0;
}

const char *version() { return "26.08"; }
const char *error_message() { return last_error.c_str(); }

libopenusd_stage *create_stage() {
    std::unique_ptr<libopenusd_stage> result;
    int success = usd_call([&] {
        result.reset(new libopenusd_stage{pxr::UsdStage::CreateInMemory()});
        if (!result->stage) throw std::runtime_error("Cannot create USD stage.");
    });
    return success ? result.release() : nullptr;
}

libopenusd_stage *open_stage(const char *filename) {
    std::unique_ptr<libopenusd_stage> result;
    int success = usd_call([&] {
        if (!filename || !*filename) throw std::invalid_argument("Empty USD filename.");
        result.reset(new libopenusd_stage{pxr::UsdStage::Open(filename)});
        if (!result->stage) throw std::runtime_error("Cannot open USD stage.");
    });
    return success ? result.release() : nullptr;
}

libopenusd_stage *stage_from_usda(const char *text) {
    std::unique_ptr<libopenusd_stage> result;
    int success = usd_call([&] {
        if (!text) throw std::invalid_argument("NULL USDA text.");
        auto layer = pxr::SdfLayer::CreateAnonymous("scene.usda");
        if (!layer || !layer->ImportFromString(text)) {
            throw std::runtime_error("Cannot parse USDA text.");
        }
        result.reset(new libopenusd_stage{pxr::UsdStage::Open(layer)});
        if (!result->stage) throw std::runtime_error("Cannot compose USD stage.");
    });
    return success ? result.release() : nullptr;
}

int export_stage(libopenusd_stage *handle, const char *filename, int flatten) {
    return usd_call([&] {
        if (!handle || !handle->stage) throw std::invalid_argument("Invalid USD stage.");
        if (!filename || !*filename) throw std::invalid_argument("Empty USD filename.");
        if (flatten != 0 && flatten != 1) throw std::invalid_argument("Invalid flatten flag.");
        // Root-layer export preserves composition; stage export bakes composed
        // opinions into one layer. Neither operation packages external assets.
        bool success = flatten ? handle->stage->Export(filename) :
            handle->stage->GetRootLayer()->Export(filename);
        if (!success) throw std::runtime_error("Cannot export USD stage.");
    });
}

char *stage_to_usda(libopenusd_stage *handle, int flatten) {
    std::string text;
    int success = usd_call([&] {
        if (!handle || !handle->stage) throw std::invalid_argument("Invalid USD stage.");
        if (flatten != 0 && flatten != 1) throw std::invalid_argument("Invalid flatten flag.");
        bool exported = flatten ? handle->stage->ExportToString(&text) :
            handle->stage->GetRootLayer()->ExportToString(&text);
        if (!exported) throw std::runtime_error("Cannot export USDA text.");
    });
    if (!success) return nullptr;

    char *result = static_cast<char *>(std::malloc(text.size() + 1));
    if (!result) {
        last_error = "Cannot allocate USDA text buffer.";
        return nullptr;
    }
    std::memcpy(result, text.c_str(), text.size() + 1);
    return result;
}

void release_stage(libopenusd_stage *stage) {
    try { delete stage; }
    catch (const std::exception& error) { LibOpenUsdRecordFailure(error.what()); }
    catch (...) { LibOpenUsdRecordFailure("OpenUSD stage cleanup failed."); }
}
void free_string(char *text) { std::free(text); }

const libopenusd_api_v1 api = {
    1, sizeof(libopenusd_api_v1), version, error_message,
    create_stage, open_stage, stage_from_usda, export_stage, stage_to_usda,
    release_stage, free_string
};
} // namespace

int libopenusd_initialize(const char *resource_path) {
    return usd_call([&] {
        if (!resource_path || !*resource_path) {
            throw std::invalid_argument("Missing OpenUSD schema resource directory.");
        }
        // Resolve resources from the installed R package, never the build prefix.
        // This is essential for R's staged installation and relocated libraries.
        pxr::PlugRegistry::GetInstance().RegisterPlugins(resource_path);
        if (!pxr::SdfLayer::FindOrOpen(std::string(resource_path) + "/usdGeom/resources/generatedSchema.usda")) {
            throw std::runtime_error("Cannot load OpenUSD schema resources.");
        }
    });
}

extern "C" const libopenusd_api_v1 *libopenusd_get_api_v1() { return &api; }
