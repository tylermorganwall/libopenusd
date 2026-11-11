#include <Rcpp.h>
#include "operation.h"
#include <pxr/base/tf/errorMark.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/primRange.h>

namespace {
// This interface deliberately uses R objects and runs only on R's main thread.
// The existing opaque-handle C API remains independent of R and worker-safe.
Rcpp::RObject usd_value(const pxr::VtValue &value) {
    if (value.IsEmpty()) return R_NilValue;
    if (value.IsHolding<pxr::VtDictionary>()) {
        const auto &dictionary = value.UncheckedGet<pxr::VtDictionary>();
        Rcpp::List result(dictionary.size());
        Rcpp::CharacterVector names(dictionary.size());
        size_t i = 0;
        for (const auto &entry : dictionary) {
            names[i] = entry.first;
            result[i++] = usd_value(entry.second);
        }
        result.attr("names") = names;
        return result;
    }
    if (value.IsHolding<bool>()) return Rcpp::wrap(value.UncheckedGet<bool>());
    if (value.IsHolding<int>()) return Rcpp::wrap(value.UncheckedGet<int>());
    if (value.IsHolding<double>()) return Rcpp::wrap(value.UncheckedGet<double>());
    if (value.IsHolding<float>()) return Rcpp::wrap(value.UncheckedGet<float>());
    if (value.IsHolding<std::string>()) return Rcpp::wrap(value.UncheckedGet<std::string>());
    if (value.IsHolding<pxr::TfToken>()) return Rcpp::wrap(value.UncheckedGet<pxr::TfToken>().GetString());
    if (value.IsHolding<pxr::SdfAssetPath>()) {
        const auto &asset = value.UncheckedGet<pxr::SdfAssetPath>();
        Rcpp::List result = Rcpp::List::create(
            Rcpp::Named("path") = asset.GetAssetPath(),
            Rcpp::Named("resolved_path") = asset.GetResolvedPath());
        result.attr("class") = "usd_asset";
        return result;
    }
    // Keep empty and singleton arrays as arrays, never auto-unbox them.
    if (value.IsHolding<pxr::VtBoolArray>()) {
        const auto &data = value.UncheckedGet<pxr::VtBoolArray>();
        Rcpp::LogicalVector result(data.size());
        for (size_t i = 0; i < data.size(); ++i) result[i] = data[i];
        return result;
    }
    if (value.IsHolding<pxr::VtIntArray>()) {
        const auto &data = value.UncheckedGet<pxr::VtIntArray>();
        return Rcpp::IntegerVector(data.begin(), data.end());
    }
    if (value.IsHolding<pxr::VtDoubleArray>()) {
        const auto &data = value.UncheckedGet<pxr::VtDoubleArray>();
        return Rcpp::NumericVector(data.begin(), data.end());
    }
    if (value.IsHolding<pxr::VtFloatArray>()) {
        const auto &data = value.UncheckedGet<pxr::VtFloatArray>();
        return Rcpp::NumericVector(data.begin(), data.end());
    }
    if (value.IsHolding<pxr::VtUCharArray>()) {
        const auto &data = value.UncheckedGet<pxr::VtUCharArray>();
        return Rcpp::RawVector(data.begin(), data.end());
    }
    if (value.IsHolding<pxr::VtStringArray>()) {
        const auto &data = value.UncheckedGet<pxr::VtStringArray>();
        return Rcpp::CharacterVector(data.begin(), data.end());
    }
    if (value.IsHolding<pxr::VtTokenArray>()) {
        const auto &data = value.UncheckedGet<pxr::VtTokenArray>();
        Rcpp::CharacterVector result(data.size());
        for (size_t i = 0; i < data.size(); ++i) result[i] = data[i].GetString();
        return result;
    }
    if (value.IsHolding<pxr::VtArray<pxr::SdfAssetPath>>()) {
        const auto &data = value.UncheckedGet<pxr::VtArray<pxr::SdfAssetPath>>();
        Rcpp::List result(data.size());
        for (size_t i = 0; i < data.size(); ++i) result[i] = usd_value(pxr::VtValue(data[i]));
        return result;
    }
    throw std::runtime_error("Unsupported USD metadata value type: " + value.GetTypeName());
}

Rcpp::List inspect_prim(const pxr::UsdPrim &prim, Rcpp::CharacterVector properties) {
    Rcpp::List values(properties.size());
    for (R_xlen_t i = 0; i < properties.size(); ++i) {
        auto attribute = prim.GetAttribute(pxr::TfToken(Rcpp::as<std::string>(properties[i])));
        pxr::VtValue value;
        if (attribute) attribute.Get(&value);
        values[i] = usd_value(value);
    }
    values.attr("names") = properties;
    std::vector<std::string> children;
    for (const auto &child : prim.GetChildren()) children.push_back(child.GetPath().GetString());
    return Rcpp::List::create(
        Rcpp::Named("path") = prim.GetPath().GetString(),
        Rcpp::Named("type") = prim.GetTypeName().GetString(),
        Rcpp::Named("custom_data") = usd_value(pxr::VtValue(prim.GetCustomData())),
        Rcpp::Named("properties") = values,
        Rcpp::Named("children") = children);
}
} // namespace

extern "C" SEXP libopenusd_inspect_R(SEXP filename, SEXP selectors, SEXP properties) {
    BEGIN_RCPP
    LibOpenUsdOperation scope;
    if (TYPEOF(filename) != STRSXP || Rf_xlength(filename) != 1 || STRING_ELT(filename, 0) == NA_STRING ||
        TYPEOF(selectors) != STRSXP || TYPEOF(properties) != STRSXP) {
        Rcpp::stop("Expected a filename, character selectors, and character property names.");
    }
    Rcpp::CharacterVector paths(selectors), property_names(properties);
    for (auto path : paths) if (path == NA_STRING) Rcpp::stop("Selectors cannot contain NA.");
    for (auto property : property_names) if (property == NA_STRING) Rcpp::stop("Properties cannot contain NA.");
    pxr::TfErrorMark errors;
    try {
        auto stage = pxr::UsdStage::Open(Rcpp::as<std::string>(filename));
        if (!stage) throw std::runtime_error("Cannot open USD stage.");
        // Collect matching prim handles first, then allocate the R result once.
        // Repeated List::push_back would copy the growing scene for every object.
        std::vector<pxr::UsdPrim> selected;
        for (auto path : paths) {
            std::string selector = Rcpp::as<std::string>(path);
            bool children = selector.size() >= 2 && selector.substr(selector.size() - 2) == "/*";
            if (children) selector.resize(selector.size() - 2);
            auto prim = selector.empty() ? stage->GetDefaultPrim() : stage->GetPrimAtPath(pxr::SdfPath(selector));
            if (!prim) continue;
            if (children) {
                for (const auto &child : prim.GetChildren()) selected.push_back(child);
            } else {
                selected.push_back(prim);
            }
        }
        Rcpp::List result(selected.size());
        for (size_t i = 0; i < selected.size(); ++i) {
            result[i] = inspect_prim(selected[i], property_names);
        }
        if (!errors.IsClean()) throw std::runtime_error("USD metadata query failed.");
        LibOpenUsdCheckFailure();
        return result;
    } catch (const std::exception &error) {
        std::string message = error.what();
        for (const auto &diagnostic : errors) message += "\n" + diagnostic.GetCommentary();
        errors.Clear();
        throw std::runtime_error(message);
    }
    END_RCPP
}
