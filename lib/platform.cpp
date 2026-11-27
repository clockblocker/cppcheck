/*
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

#include "platform.h"

#include "path.h"
#include "schemas.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

Platform::Platform()
{
    set(Type::Native);
}


bool Platform::set(Type t)
{
    switch (t) {
    case Type::Unspecified: // unknown type sizes (sizes etc are set but are not known)
    case Type::Native: // same as system this code was compile on
        type = t;
        windows = false;
        sizeof_bool = sizeof(bool);
        sizeof_short = sizeof(short);
        sizeof_int = sizeof(int);
        sizeof_long = sizeof(long);
        sizeof_long_long = sizeof(long long);
        sizeof_float = sizeof(float);
        sizeof_double = sizeof(double);
        sizeof_long_double = sizeof(long double);
        sizeof_wchar_t = sizeof(wchar_t);
        sizeof_size_t = sizeof(std::size_t);
        sizeof_pointer = sizeof(void *);
        if (type == Type::Unspecified) {
            defaultSign = '\0';
        } else {
            defaultSign = std::numeric_limits<char>::is_signed ? 's' : 'u';
        }
        char_bit = 8;
        calculateBitMembers();
        return true;
    case Type::Win32W:
    case Type::Win32A:
        type = t;
        windows = true;
        sizeof_bool = 1; // 4 in Visual C++ 4.2
        sizeof_short = 2;
        sizeof_int = 4;
        sizeof_long = 4;
        sizeof_long_long = 8;
        sizeof_float = 4;
        sizeof_double = 8;
        sizeof_long_double = 8;
        sizeof_wchar_t = 2;
        sizeof_size_t = 4;
        sizeof_pointer = 4;
        defaultSign = 's';
        char_bit = 8;
        calculateBitMembers();
        return true;
    case Type::Win64:
        type = t;
        windows = true;
        sizeof_bool = 1;
        sizeof_short = 2;
        sizeof_int = 4;
        sizeof_long = 4;
        sizeof_long_long = 8;
        sizeof_float = 4;
        sizeof_double = 8;
        sizeof_long_double = 8;
        sizeof_wchar_t = 2;
        sizeof_size_t = 8;
        sizeof_pointer = 8;
        defaultSign = 's';
        char_bit = 8;
        calculateBitMembers();
        return true;
    case Type::WinARM64:
        type = t;
        windows = true;
        sizeof_bool = 1;
        sizeof_short = 2;
        sizeof_int = 4;
        sizeof_long = 4;
        sizeof_long_long = 8;
        sizeof_float = 4;
        sizeof_double = 8;
        sizeof_long_double = 8;
        sizeof_wchar_t = 2;
        sizeof_size_t = 8;
        sizeof_pointer = 8;
        defaultSign = 's';
        char_bit = 8;
        calculateBitMembers();
        return true;
    case Type::WinARM64EC:
        // ARM64EC is an x64-ABI on ARM64 hardware (VS 2022+).
        // Pointer width and size_t are 64-bit; all other layout matches WinARM64.
        type = t;
        windows = true;
        sizeof_bool = 1;
        sizeof_short = 2;
        sizeof_int = 4;
        sizeof_long = 4;
        sizeof_long_long = 8;
        sizeof_float = 4;
        sizeof_double = 8;
        sizeof_long_double = 8;
        sizeof_wchar_t = 2;
        sizeof_size_t = 8;
        sizeof_pointer = 8;
        defaultSign = 's';
        char_bit = 8;
        calculateBitMembers();
        return true;
    case Type::WinARM:
        type = t;
        windows = true;
        sizeof_bool = 1;
        sizeof_short = 2;
        sizeof_int = 4;
        sizeof_long = 4;
        sizeof_long_long = 8;
        sizeof_float = 4;
        sizeof_double = 8;
        sizeof_long_double = 8;
        sizeof_wchar_t = 2;
        sizeof_size_t = 4;
        sizeof_pointer = 4;
        defaultSign = 's';
        char_bit = 8;
        calculateBitMembers();
        return true;
    case Type::Unix32:
        type = t;
        windows = false;
        sizeof_bool = 1;
        sizeof_short = 2;
        sizeof_int = 4;
        sizeof_long = 4;
        sizeof_long_long = 8;
        sizeof_float = 4;
        sizeof_double = 8;
        sizeof_long_double = 12;
        sizeof_wchar_t = 4;
        sizeof_size_t = 4;
        sizeof_pointer = 4;
        defaultSign = 's';
        char_bit = 8;
        calculateBitMembers();
        return true;
    case Type::Unix64:
        type = t;
        windows = false;
        sizeof_bool = 1;
        sizeof_short = 2;
        sizeof_int = 4;
        sizeof_long = 8;
        sizeof_long_long = 8;
        sizeof_float = 4;
        sizeof_double = 8;
        sizeof_long_double = 16;
        sizeof_wchar_t = 4;
        sizeof_size_t = 8;
        sizeof_pointer = 8;
        defaultSign = 's';
        char_bit = 8;
        calculateBitMembers();
        return true;
    case Type::File:
        // sizes are not set.
        return false;
    }
    // unsupported platform
    return false;
}

bool Platform::set(const std::string& platformstr, std::string& errstr, const std::vector<std::string>& paths, bool debug)
{
    // TODO: needs to be invalidated in case it was already set
    if (platformstr == "win32A")
        set(Type::Win32A);
    else if (platformstr == "win32W")
        set(Type::Win32W);
    else if (platformstr == "win64")
        set(Type::Win64);
    else if (platformstr == "winARM64")
        set(Type::WinARM64);
    else if (platformstr == "winARM64EC")
        set(Type::WinARM64EC);
    else if (platformstr == "winARM")
        set(Type::WinARM);
    else if (platformstr == "unix32")
        set(Type::Unix32);
    else if (platformstr == "unix64")
        set(Type::Unix64);
    else if (platformstr == "native")
        set(Type::Native);
    else if (platformstr == "unspecified")
        set(Type::Unspecified);
    else if (paths.empty()) {
        errstr = "unrecognized platform: '" + platformstr + "' (no lookup).";
        return false;
    }
    else if (!loadFromFile(paths, platformstr, debug)) {
        errstr = "unrecognized platform: '" + platformstr + "'.";
        return false;
    }

    return true;
}

bool Platform::loadFromFile(const std::vector<std::string>& paths, const std::string &filename, bool debug)
{
    if (debug)
        std::cout << "looking for platform '" + filename + "'" << std::endl;

    const bool is_abs_path = Path::isAbsolute(filename);
    const bool is_rel_path = Path::isRelative(filename);

    std::string fullfilename(filename);
    // TODO: what if extension is not .xml?
    // only append extension when we provide the library name is not a path - TODO: handle relative paths?
    if (!is_abs_path && !is_rel_path && Path::getFilenameExtension(fullfilename).empty())
        fullfilename += ".xml";

    // TODO: use native separators
    std::vector<std::string> filenames;
    if (is_abs_path)
    {
        filenames.push_back(std::move(fullfilename));
    }
    else {
        // TODO: drop duplicated paths
        for (const std::string& path : paths)
        {
            if (path.empty())
                continue; // TODO: error out instead?

            std::string ppath = Path::fromNativeSeparators(path);
            if (ppath.back() != '/')
                ppath += '/';
            // TODO: look in platforms first?
            filenames.push_back(ppath + fullfilename);
            filenames.push_back(ppath + "platforms/" + fullfilename);
        }
#ifdef FILESDIR
        std::string filesdir = FILESDIR;
        if (!filesdir.empty()) {
            if (filesdir.back() != '/')
                filesdir += '/';
            // TODO: look in filesdir?
            filenames.push_back(filesdir + "platforms/" + fullfilename);
        }
#endif
    }

    // open file..
    std::string content;
    std::string loadedFile;
    bool opened = false;
    for (const std::string & f : filenames) {
        if (debug)
            std::cout << "try to load platform file '" << f << "' ... ";
        std::ifstream fin(f);
        if (fin.is_open()) {
            content.assign(std::istreambuf_iterator<char>(fin), std::istreambuf_iterator<char>());
            loadedFile = f;
            opened = true;
            break;
        }
        if (debug)
            std::cout << "file not found" << std::endl;
    }
    if (!opened)
        return false;

    const bool ok = (Path::getFilenameExtensionInLowerCase(loadedFile) == ".json")
                        ? loadFromJson(content)
                        : loadFromXml(content);
    if (debug)
        std::cout << (ok ? "Success" : "Failed to parse") << std::endl;
    return ok;
}

static bool applyPlatformSchema(Platform& platform, const cppcheck::schemas::PlatformSchema& schema)
{
    if (schema.default_sign.value().has_value()) {
        const std::string& sign = *schema.default_sign.value();
        if (sign.empty())
            return false;
        platform.defaultSign = sign[0];
    }
    if (schema.char_bit.has_value())
        platform.char_bit = static_cast<std::uint8_t>(*schema.char_bit);
    if (schema.sizeof_types.value().has_value()) {
        const auto& sz = *schema.sizeof_types.value();
        if (sz.short_size.value().has_value())
            platform.sizeof_short = *sz.short_size.value();
        if (sz.bool_size.value().has_value())
            platform.sizeof_bool = *sz.bool_size.value();
        if (sz.int_size.value().has_value())
            platform.sizeof_int = *sz.int_size.value();
        if (sz.long_size.value().has_value())
            platform.sizeof_long = *sz.long_size.value();
        if (sz.long_long_size.value().has_value())
            platform.sizeof_long_long = *sz.long_long_size.value();
        if (sz.float_size.value().has_value())
            platform.sizeof_float = *sz.float_size.value();
        if (sz.double_size.value().has_value())
            platform.sizeof_double = *sz.double_size.value();
        if (sz.long_double_size.value().has_value())
            platform.sizeof_long_double = *sz.long_double_size.value();
        if (sz.pointer_size.value().has_value())
            platform.sizeof_pointer = *sz.pointer_size.value();
        if (sz.size_t_size.value().has_value())
            platform.sizeof_size_t = *sz.size_t_size.value();
        if (sz.wchar_t_size.value().has_value())
            platform.sizeof_wchar_t = *sz.wchar_t_size.value();
    }
    if (schema.windows.has_value())
        platform.windows = *schema.windows;
    return true;
}

static cppcheck::schemas::PlatformSchema toPlatformSchema(const Platform& platform)
{
    cppcheck::schemas::PlatformSchema schema;
    schema.char_bit = platform.char_bit;
    if (platform.defaultSign == 's')
        schema.default_sign = std::string("signed");
    else if (platform.defaultSign == 'u')
        schema.default_sign = std::string("unsigned");
    else if (platform.defaultSign != '\0')
        schema.default_sign = std::string(1, platform.defaultSign);
    cppcheck::schemas::PlatformSizeofSchema sz;
    sz.bool_size = platform.sizeof_bool;
    sz.short_size = platform.sizeof_short;
    sz.int_size = platform.sizeof_int;
    sz.long_size = platform.sizeof_long;
    sz.long_long_size = platform.sizeof_long_long;
    sz.float_size = platform.sizeof_float;
    sz.double_size = platform.sizeof_double;
    sz.long_double_size = platform.sizeof_long_double;
    sz.pointer_size = platform.sizeof_pointer;
    sz.size_t_size = platform.sizeof_size_t;
    sz.wchar_t_size = platform.sizeof_wchar_t;
    schema.sizeof_types = sz;
    schema.windows = platform.windows;
    return schema;
}

bool Platform::loadFromXml(std::string_view xml)
{
    const auto parsed = cppcheck::schemas::parsePlatformXml(xml);
    if (!parsed)
        return false;
    if (!applyPlatformSchema(*this, *parsed))
        return false;
    calculateBitMembers();
    type = Type::File;
    return true;
}

bool Platform::loadFromJson(std::string_view json)
{
    const auto parsed = cppcheck::schemas::parsePlatformJson(json);
    if (!parsed)
        return false;
    if (!applyPlatformSchema(*this, *parsed))
        return false;
    calculateBitMembers();
    type = Type::File;
    return true;
}

std::string Platform::toXml() const
{
    return cppcheck::schemas::writePlatformXml(toPlatformSchema(*this));
}

std::string Platform::toJson() const
{
    return cppcheck::schemas::writePlatformJson(toPlatformSchema(*this));
}

std::string Platform::jsonSchema()
{
    return cppcheck::schemas::platformJsonSchema();
}

std::string Platform::getLimitsDefines(bool c99) const
{
    std::string s;

    // climits / limits.h
    s += "CHAR_BIT=";
    s += std::to_string(char_bit);
    s += ";SCHAR_MIN=";
    s += std::to_string(min_value(char_bit));
    s += ";SCHAR_MAX=";
    s += std::to_string(max_value(char_bit));
    s += ";UCHAR_MAX=";
    s += std::to_string(max_value(char_bit+1));
    s += ";CHAR_MIN=";
    if (defaultSign == 'u')
        s += std::to_string(min_value(char_bit));
    else
        s += std::to_string(0);
    s += ";CHAR_MAX=";
    if (defaultSign == 'u')
        s += std::to_string(max_value(char_bit+1));
    else
        s += std::to_string(max_value(char_bit));
    // TODO
    //s += ";MB_LEN_MAX=";
    s += ";SHRT_MIN=";
    s += std::to_string(min_value(short_bit));
    s += ";SHRT_MAX=";
    s += std::to_string(max_value(short_bit));
    s += ";USHRT_MAX=";
    s += std::to_string(max_value(short_bit+1));
    s += ";INT_MIN=";
    s += "(-" + std::to_string(max_value(int_bit)) + " - 1)";
    s += ";INT_MAX=";
    s += std::to_string(max_value(int_bit));
    s += ";UINT_MAX=";
    s += std::to_string(max_value(int_bit+1));
    s += ";LONG_MIN=";
    s += "(-" + std::to_string(max_value(long_bit)) + "L - 1L)";
    s += ";LONG_MAX=";
    s += std::to_string(max_value(long_bit)) + "L";
    s += ";ULONG_MAX=";
    s += std::to_string(max_value_unsigned(long_bit)) + "UL";
    if (c99) {
        s += ";LLONG_MIN=";
        s += "(-" + std::to_string(max_value(long_long_bit)) + "LL - 1LL)";
        s += ";LLONG_MAX=";
        s += std::to_string(max_value(long_long_bit)) + "LL";
        s += ";ULLONG_MAX=";
        s += std::to_string(max_value_unsigned(long_long_bit)) + "ULL";
    }

    // cstdint / stdint.h
    // FIXME: these are currently hard-coded in std.cfg
    /*
        INTMAX_MIN
        INTMAX_MAX
        UINTMAX_MAX
        INTN_MIN
        INTN_MAX
        UINTN_MAX
        INT_LEASTN_MIN
        INT_LEASTN_MAX
        UINT_LEASTN_MAX
        INT_FASTN_MIN
        INT_FASTN_MAX
        UINT_FASTN_MAX
        INTPTR_MIN
        INTPTR_MAX
        UINTPTR_MAX
        SIZE_MAX
        PTRDIFF_MIN
        PTRDIFF_MAX
        SIG_ATOMIC_MIN
        SIG_ATOMIC_MAX
        WCHAR_MIN
        WCHAR_MAX
        WINT_MIN
        WINT_MAX

        // function-like macros
        // implemented in std.cfg
        INTMAX_C
        UINTMAX_C
        INTN_C
        UINTN_C
     */

    // cfloat / float.h
    /*
        // TODO: implement
        FLT_RADIX

        FLT_MANT_DIG
        DBL_MANT_DIG
        LDBL_MANT_DIG

        FLT_DIG
        DBL_DIG
        LDBL_DIG

        FLT_MIN_EXP
        DBL_MIN_EXP
        LDBL_MIN_EXP

        FLT_MIN_10_EXP
        DBL_MIN_10_EXP
        LDBL_MIN_10_EXP

        FLT_MAX_EXP
        DBL_MAX_EXP
        LDBL_MAX_EXP

        FLT_MAX_10_EXP
        DBL_MAX_10_EXP
        LDBL_MAX_10_EXP

        FLT_MAX
        DBL_MAX
        LDBL_MAX

        FLT_EPSILON
        DBL_EPSILON
        LDBL_EPSILON

        FLT_MIN
        DBL_MIN
        LDBL_MIN

        FLT_ROUNDS

        // C99 / C++11 only
        FLT_EVAL_METHOD

        DECIMAL_DIG
     */

    return s;
}

std::string Platform::getLimitsDefines(Standards::cstd_t cstd) const
{
    return getLimitsDefines(cstd >= Standards::cstd_t::C99);
}

std::string Platform::getLimitsDefines(Standards::cppstd_t cppstd) const
{
    return getLimitsDefines(cppstd >= Standards::cppstd_t::CPP11);
}
