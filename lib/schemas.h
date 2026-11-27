/* -*- C++ -*-
 * Cppcheck - A tool for static C/C++ code analysis
 * Copyright (C) 2007-2026 Cppcheck team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef schemasH
#define schemasH

#include <pugixml.hpp>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <rfl/xml.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace cppcheck::schemas {

struct EmptyElement {};

// ---------------------------------------------------------------------------
// Domain 1: Platform XML / JSON schema
// ---------------------------------------------------------------------------

struct PlatformSizeofSchema {
    rfl::Rename<"bool", std::optional<std::size_t>> bool_size;
    rfl::Rename<"short", std::optional<std::size_t>> short_size;
    rfl::Rename<"int", std::optional<std::size_t>> int_size;
    rfl::Rename<"long", std::optional<std::size_t>> long_size;
    rfl::Rename<"long-long", std::optional<std::size_t>> long_long_size;
    rfl::Rename<"float", std::optional<std::size_t>> float_size;
    rfl::Rename<"double", std::optional<std::size_t>> double_size;
    rfl::Rename<"long-double", std::optional<std::size_t>> long_double_size;
    rfl::Rename<"pointer", std::optional<std::size_t>> pointer_size;
    rfl::Rename<"size_t", std::optional<std::size_t>> size_t_size;
    rfl::Rename<"wchar_t", std::optional<std::size_t>> wchar_t_size;
};

struct PlatformSchema {
    std::optional<unsigned int> char_bit;
    rfl::Rename<"default-sign", std::optional<std::string>> default_sign;
    rfl::Rename<"sizeof", std::optional<PlatformSizeofSchema>> sizeof_types;
    std::optional<bool> windows;
};

template <class Schema = PlatformSchema>
inline rfl::Result<Schema> parsePlatformXmlNode(const pugi::xml_node& root) {
    if (!root || std::string_view(root.name()) != "platform")
        return rfl::error("Expected <platform> root element.");
    return rfl::xml::read<Schema>(root);
}

template <class Schema = PlatformSchema>
inline rfl::Result<Schema> parsePlatformXml(std::string_view xml) {
    pugi::xml_document doc;
    const auto res = doc.load_buffer(xml.data(), xml.size());
    if (!res)
        return rfl::error(std::string("XML parse error: ") + res.description());
    return parsePlatformXmlNode<Schema>(doc.document_element());
}

template <class Schema = PlatformSchema>
inline std::string writePlatformXml(const Schema& s) {
    return rfl::xml::write<"platform">(s, "  ");
}

template <class Schema = PlatformSchema>
inline rfl::Result<Schema> parsePlatformJson(std::string_view json) {
    return rfl::json::read<Schema>(json);
}

template <class Schema = PlatformSchema>
inline std::string writePlatformJson(const Schema& s) {
    return rfl::json::write(s);
}

template <class Schema = PlatformSchema>
inline std::string platformJsonSchema() {
    return rfl::json::to_schema<Schema>();
}

// ---------------------------------------------------------------------------
// Domain 2: Suppressions XML / JSON / Dump schema
// ---------------------------------------------------------------------------

struct SuppressEntrySchema {
    std::optional<std::string> id;
    std::optional<std::string> fileName;
    std::optional<int> lineNumber;
    std::optional<std::string> symbolName;
    std::optional<std::string> macroName;
    std::optional<std::size_t> hash;
};

struct SuppressionsFileSchema {
    std::vector<SuppressEntrySchema> suppress;
};

struct DumpSuppressionSchema {
    rfl::Attribute<std::string> errorId;
    rfl::Attribute<std::optional<std::string>> fileName;
    rfl::Attribute<std::optional<int>> lineNumber;
    rfl::Attribute<std::optional<bool>> thisAndNextLine;
    rfl::Attribute<std::optional<std::string>> symbolName;
    rfl::Attribute<std::optional<std::size_t>> hash;
    rfl::Attribute<std::optional<int>> lineBegin;
    rfl::Attribute<std::optional<int>> lineEnd;
    rfl::Attribute<std::optional<std::string>> type;
    rfl::Rename<"inline", rfl::Attribute<bool>> is_inline = false;
    rfl::Attribute<std::optional<std::string>> comment;
};

struct DumpSuppressionsSchema {
    std::vector<DumpSuppressionSchema> suppression;
};

template <class Schema = SuppressionsFileSchema>
inline std::string writeSuppressionsXml(const Schema& s) {
    return rfl::xml::write<"suppressions">(s, "  ");
}

template <class Schema = DumpSuppressionsSchema>
inline std::string writeDumpSuppressionsXml(const Schema& s) {
    pugi::xml_document doc;
    rfl::xml::write<"suppressions">(s, doc);
    std::ostringstream oss;
    doc.save(oss, "  ", pugi::format_default | pugi::format_no_declaration);
    const std::string raw = oss.str();
    std::string prefixed;
    prefixed.reserve(raw.size() + 32);
    std::size_t pos = 0;
    while (pos < raw.size()) {
        const std::size_t nl = raw.find('\n', pos);
        const std::size_t end = (nl == std::string::npos) ? raw.size() : nl + 1;
        prefixed += "  ";
        prefixed.append(raw, pos, end - pos);
        pos = end;
    }
    return prefixed;
}

template <class Schema = SuppressionsFileSchema>
inline rfl::Result<Schema> parseSuppressionsJson(std::string_view json) {
    return rfl::json::read<Schema>(json);
}

template <class Schema = SuppressionsFileSchema>
inline std::string writeSuppressionsJson(const Schema& s) {
    return rfl::json::write(s);
}

template <class Schema = SuppressionsFileSchema>
inline std::string suppressionsJsonSchema() {
    return rfl::json::to_schema<Schema>();
}

template <class Schema = DumpSuppressionsSchema>
inline std::string dumpSuppressionsJsonSchema() {
    return rfl::json::to_schema<Schema>();
}

// ---------------------------------------------------------------------------
// Domain 3: .cppcheck Project File XML / JSON schema (shared by CLI & GUI)
// ---------------------------------------------------------------------------

struct NamedAttrSchema {
    rfl::Attribute<std::optional<std::string>> name;
};

struct IncludeDirListSchema {
    std::vector<NamedAttrSchema> dir;
};

struct DefinesListSchema {
    std::vector<NamedAttrSchema> define;
};

struct UndefinesListSchema {
    std::vector<std::string> undefine;
};

struct PathsListSchema {
    std::vector<NamedAttrSchema> dir;
};

struct ExcludeListSchema {
    std::vector<NamedAttrSchema> path;
};

struct LibrariesListSchema {
    std::vector<std::string> library;
};

struct VsConfigsSchema {
    std::vector<std::string> config;
};

struct AddonsListSchema {
    std::vector<std::string> addon;
};

struct ToolsListSchema {
    std::vector<std::string> tool;
};

struct TagsListSchema {
    std::vector<std::string> tag;
};

struct CheckUnknownFunctionReturnSchema {
    std::vector<std::string> name;
};

struct CodingStandardsListSchema {
    rfl::Rename<"coding-standard", std::vector<std::string>> coding_standard;
};

struct ProjectSuppressionSchema {
    rfl::Attribute<std::optional<std::string>> fileName;
    rfl::Attribute<std::optional<int>> lineNumber;
    rfl::Attribute<std::optional<std::string>> symbolName;
    rfl::Attribute<std::optional<std::size_t>> hash;
    rfl::Rename<"xml_content", std::optional<std::string>> errorId;
};

struct ProjectSuppressionsSchema {
    std::vector<ProjectSuppressionSchema> suppression;
};

struct SafeChecksSchema {
    rfl::Rename<"class-public", std::optional<EmptyElement>> class_public;
    rfl::Rename<"external-functions", std::optional<EmptyElement>> external_functions;
    rfl::Rename<"internal-functions", std::optional<EmptyElement>> internal_functions;
    rfl::Rename<"external-variables", std::optional<EmptyElement>> external_variables;
};

struct TagWarningEntrySchema {
    rfl::Attribute<std::optional<std::size_t>> hash;
};

struct TagWarningsSchema {
    rfl::Attribute<std::optional<std::string>> tag;
    std::vector<TagWarningEntrySchema> warning;
};

struct ProjectFileSchema {
    rfl::Attribute<std::optional<std::string>> version;
    std::optional<NamedAttrSchema> root;
    std::optional<std::string> builddir;
    std::optional<std::string> platform;
    std::optional<std::string> importproject;
    rfl::Rename<"analyze-all-vs-configs", std::optional<bool>> analyze_all_vs_configs;
    std::optional<std::string> parser;
    rfl::Rename<"check-headers", std::optional<bool>> check_headers;
    rfl::Rename<"check-unused-templates", std::optional<bool>> check_unused_templates;
    rfl::Rename<"inline-suppression", std::optional<bool>> inline_suppression;
    rfl::Rename<"max-ctu-depth", std::optional<int>> max_ctu_depth;
    rfl::Rename<"max-template-recursion", std::optional<int>> max_template_recursion;
    std::optional<IncludeDirListSchema> includedir;
    std::optional<DefinesListSchema> defines;
    rfl::Rename<"user-include", std::optional<std::string>> user_include;
    rfl::Rename<"vs-configurations", std::optional<VsConfigsSchema>> vs_configurations;
    std::optional<UndefinesListSchema> undefines;
    std::optional<PathsListSchema> paths;
    std::optional<ExcludeListSchema> exclude;
    std::optional<ExcludeListSchema> ignore;
    rfl::Rename<"function-contracts", std::optional<std::string>> function_contracts;
    rfl::Rename<"variable-contracts", std::optional<std::string>> variable_contracts;
    std::optional<LibrariesListSchema> libraries;
    std::optional<ProjectSuppressionsSchema> suppressions;
    rfl::Rename<"check-unknown-function-return-values", std::optional<CheckUnknownFunctionReturnSchema>> check_unknown_function_return_values;
    rfl::Rename<"safe-checks", std::optional<SafeChecksSchema>> safe_checks;
    std::optional<AddonsListSchema> addons;
    std::optional<ToolsListSchema> tools;
    std::optional<TagsListSchema> tags;
    rfl::Rename<"tag-warnings", std::optional<std::vector<TagWarningsSchema>>> tag_warnings;
    rfl::Rename<"check-level-reduced", std::optional<EmptyElement>> check_level_reduced;
    rfl::Rename<"check-level-normal", std::optional<EmptyElement>> check_level_normal;
    rfl::Rename<"check-level-exhaustive", std::optional<EmptyElement>> check_level_exhaustive;
    rfl::Rename<"bug-hunting", std::optional<EmptyElement>> bug_hunting;
    rfl::Rename<"coding-standards", std::optional<CodingStandardsListSchema>> coding_standards;
    rfl::Rename<"cert-c-int-precision", std::optional<int>> cert_c_int_precision;
    rfl::Rename<"project-name", std::optional<std::string>> project_name;
};

template <class Schema = ProjectFileSchema>
inline std::optional<Schema> parseProjectFileXml(std::string_view xml,
                                                 bool strictUnknownElements,
                                                 std::string& errorMessage) {
    if (std::all_of(xml.cbegin(), xml.cend(), [](unsigned char c) { return std::isspace(c) != 0; })) {
        errorMessage = "Cppcheck GUI project file is not a valid XML - XML_ERROR_EMPTY_DOCUMENT";
        return std::nullopt;
    }

    pugi::xml_document doc;
    const pugi::xml_parse_result res = doc.load_buffer(xml.data(), xml.size());
    if (res.status == pugi::status_no_document_element) {
        errorMessage = "Cppcheck GUI project file has no XML root node";
        return std::nullopt;
    }
    if (!res) {
        errorMessage = std::string("Cppcheck GUI project file is not a valid XML - ") + res.description();
        return std::nullopt;
    }

    pugi::xml_node root = doc.document_element();
    if (!root || std::string_view(root.name()) != "project") {
        errorMessage = "Cppcheck GUI project file has no XML root node";
        return std::nullopt;
    }

    if (strictUnknownElements) {
        static constexpr std::array<std::string_view, 36> kKnownProjectElements = {
            "root",
            "builddir",
            "platform",
            "importproject",
            "analyze-all-vs-configs",
            "parser",
            "check-headers",
            "check-unused-templates",
            "inline-suppression",
            "max-ctu-depth",
            "max-template-recursion",
            "includedir",
            "defines",
            "user-include",
            "vs-configurations",
            "undefines",
            "paths",
            "exclude",
            "ignore",
            "function-contracts",
            "variable-contracts",
            "libraries",
            "suppressions",
            "check-unknown-function-return-values",
            "safe-checks",
            "addons",
            "tools",
            "tags",
            "tag-warnings",
            "check-level-reduced",
            "check-level-normal",
            "check-level-exhaustive",
            "bug-hunting",
            "coding-standards",
            "cert-c-int-precision",
            "project-name",
        };
        static constexpr std::array<std::string_view, 4> kKnownSafeChecksElements = {
            "class-public",
            "external-functions",
            "internal-functions",
            "external-variables",
        };

        for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling()) {
            if (child.type() != pugi::node_element)
                continue;
            const std::string_view name = child.name();
            if (std::find(kKnownProjectElements.cbegin(), kKnownProjectElements.cend(), name) == kKnownProjectElements.cend()) {
                errorMessage = "Unknown element '" + std::string(name) + "' in Cppcheck GUI project file";
                return std::nullopt;
            }
            if (name == "safe-checks") {
                for (pugi::xml_node sc = child.first_child(); sc; sc = sc.next_sibling()) {
                    if (sc.type() != pugi::node_element)
                        continue;
                    const std::string_view scName = sc.name();
                    if (std::find(kKnownSafeChecksElements.cbegin(), kKnownSafeChecksElements.cend(), scName) == kKnownSafeChecksElements.cend()) {
                        errorMessage = "Unknown 'safe-checks' element '" + std::string(scName) + "' in Cppcheck GUI project file";
                        return std::nullopt;
                    }
                }
            }
        }
    }

    // Normalize empty self-closing boolean/integer tags before reflect-cpp XML deserialization.
    for (pugi::xml_node child = root.first_child(); child;) {
        pugi::xml_node next = child.next_sibling();
        if (child.type() == pugi::node_element && child.child_value()[0] == '\0') {
            const std::string_view name = child.name();
            if (name == "analyze-all-vs-configs")
                child.text().set(strictUnknownElements ? "true" : "false");
            else if (name == "check-headers" || name == "check-unused-templates" || name == "inline-suppression")
                child.text().set("false");
            else if (name == "max-ctu-depth" || name == "max-template-recursion" || name == "cert-c-int-precision")
                root.remove_child(child);
        }
        child = next;
    }

    const auto parsed = rfl::xml::read<Schema>(root);
    if (!parsed) {
        errorMessage = parsed.error().what();
        return std::nullopt;
    }
    return *parsed;
}

template <class Schema = ProjectFileSchema>
inline std::string writeProjectFileXml(const Schema& s) {
    return rfl::xml::write<"project">(s, "    ");
}

template <class Schema = ProjectFileSchema>
inline rfl::Result<Schema> parseProjectFileJson(std::string_view json) {
    return rfl::json::read<Schema>(json);
}

template <class Schema = ProjectFileSchema>
inline std::string writeProjectFileJson(const Schema& s) {
    return rfl::json::write(s);
}

template <class Schema = ProjectFileSchema>
inline std::string projectFileJsonSchema() {
    return rfl::json::to_schema<Schema>();
}

} // namespace cppcheck::schemas

#endif // schemasH
