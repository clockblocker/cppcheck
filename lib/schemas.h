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
#include <string>
#include <string_view>

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

} // namespace cppcheck::schemas

#endif // schemasH
