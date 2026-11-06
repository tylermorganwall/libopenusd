#define R_NO_REMAP
#include <Rinternals.h>
#include <R_ext/Rdynload.h>
#include <R_ext/Visibility.h>
#include "bridge.h"
extern "C" SEXP libopenusd_inspect_R(SEXP, SEXP, SEXP);
extern "C" SEXP libopenusd_write_stage_R(SEXP, SEXP);
#include <climits>
#include <cstring>

namespace {
const libopenusd_api_v1 *api() { return libopenusd_get_api_v1(); }
SEXP stage_tag() { return Rf_install("libopenusd_stage_v1"); }

const char *scalar_string(SEXP value) {
    if (TYPEOF(value) != STRSXP || XLENGTH(value) != 1 || STRING_ELT(value, 0) == NA_STRING) {
        Rf_error("Expected one non-missing character string.");
    }
    return Rf_translateCharUTF8(STRING_ELT(value, 0));
}

int scalar_flag(SEXP value) {
    if (TYPEOF(value) != LGLSXP || XLENGTH(value) != 1 || LOGICAL(value)[0] == NA_LOGICAL) {
        Rf_error("Expected TRUE or FALSE.");
    }
    return LOGICAL(value)[0];
}

libopenusd_stage *stage_handle(SEXP value) {
    if (TYPEOF(value) != EXTPTRSXP || R_ExternalPtrTag(value) != stage_tag() ||
        !R_ExternalPtrAddr(value)) {
        Rf_error("Invalid or closed USD stage. Reopen the file in this R session.");
    }
    return static_cast<libopenusd_stage *>(R_ExternalPtrAddr(value));
}

void finalize_stage(SEXP pointer) {
    api()->stage_release(static_cast<libopenusd_stage *>(R_ExternalPtrAddr(pointer)));
    R_ClearExternalPtr(pointer);
}

SEXP make_stage_pointer() {
    SEXP pointer = PROTECT(R_MakeExternalPtr(nullptr, stage_tag(), R_NilValue));
    R_RegisterCFinalizerEx(pointer, finalize_stage, TRUE);
    SEXP classes = PROTECT(Rf_mkString("usd_stage"));
    Rf_classgets(pointer, classes);
    UNPROTECT(2);
    return pointer;
}
} // namespace

extern "C" SEXP libopenusd_initialize_R(SEXP path) {
    if (!libopenusd_initialize(scalar_string(path))) Rf_error("%s", api()->last_error());
    return R_NilValue;
}

extern "C" SEXP libopenusd_version_R() { return Rf_mkString(api()->version()); }

extern "C" SEXP libopenusd_stage_create_R() {
    // Allocate the R owner first so R allocation errors cannot leak a stage.
    SEXP pointer = PROTECT(make_stage_pointer());
    auto *stage = api()->stage_create();
    if (!stage) Rf_error("%s", api()->last_error());
    R_SetExternalPtrAddr(pointer, stage);
    UNPROTECT(1);
    return pointer;
}

extern "C" SEXP libopenusd_stage_open_R(SEXP path) {
    const char *filename = scalar_string(path);
    SEXP pointer = PROTECT(make_stage_pointer());
    auto *stage = api()->stage_open(filename);
    if (!stage) Rf_error("%s", api()->last_error());
    R_SetExternalPtrAddr(pointer, stage);
    UNPROTECT(1);
    return pointer;
}

extern "C" SEXP libopenusd_stage_from_usda_R(SEXP text) {
    const char *source = scalar_string(text);
    SEXP pointer = PROTECT(make_stage_pointer());
    auto *stage = api()->stage_from_usda(source);
    if (!stage) Rf_error("%s", api()->last_error());
    R_SetExternalPtrAddr(pointer, stage);
    UNPROTECT(1);
    return pointer;
}

extern "C" SEXP libopenusd_stage_export_R(SEXP stage, SEXP path, SEXP flatten) {
    if (!api()->stage_export(stage_handle(stage), scalar_string(path), scalar_flag(flatten))) {
        Rf_error("%s", api()->last_error());
    }
    return R_NilValue;
}

extern "C" SEXP libopenusd_stage_to_usda_R(SEXP stage, SEXP flatten) {
    // A finalizable owner protects native text if an R string allocation fails.
    SEXP owner = PROTECT(R_MakeExternalPtr(nullptr, R_NilValue, R_NilValue));
    R_RegisterCFinalizerEx(owner, [](SEXP pointer) {
        libopenusd_get_api_v1()->free_string(static_cast<char *>(R_ExternalPtrAddr(pointer)));
        R_ClearExternalPtr(pointer);
    }, TRUE);
    char *text = api()->stage_to_usda(stage_handle(stage), scalar_flag(flatten));
    if (!text) Rf_error("%s", api()->last_error());
    R_SetExternalPtrAddr(owner, text);
    if (std::strlen(text) > INT_MAX) Rf_error("USDA text exceeds R's string size limit.");
    SEXP result = PROTECT(Rf_ScalarString(Rf_mkCharCE(text, CE_UTF8)));
    api()->free_string(text);
    R_ClearExternalPtr(owner);
    UNPROTECT(2);
    return result;
}

extern "C" SEXP libopenusd_stage_close_R(SEXP stage) {
    stage_handle(stage);
    finalize_stage(stage);
    return R_NilValue;
}

static const R_CallMethodDef call_methods[] = {
    {"libopenusd_write_stage_R", (DL_FUNC) &libopenusd_write_stage_R, 2},
    {"libopenusd_inspect_R", (DL_FUNC) &libopenusd_inspect_R, 3},
    {"libopenusd_initialize_R", (DL_FUNC) &libopenusd_initialize_R, 1},
    {"libopenusd_version_R", (DL_FUNC) &libopenusd_version_R, 0},
    {"libopenusd_stage_create_R", (DL_FUNC) &libopenusd_stage_create_R, 0},
    {"libopenusd_stage_open_R", (DL_FUNC) &libopenusd_stage_open_R, 1},
    {"libopenusd_stage_from_usda_R", (DL_FUNC) &libopenusd_stage_from_usda_R, 1},
    {"libopenusd_stage_export_R", (DL_FUNC) &libopenusd_stage_export_R, 3},
    {"libopenusd_stage_to_usda_R", (DL_FUNC) &libopenusd_stage_to_usda_R, 2},
    {"libopenusd_stage_close_R", (DL_FUNC) &libopenusd_stage_close_R, 1},
    {nullptr, nullptr, 0}
};

extern "C" void attribute_visible R_init_libopenusd(DllInfo *dll) {
    R_registerRoutines(dll, nullptr, call_methods, nullptr, nullptr);
    R_useDynamicSymbols(dll, FALSE);
    R_forceSymbols(dll, TRUE);
    R_RegisterCCallable("libopenusd", "libopenusd_write_stage_v1",
        (DL_FUNC) &libopenusd_write_stage_R);
    R_RegisterCCallable("libopenusd", "libopenusd_inspect_v1",
        (DL_FUNC) &libopenusd_inspect_R);
    R_RegisterCCallable("libopenusd", "libopenusd_get_api_v1",
        (DL_FUNC) &libopenusd_get_api_v1);
}
