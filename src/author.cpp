#include <Rcpp.h>
#include "operation.h"
#include <pxr/base/arch/fileSystem.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/quatf.h>
#include <pxr/base/gf/quath.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/listOp.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/stage.h>
#include <cmath>
#include <climits>
#include <cerrno>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace {
using namespace pxr;

std::string string_value(SEXP value) {
    if (TYPEOF(value) != STRSXP || XLENGTH(value) != 1 || STRING_ELT(value, 0) == NA_STRING) {
        Rcpp::stop("Expected one non-missing USD string.");
    }
    return Rf_translateCharUTF8(STRING_ELT(value, 0));
}

void require_success(bool success, const std::string &operation) {
    if (!success) Rcpp::stop("OpenUSD failed to %s.", operation.c_str());
}

// Convert directly from column-major R storage. A tuple matrix has one tuple
// per row; a scalar matrix4d uses its actual row/column layout, not tuple layout.
double number_at(SEXP value, R_xlen_t index) {
    double result;
    if (TYPEOF(value) == REALSXP) result = REAL(value)[index];
    else if (TYPEOF(value) == INTSXP || TYPEOF(value) == LGLSXP) {
        int integer = TYPEOF(value) == INTSXP ? INTEGER(value)[index] : LOGICAL(value)[index];
        if (integer == NA_INTEGER) Rcpp::stop("Missing value in USD numeric property.");
        result = integer;
    } else Rcpp::stop("USD numeric properties require numeric data.");
    if (!std::isfinite(result)) Rcpp::stop("Nonfinite value in USD numeric property.");
    return result;
}

template <typename T, typename Convert>
VtValue typed_array(SEXP value, int width, bool array, Convert convert) {
    const R_xlen_t length = XLENGTH(value);
    if (length % width || (!array && length != width)) {
        Rcpp::stop("Invalid USD property value length.");
    }
    const R_xlen_t count = length / width;
    const bool matrix = Rf_isMatrix(value);
    if (matrix && Rf_ncols(value) != width) Rcpp::stop("Invalid USD tuple matrix width.");
    auto element = [&](R_xlen_t row) {
        return convert([&](int column) {
            return number_at(value, matrix ? row + count * column : row * width + column);
        }, row);
    };
    if (!array) return VtValue(element(0));
    VtArray<T> result(static_cast<size_t>(count));
    for (R_xlen_t row = 0; row < count; ++row) {
        if (row % 65536 == 0) Rcpp::checkUserInterrupt();
        result[row] = element(row);
    }
    return VtValue(std::move(result));
}

VtValue property_value(SEXP value, std::string type) {
    const bool array = type.size() >= 2 && type.substr(type.size() - 2) == "[]";
    if (array) type.resize(type.size() - 2);
    if (type == "matrix4d") {
        if (array || !Rf_isMatrix(value) || Rf_nrows(value) != 4 || Rf_ncols(value) != 4) {
            Rcpp::stop("USD matrix4d requires a 4 by 4 matrix.");
        }
        GfMatrix4d matrix;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) matrix[row][col] = number_at(value, row + 4 * col);
        }
        return VtValue(matrix);
    }
    if (type == "string" || type == "token" || type == "asset") {
        if (TYPEOF(value) != STRSXP) Rcpp::stop("USD string properties require character data.");
        auto text = [&](auto, R_xlen_t row) {
            if (STRING_ELT(value, row) == NA_STRING) Rcpp::stop("Missing USD string value.");
            return std::string(Rf_translateCharUTF8(STRING_ELT(value, row)));
        };
        if (type == "string") return typed_array<std::string>(value, 1, array, text);
        if (type == "token") return typed_array<TfToken>(value, 1, array,
            [&](auto at, R_xlen_t row) { return TfToken(text(at, row)); });
        return typed_array<SdfAssetPath>(value, 1, array,
            [&](auto at, R_xlen_t row) { return SdfAssetPath(text(at, row)); });
    }
    auto integer = [](double value) {
        if (value < INT_MIN || value > INT_MAX || value != std::trunc(value)) {
            Rcpp::stop("USD integer property is outside the integer range or is fractional.");
        }
        return static_cast<int>(value);
    };
    if (type == "bool") return typed_array<bool>(value, 1, array, [](auto at, auto) { return at(0) != 0; });
    if (type == "int") return typed_array<int>(value, 1, array, [&](auto at, auto) { return integer(at(0)); });
    if (type == "float") return typed_array<float>(value, 1, array, [](auto at, auto) { return float(at(0)); });
    if (type == "double") return typed_array<double>(value, 1, array, [](auto at, auto) { return at(0); });
    if (type == "int2") return typed_array<GfVec2i>(value, 2, array,
        [&](auto at, auto) { return GfVec2i(integer(at(0)), integer(at(1))); });
    if (type == "float2" || type == "texCoord2f") return typed_array<GfVec2f>(value, 2, array,
        [](auto at, auto) { return GfVec2f(at(0), at(1)); });
    if (type == "float3" || type == "point3f" || type == "normal3f" || type == "color3f" || type == "vector3f") {
        return typed_array<GfVec3f>(value, 3, array, [](auto at, auto) { return GfVec3f(at(0), at(1), at(2)); });
    }
    if (type == "double3") return typed_array<GfVec3d>(value, 3, array,
        [](auto at, auto) { return GfVec3d(at(0), at(1), at(2)); });
    if (type == "quatf") return typed_array<GfQuatf>(value, 4, array,
        [](auto at, auto) { return GfQuatf(at(0), at(1), at(2), at(3)); });
    if (type == "quatd") return typed_array<GfQuatd>(value, 4, array,
        [](auto at, auto) { return GfQuatd(at(0), at(1), at(2), at(3)); });
    if (type == "quath") return typed_array<GfQuath>(value, 4, array,
        [](auto at, auto) { return GfQuath(GfHalf(at(0)), GfHalf(at(1)), GfHalf(at(2)), GfHalf(at(3))); });
    Rcpp::stop("Unsupported USD authoring type: %s.", type.c_str());
    return VtValue();
}

VtDictionary encode_r_object(SEXP value, unsigned depth, bool template_only = false);

VtDictionary encode_r_fields(SEXP value, unsigned depth, bool templates) {
    Rcpp::List fields(value);
    Rcpp::CharacterVector names = fields.hasAttribute("names") ? Rcpp::CharacterVector(fields.attr("names")) : Rcpp::CharacterVector(0);
    if (names.size() != fields.size()) Rcpp::stop("USD dictionary fields must be named.");
    VtDictionary result;
    for (R_xlen_t i = 0; i < fields.size(); ++i) {
        result[string_value(Rcpp::CharacterVector::create(names[i]))] =
            VtValue(encode_r_object(fields[i], depth + 1, templates));
    }
    return result;
}

// Preserve R attributes and missing-value distinctions as typed dictionaries.
// Numeric payloads become native arrays in a single pass, never strings or an
// R list per element. Column templates retain attributes but omit column data.
VtDictionary encode_r_object(SEXP value, unsigned depth, bool template_only) {
    if (depth > 512) Rcpp::stop("USD metadata nesting exceeds 512 levels.");
    VtDictionary result;
    const char *kind = nullptr;
    switch (TYPEOF(value)) {
        case NILSXP: kind = "NULL"; break;
        case VECSXP: kind = "list"; break;
        case LGLSXP: kind = "logical"; break;
        case INTSXP: kind = "integer"; break;
        case REALSXP: kind = "double"; break;
        case STRSXP: kind = "character"; break;
        case RAWSXP: kind = "raw"; break;
        default: Rcpp::stop("USD export cannot preserve an R %s; use a file-backed or numeric descriptor.", Rf_type2char(TYPEOF(value)));
    }
    result["type"] = std::string(kind);
    const R_xlen_t size = template_only || value == R_NilValue ? 0 : XLENGTH(value);
    if (TYPEOF(value) == VECSXP) {
        VtDictionary items;
        for (R_xlen_t i = 0; i < size; ++i) {
            if (i % 65536 == 0) Rcpp::checkUserInterrupt();
            std::ostringstream key;
            key << 'n' << std::setfill('0') << std::setw(8) << i;
            items[key.str()] = VtValue(encode_r_object(VECTOR_ELT(value, i), depth + 1));
        }
        result["items"] = std::move(items);
    } else if (value != R_NilValue) {
        VtIntArray missing;
        VtStringArray codes;
        auto record_missing = [&](R_xlen_t i, const char *code) {
            if (i >= INT_MAX) Rcpp::stop("USD missing-value index exceeds schema version 1 limits.");
            missing.push_back(static_cast<int>(i + 1));
            codes.push_back(code);
        };
        auto encode = [&](auto initial, auto get) {
            using T = decltype(initial);
            VtArray<T> data(static_cast<size_t>(size));
            for (R_xlen_t i = 0; i < size; ++i) {
                if (i % 65536 == 0) Rcpp::checkUserInterrupt();
                data[i] = get(i);
            }
            result["data"] = VtValue(std::move(data));
        };
        switch (TYPEOF(value)) {
            case LGLSXP:
                encode(false, [&](R_xlen_t i) {
                    if (LOGICAL(value)[i] == NA_LOGICAL) record_missing(i, "NA");
                    return LOGICAL(value)[i] == TRUE;
                }); break;
            case INTSXP:
                encode(0, [&](R_xlen_t i) {
                    if (INTEGER(value)[i] != NA_INTEGER) return INTEGER(value)[i];
                    record_missing(i, "NA"); return 0;
                }); break;
            case REALSXP:
                encode(0.0, [&](R_xlen_t i) {
                    const double number = REAL(value)[i];
                    if (std::isfinite(number)) return number;
                    record_missing(i, R_IsNA(number) ? "NA" : std::isnan(number) ? "NaN" : number > 0 ? "Inf" : "-Inf");
                    return 0.0;
                }); break;
            case STRSXP:
                encode(std::string(), [&](R_xlen_t i) {
                    if (STRING_ELT(value, i) != NA_STRING) return std::string(Rf_translateCharUTF8(STRING_ELT(value, i)));
                    record_missing(i, "NA"); return std::string();
                }); break;
            case RAWSXP:
                encode(static_cast<unsigned char>(0), [&](R_xlen_t i) { return RAW(value)[i]; }); break;
        }
        if (!missing.empty()) {
            result["missingIndices"] = std::move(missing);
            result["missingCodes"] = std::move(codes);
        }
    }
    VtDictionary attributes;
    Rcpp::RObject object(value);
    for (const auto &name : object.attributeNames()) {
        Rcpp::RObject attribute = object.attr(name);
        attributes[name] = VtValue(encode_r_object(attribute, depth + 1));
    }
    if (!attributes.empty()) result["attributes"] = std::move(attributes);
    return result;
}

// Plain named lists author dictionaries. An explicit usd_r_object node asks for
// exact R-value encoding rather than inferring scalar versus array semantics.
VtValue metadata_value(SEXP value, unsigned depth = 0) {
    if (depth > 512) Rcpp::stop("USD metadata nesting exceeds 512 levels.");
    if (Rf_inherits(value, "usd_r_object")) {
        Rcpp::List node(value);
        if (Rcpp::as<bool>(node["fields"])) {
            return VtValue(encode_r_fields(node["value"], depth, Rcpp::as<bool>(node["templates"])));
        }
        return VtValue(encode_r_object(node["value"], depth));
    }
    if (TYPEOF(value) == VECSXP) {
        Rcpp::List fields(value);
        Rcpp::CharacterVector names = fields.hasAttribute("names") ? Rcpp::CharacterVector(fields.attr("names")) : Rcpp::CharacterVector(0);
        if (fields.size() && names.size() != fields.size()) Rcpp::stop("USD metadata dictionaries must be named.");
        VtDictionary dictionary;
        for (R_xlen_t i = 0; i < fields.size(); ++i) {
            if (names[i] == NA_STRING) Rcpp::stop("Missing USD dictionary key.");
            dictionary[Rf_translateCharUTF8(STRING_ELT(names, i))] = metadata_value(fields[i], depth + 1);
        }
        return VtValue(std::move(dictionary));
    }
    const std::string suffix = XLENGTH(value) == 1 ? "" : "[]";
    if (TYPEOF(value) == STRSXP) return property_value(value, "string" + suffix);
    if (TYPEOF(value) == INTSXP) return property_value(value, "int" + suffix);
    if (TYPEOF(value) == LGLSXP) return property_value(value, "bool" + suffix);
    if (TYPEOF(value) == REALSXP) return property_value(value, "double" + suffix);
    Rcpp::stop("Unsupported USD dictionary value.");
    return VtValue();
}

SdfPathVector paths(SEXP value) {
    if (TYPEOF(value) != STRSXP) Rcpp::stop("USD targets must be character paths.");
    SdfPathVector result;
    result.reserve(XLENGTH(value));
    for (R_xlen_t i = 0; i < XLENGTH(value); ++i) {
        if (STRING_ELT(value, i) == NA_STRING) Rcpp::stop("Missing USD target path.");
        SdfPath path(Rf_translateCharUTF8(STRING_ELT(value, i)));
        if (!path.IsAbsolutePath()) Rcpp::stop("USD targets must be absolute paths.");
        result.push_back(path);
    }
    return result;
}

void author_nodes(const UsdStageRefPtr &stage, const UsdPrim &parent, SEXP nodes, unsigned depth = 0) {
    if (depth > 512) Rcpp::stop("USD prim nesting exceeds 512 levels.");
    if (Rf_isNull(nodes) || XLENGTH(nodes) == 0) return;
    if (TYPEOF(nodes) != VECSXP) Rcpp::stop("USD authoring nodes must be lists.");
    Rcpp::List node(nodes);
    if (!node.containsElementNamed("kind")) {
        for (SEXP child : node) {
            Rcpp::checkUserInterrupt();
            author_nodes(stage, parent, child, depth + 1);
        }
        return;
    }
    const std::string kind = string_value(node["kind"]);
    const std::string name = string_value(node["name"]);
    if (kind == "prim") {
        if (!SdfPath::IsValidIdentifier(name)) Rcpp::stop("Invalid USD prim name: %s.", name.c_str());
        const SdfPath path = parent.GetPath().AppendChild(TfToken(name));
        auto prim = stage->DefinePrim(path, TfToken(string_value(node["type"])));
        require_success(bool(prim), "define prim " + path.GetString());
        Rcpp::List metadata(node["metadata"]);
        Rcpp::CharacterVector names = metadata.hasAttribute("names") ? Rcpp::CharacterVector(metadata.attr("names")) : Rcpp::CharacterVector(0);
        if (names.size() != metadata.size()) Rcpp::stop("USD prim metadata must be named.");
        for (R_xlen_t i = 0; i < metadata.size(); ++i) {
            const std::string key = Rcpp::as<std::string>(names[i]);
            VtValue value;
            if (key == "apiSchemas") {
                Rcpp::CharacterVector schemas(metadata[i]);
                TfTokenVector tokens;
                for (auto schema : schemas) tokens.emplace_back(Rcpp::as<std::string>(schema));
                SdfTokenListOp operation;
                operation.SetPrependedItems(tokens);
                value = VtValue(operation);
            } else value = metadata_value(metadata[i]);
            require_success(prim.SetMetadata(TfToken(key), value), "set metadata " + key);
        }
        author_nodes(stage, prim, node["body"], depth + 1);
        return;
    }
    if (kind == "relationship") {
        auto relationship = parent.CreateRelationship(TfToken(name), false);
        require_success(relationship.SetTargets(paths(node["targets"])), "set relationship " + name);
        return;
    }
    if (kind != "attribute") Rcpp::stop("Unknown USD authoring node: %s.", kind.c_str());
    const std::string type = string_value(node["type"]);
    const auto value_type = SdfSchema::GetInstance().FindType(TfToken(type));
    if (!value_type) Rcpp::stop("Unknown USD attribute type: %s.", type.c_str());
    const bool uniform = Rcpp::as<bool>(node["uniform"]);
    auto attribute = parent.CreateAttribute(TfToken(name), value_type, Rcpp::as<bool>(node["custom"]),
        uniform ? SdfVariabilityUniform : SdfVariabilityVarying);
    if (!Rf_isNull(node["value"])) {
        require_success(attribute.Set(property_value(node["value"], type)), "set attribute " + name);
    }
    if (node.containsElementNamed("times") && !Rf_isNull(node["times"])) {
        Rcpp::NumericVector times(node["times"]);
        Rcpp::List samples(node["samples"]);
        if (times.size() != samples.size()) Rcpp::stop("USD sample times and values must have equal lengths.");
        for (R_xlen_t i = 0; i < times.size(); ++i) {
            if (!std::isfinite(times[i])) Rcpp::stop("Nonfinite USD sample time.");
            require_success(attribute.Set(property_value(samples[i], type), UsdTimeCode(times[i])), "set sample " + name);
        }
    }
    if (node.containsElementNamed("connections") && !Rf_isNull(node["connections"])) {
        require_success(attribute.SetConnections(paths(node["connections"])), "connect attribute " + name);
    }
    if (node.containsElementNamed("interpolation") && !Rf_isNull(node["interpolation"])) {
        require_success(attribute.SetMetadata(TfToken("interpolation"), TfToken(string_value(node["interpolation"]))),
            "set interpolation " + name);
    }
}
} // namespace

extern "C" SEXP libopenusd_write_stage_R(SEXP document, SEXP filename) {
    BEGIN_RCPP
    LibOpenUsdOperation scope;
    const std::string path = string_value(filename);
    pxr::TfErrorMark errors;
    try {
        Rcpp::List description(document);
        auto stage = pxr::UsdStage::CreateInMemory();
        if (!stage) Rcpp::stop("Cannot create USD stage.");
        author_nodes(stage, stage->GetPseudoRoot(), description["prims"]);
        const auto root = stage->GetPrimAtPath(pxr::SdfPath(string_value(description["default_prim"])));
        if (!root) Rcpp::stop("USD default prim does not exist.");
        stage->SetDefaultPrim(root);
        require_success(stage->SetMetadata(pxr::TfToken("upAxis"), pxr::TfToken("Y")), "set up axis");
        require_success(stage->SetMetadata(pxr::TfToken("metersPerUnit"), 1.0), "set scene units");
        const double fps = Rcpp::as<double>(description["fps"]);
        const double end = Rcpp::as<double>(description["end_time"]);
        if (!std::isfinite(fps) || fps <= 0 || !std::isfinite(end)) Rcpp::stop("Invalid USD stage timing.");
        stage->SetStartTimeCode(0);
        stage->SetEndTimeCode(end);
        stage->SetFramesPerSecond(fps);
        stage->SetTimeCodesPerSecond(fps);
        if (!errors.IsClean()) Rcpp::stop("USD stage construction failed.");

        // Export the typed in-memory layer directly. OpenUSD chooses the native
        // crate writer for .usdc and its text writer only for requested .usda.
        errno = 0;
        if (!stage->GetRootLayer()->Export(path)) {
            const int code = errno;
            std::ostringstream message;
            message << "Cannot write USD stage \"" << path << "\"";
            if (code) message << ": " << std::error_code(code, std::generic_category()).message();
            std::error_code query_error;
#ifdef _WIN32
            const std::filesystem::path output_path(ArchWindowsUtf8ToUtf16(path));
#else
            const std::filesystem::path output_path(path);
#endif
            const auto destination = output_path.parent_path();
            const auto space = std::filesystem::space(destination.empty() ? "." : destination, query_error);
            if (!query_error) message << ". Available destination space: " << space.available << " bytes";
            throw std::runtime_error(message.str());
        }
        if (!errors.IsClean()) Rcpp::stop("USD stage export failed.");
        LibOpenUsdCheckFailure();
        return R_NilValue;
    } catch (const std::exception &error) {
        std::string message = error.what();
        for (const auto &diagnostic : errors) message += "\n" + diagnostic.GetCommentary();
        errors.Clear();
        throw std::runtime_error(message);
    } catch (...) {
        errors.Clear();
        throw;
    }
    END_RCPP
}
