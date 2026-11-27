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

inline rfl::Result<PlatformSchema> parsePlatformXmlNode(const pugi::xml_node& root) {
    if (!root || std::string_view(root.name()) != "platform")
        return rfl::error("Expected <platform> root element.");
    return rfl::xml::read<PlatformSchema>(root);
}

inline rfl::Result<PlatformSchema> parsePlatformXml(std::string_view xml) {
    pugi::xml_document doc;
    const auto res = doc.load_buffer(xml.data(), xml.size());
    if (!res)
        return rfl::error(std::string("XML parse error: ") + res.description());
    return parsePlatformXmlNode(doc.document_element());
}

inline std::string writePlatformXml(const PlatformSchema& s) {
    return rfl::xml::write<"platform">(s, "  ");
}

inline rfl::Result<PlatformSchema> parsePlatformJson(std::string_view json) {
    return rfl::json::read<PlatformSchema>(json);
}

inline std::string writePlatformJson(const PlatformSchema& s) {
    return rfl::json::write(s);
}

inline std::string platformJsonSchema() {
    return rfl::json::to_schema<PlatformSchema>();
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

inline std::string writeSuppressionsXml(const SuppressionsFileSchema& s) {
    return rfl::xml::write<"suppressions">(s, "  ");
}

inline std::string writeDumpSuppressionsXml(const DumpSuppressionsSchema& s) {
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

inline rfl::Result<SuppressionsFileSchema> parseSuppressionsJson(std::string_view json) {
    return rfl::json::read<SuppressionsFileSchema>(json);
}

inline std::string writeSuppressionsJson(const SuppressionsFileSchema& s) {
    return rfl::json::write(s);
}

inline std::string suppressionsJsonSchema() {
    return rfl::json::to_schema<SuppressionsFileSchema>();
}

inline std::string dumpSuppressionsJsonSchema() {
    return rfl::json::to_schema<DumpSuppressionsSchema>();
}

} // namespace cppcheck::schemas

#endif // schemasH
