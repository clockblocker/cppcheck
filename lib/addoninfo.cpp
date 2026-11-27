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

#include "addoninfo.h"

#include "path.h"
#include "utils.h"

#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <rfl/json.hpp>

static std::string getFullPath(const std::string &fileName, const std::string &exename, bool debug = false) {
    if (debug)
        std::cout << "looking for addon '" << fileName << "'" << std::endl;
    if (Path::isFile(fileName))
        return fileName;

    const bool is_abs_path = Path::isAbsolute(fileName);
    if (is_abs_path)
        return "";

    const std::string exepath = Path::getPathFromFilename(exename);
    {
        std::string p = Path::join(exepath, fileName);
        if (debug)
            std::cout << "looking for addon '" << p << "'" << std::endl;
        if (Path::isFile(p))
            return p;
    }
    {
        std::string p = Path::join(exepath, "addons", fileName);
        if (debug)
            std::cout << "looking for addon '" << p << "'" << std::endl;
        if (Path::isFile(p))
            return p;
    }

#ifdef FILESDIR
    {
        std::string p = Path::join(FILESDIR, fileName);
        if (debug)
            std::cout << "looking for addon '" << p << "'" << std::endl;
        if (Path::isFile(p))
            return p;
    }
    {
        std::string p = Path::join(FILESDIR, "addons", fileName);
        if (debug)
            std::cout << "looking for addon '" << p << "'" << std::endl;
        if (Path::isFile(p))
            return p;
    }
#endif
    return "";
}

namespace {
struct AddonInfoJson {
    std::optional<std::vector<std::string>> args;
    std::optional<bool> ctu;
    std::optional<std::string> python;
    std::optional<std::vector<std::map<std::string, std::string>>> checkers;
    std::optional<std::string> executable;
    std::optional<std::string> script;
};
}

static std::string formatAddonError(const rfl::Error& err, const std::string& fileName) {
    const std::string prefix = "Loading " + fileName + " failed. ";
    if (err.kind() == rfl::ErrorKind::syntax_error)
        return prefix + err.what();

    for (const auto& d : err.details()) {
        if (d.path.empty()) {
            if (d.kind == rfl::ErrorKind::invalid_type)
                return prefix + "JSON is not an object.";
            continue;
        }
        const auto* field = std::get_if<std::string>(&d.path.front());
        if (!field)
            continue;
        if (d.kind == rfl::ErrorKind::missing_field)
            return prefix + "'" + *field + "' is missing.";
        if (d.kind == rfl::ErrorKind::invalid_type) {
            if (*field == "args")
                return prefix + (d.path.size() == 1 ? "'args' must be an array." : "'args' entry is not a string.");
            if (*field == "ctu")
                return prefix + "'ctu' must be a boolean.";
            if (*field == "python")
                return prefix + "'python' must be a string.";
            if (*field == "checkers") {
                if (d.path.size() == 1)
                    return prefix + "'checkers' must be an array.";
                if (d.path.size() == 2)
                    return prefix + "'checkers' entry is not an object.";
                return prefix + "'checkers' entry requirement is not a string.";
            }
            if (*field == "executable")
                return prefix + "'executable' must be a string.";
            if (*field == "script")
                return prefix + "'script' must be a string.";
        }
    }
    return prefix + err.what();
}

static std::string parseAddonInfo(AddonInfo& addoninfo, const rfl::Result<AddonInfoJson>& parsed, const std::string& fileName, const std::string& exename) {
    if (!parsed)
        return formatAddonError(parsed.error(), fileName);

    const AddonInfoJson& cfg = *parsed;
    if (cfg.args) {
        for (const std::string& v : *cfg.args)
            addoninfo.args += " " + v;
    }
    addoninfo.ctu = cfg.ctu.value_or(false);
    addoninfo.python = cfg.python.value_or("");
    if (cfg.checkers) {
        for (const auto& checkerObj : *cfg.checkers) {
            if (checkerObj.size() == 1)
                addoninfo.checkers.emplace(checkerObj.begin()->first, checkerObj.begin()->second);
        }
    }
    if (cfg.executable) {
        std::string e = *cfg.executable;
        addoninfo.executable = getFullPath(e, fileName);
        if (addoninfo.executable.empty())
            addoninfo.executable = std::move(e);
        return ""; // <- do not load both "executable" and "script".
    }
    if (!cfg.script)
        return "Loading " + fileName + " failed. 'script' is missing.";

    return addoninfo.getAddonInfo(*cfg.script, exename);
}

std::string AddonInfo::getAddonInfo(const std::string &fileName, const std::string &exename, bool debug) {
    if (!fileName.empty() && fileName[0] == '{')
        return parseAddonInfo(*this, rfl::json::read<AddonInfoJson>(fileName), fileName, exename);
    if (fileName.find('.') == std::string::npos)
        return getAddonInfo(fileName + ".py", exename, debug);

    if (endsWith(fileName, ".py")) {
        scriptFile = Path::fromNativeSeparators(getFullPath(fileName, exename, debug));
        if (scriptFile.empty())
            return "Did not find addon " + fileName;

        std::string::size_type pos1 = scriptFile.rfind('/');
        if (pos1 == std::string::npos)
            pos1 = 0;
        else
            pos1++;
        std::string::size_type pos2 = scriptFile.rfind('.');
        if (pos2 < pos1)
            pos2 = std::string::npos;
        name = scriptFile.substr(pos1, pos2 - pos1);

        runScript = getFullPath("runaddon.py", exename);

        return "";
    }

    if (!endsWith(fileName, ".json"))
        return "Failed to open addon " + fileName;

    std::ifstream fin(fileName);
    if (!fin.is_open())
        return "Failed to open " + fileName;
    if (name.empty()) {
        name = Path::fromNativeSeparators(fileName);
        if (name.find('/') != std::string::npos)
            name = name.substr(name.rfind('/') + 1);
    }
    return parseAddonInfo(*this, rfl::json::read<AddonInfoJson>(fin), fileName, exename);
}
