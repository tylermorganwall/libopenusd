#include <Rcpp.h>
#include <libopenusd.h>
#include <cstring>
#include <memory>

// [[Rcpp::depends(libopenusd)]]
// [[Rcpp::export]]
bool registered_consumer_roundtrip(std::string filename) {
    const auto *api = libopenusd_get_api();
    if (std::strcmp(api->version(), "26.08") != 0) Rcpp::stop("Wrong OpenUSD version.");

    // Native clients own handles with provider deleters, not C++ delete.
    using Stage = std::unique_ptr<libopenusd_stage, void (*)(libopenusd_stage *)>;
    using Text = std::unique_ptr<char, void (*)(char *)>;
    Stage stage(api->stage_from_usda(
        "#usda 1.0\ndef Sphere \"NativeBall\" {\n double radius = 3\n}\n"), api->stage_release);
    if (!stage) Rcpp::stop(api->last_error());
    if (!api->stage_export(stage.get(), filename.c_str(), 0)) Rcpp::stop(api->last_error());
    Stage restored(api->stage_open(filename.c_str()), api->stage_release);
    if (!restored) Rcpp::stop(api->last_error());
    Text text(api->stage_to_usda(restored.get(), 1), api->free_string);
    if (!text || !std::strstr(text.get(), "radius = 3")) Rcpp::stop("Native roundtrip failed.");

    // C errors must not throw through the function table or touch the R API.
    if (api->stage_export(nullptr, filename.c_str(), 0) != 0) Rcpp::stop("Expected failure.");
    if (!std::strstr(api->last_error(), "Invalid USD stage")) Rcpp::stop("Missing diagnostic.");
    Stage recovered(api->stage_create(), api->stage_release);
    return recovered && std::strlen(api->last_error()) == 0;
}
