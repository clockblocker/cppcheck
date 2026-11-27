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

#include "suppressions.h"

#include "addoninfo.h"
#include "errorlogger.h"
#include "filesettings.h"
#include "path.h"
#include "pathmatch.h"
#include "schemas.h"
#include "utils.h"
#include "token.h"
#include "tokenlist.h"
#include "settings.h"

#include <algorithm>
#include <cctype>   // std::isdigit, std::isalnum, etc
#include <exception>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <utility>

static const char ID_CHECKERSREPORT[] = "checkersReport";

SuppressionList::ErrorMessage SuppressionList::ErrorMessage::fromErrorMessage(const ::ErrorMessage &msg, const std::set<std::string> &macroNames)
{
    SuppressionList::ErrorMessage ret;
    ret.hash = msg.hash;
    ret.errorId = msg.id;
    if (!msg.callStack.empty()) {
        ret.setFileName(msg.callStack.back().getfile(false));
        ret.lineNumber = msg.callStack.back().line;
    } else {
        ret.setFileName(msg.file0);
        ret.lineNumber = SuppressionList::Suppression::NO_LINE;
    }
    ret.certainty = msg.certainty;
    ret.symbolNames = msg.symbolNames();
    ret.macroNames = macroNames;
    return ret;
}

static bool isAcceptedErrorIdChar(char c)
{
    switch (c) {
    case '_':
    case '-':
    case '.':
    case '*':
        return true;
    default:
        return c > 0 && std::isalnum(c);
    }
}

std::string SuppressionList::parseFile(std::istream &istr)
{
    // Change '\r' to '\n' in the istr
    std::string filedata;
    std::string line;
    while (std::getline(istr, line))
        filedata += line + "\n";
    std::replace(filedata.begin(), filedata.end(), '\r', '\n');

    // Parse filedata..
    std::istringstream istr2(filedata);
    while (std::getline(istr2, line)) {
        // Skip empty lines
        if (line.empty())
            continue;

        std::string::size_type pos = 0;
        while (pos < line.size() && std::isspace(line[pos]))
            ++pos;
        if (pos == line.size())
            continue;

        // Skip comments
        if (line[pos] == '#')
            continue;
        if (pos < line.size() - 1 && line[pos] == '/' && line[pos + 1] == '/')
            continue;

        std::string errmsg(addSuppressionLine(line));
        if (!errmsg.empty())
            return errmsg;
    }

    return "";
}


static SuppressionList::Suppression suppressionFromSchema(const cppcheck::schemas::SuppressEntrySchema& entry)
{
    SuppressionList::Suppression s;
    if (entry.id.has_value())
        s.errorId = *entry.id;
    if (entry.fileName.has_value())
        s.fileName = Path::simplifyPath(*entry.fileName);
    if (entry.lineNumber.has_value())
        s.lineNumber = *entry.lineNumber;
    if (entry.symbolName.has_value())
        s.symbolName = *entry.symbolName;
    if (entry.macroName.has_value()) {
        s.macroName = *entry.macroName;
        s.type = SuppressionList::Type::macro;
    }
    if (entry.hash.has_value())
        s.hash = *entry.hash;
    return s;
}

static cppcheck::schemas::SuppressionsFileSchema toSuppressionsFileSchema(const std::list<SuppressionList::Suppression>& suppressions)
{
    cppcheck::schemas::SuppressionsFileSchema schema;
    schema.suppress.reserve(suppressions.size());
    for (const auto& s : suppressions) {
        cppcheck::schemas::SuppressEntrySchema entry;
        if (!s.errorId.empty())
            entry.id = s.errorId;
        if (!s.fileName.empty())
            entry.fileName = s.fileName;
        if (s.lineNumber != SuppressionList::Suppression::NO_LINE)
            entry.lineNumber = s.lineNumber;
        if (!s.symbolName.empty())
            entry.symbolName = s.symbolName;
        if (!s.macroName.empty())
            entry.macroName = s.macroName;
        if (s.hash > 0)
            entry.hash = s.hash;
        schema.suppress.push_back(std::move(entry));
    }
    return schema;
}

std::string SuppressionList::parseXmlFile(const char *filename)
{
    const std::string fname = filename ? filename : "";
    std::ifstream fin(fname, std::ios::binary);
    if (!filename || !fin.is_open())
        return "failed to load suppressions XML '" + fname + "' (XML_ERROR_FILE_NOT_FOUND).";

    const std::string content((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
    if (std::all_of(content.cbegin(), content.cend(), [](unsigned char c) { return std::isspace(c) != 0; }))
        return "failed to load suppressions XML '" + fname + "' (XML_ERROR_EMPTY_DOCUMENT).";

    pugi::xml_document doc;
    const pugi::xml_parse_result parseRes = doc.load_buffer(content.data(), content.size());
    if (parseRes.status == pugi::status_no_document_element)
        return "failed to load suppressions XML '" + fname + "' (no root node found).";
    if (!parseRes)
        return "failed to load suppressions XML '" + fname + "' (" + parseRes.description() + ").";

    const pugi::xml_node rootnode = doc.document_element();
    if (!rootnode)
        return "failed to load suppressions XML '" + fname + "' (no root node found).";

    // TODO: check for proper root node 'suppressions'
    for (pugi::xml_node e = rootnode.first_child(); e; e = e.next_sibling()) {
        if (e.type() != pugi::node_element)
            continue;
        if (std::string_view(e.name()) != "suppress")
            return "invalid suppression xml file '" + fname + "', expected 'suppress' element but got a '" + e.name() + "'.";

        for (pugi::xml_node e2 = e.first_child(); e2; e2 = e2.next_sibling()) {
            if (e2.type() != pugi::node_element)
                continue;
            const std::string_view name = e2.name();
            const char *text = e2.child_value();
            const bool valid =
                (name == "id") ||
                (name == "fileName") ||
                (name == "lineNumber") ||
                (name == "symbolName") ||
                (name == "macroName") ||
                (name == "hash" && text && *text != '\0');
            if (!valid)
                return "unknown element '" + std::string(name) + "' in suppressions XML '" + fname + "', expected id/fileName/lineNumber/symbolName/hash.";
        }

        const auto entry = rfl::xml::read<cppcheck::schemas::SuppressEntrySchema>(e);
        if (!entry)
            return "failed to load suppressions XML '" + fname + "' (" + entry.error().what() + ").";

        std::string err = addSuppression(suppressionFromSchema(*entry));
        if (!err.empty())
            return err;
    }

    return "";
}

std::string SuppressionList::parseJson(std::string_view json)
{
    const auto parsed = cppcheck::schemas::parseSuppressionsJson(json);
    if (!parsed)
        return "failed to load suppressions JSON (" + parsed.error().what() + ").";

    for (const auto& entry : parsed->suppress) {
        std::string err = addSuppression(suppressionFromSchema(entry));
        if (!err.empty())
            return err;
    }
    return "";
}

std::string SuppressionList::toXml() const
{
    std::lock_guard<std::mutex> lg(mSuppressionsSync);
    return cppcheck::schemas::writeSuppressionsXml(toSuppressionsFileSchema(mSuppressions));
}

std::string SuppressionList::toJson() const
{
    std::lock_guard<std::mutex> lg(mSuppressionsSync);
    return cppcheck::schemas::writeSuppressionsJson(toSuppressionsFileSchema(mSuppressions));
}

std::string SuppressionList::jsonSchema()
{
    return cppcheck::schemas::suppressionsJsonSchema();
}

static std::string getExtraComment(const std::string &comment, std::string::size_type startPos, std::string::size_type *delimPos = nullptr)
{
    const std::string::size_type semiPos = comment.find(';', startPos);
    const std::string::size_type slashPos = comment.find("//", startPos);
    std::string::size_type pos;

    if (delimPos)
        *delimPos = std::min(semiPos, slashPos);

    if (semiPos < slashPos) {
        pos = semiPos + 1;
    } else if (slashPos < semiPos) {
        pos = slashPos + 2;
    } else {
        return "";
    }

    std::string extra = comment.substr(pos);

    if (startsWith(comment, "/*") && endsWith(comment, "*/"))
        extra.erase(extra.size() - 2, 2);

    extra = trim(extra);

    for (auto it = extra.begin(); it != extra.end();)
        it = (*it & 0x80) ? extra.erase(it) : it + 1;

    return extra;
}

std::vector<SuppressionList::Suppression> SuppressionList::parseMultiSuppressComment(const std::string &comment, std::string *errorMessage)
{
    std::vector<Suppression> suppressions;

    // If this function is called we assume that comment starts with "cppcheck-suppress[".
    const std::string::size_type start_position = comment.find('[');
    const std::string::size_type end_position = comment.find(']', start_position);
    if (end_position == std::string::npos) {
        if (errorMessage && errorMessage->empty())
            *errorMessage = "Bad multi suppression '" + comment + "'. legal format is cppcheck-suppress[errorId, errorId symbolName=arr, ...]";
        return suppressions;
    }

    // parse all suppressions
    for (std::string::size_type pos = start_position; pos < end_position;) {
        const std::string::size_type pos1 = pos + 1;
        pos = comment.find(',', pos1);
        const std::string::size_type pos2 = (pos < end_position) ? pos : end_position;
        if (pos1 == pos2)
            continue;

        Suppression s;
        std::istringstream iss(comment.substr(pos1, pos2-pos1));

        iss >> s.errorId;
        if (!iss) {
            if (errorMessage && errorMessage->empty())
                *errorMessage = "Bad multi suppression '" + comment + "'. legal format is cppcheck-suppress[errorId, errorId symbolName=arr, ...]";
            suppressions.clear();
            return suppressions;
        }

        const std::string symbolNameString = "symbolName=";

        while (iss) {
            std::string word;
            iss >> word;
            if (!iss)
                break;
            if (word.find_first_not_of("+-*/%#;") == std::string::npos)
                break;
            if (startsWith(word, symbolNameString)) {
                s.symbolName = word.substr(symbolNameString.size());
            } else {
                if (errorMessage && errorMessage->empty())
                    *errorMessage = "Bad multi suppression '" + comment + "'. legal format is cppcheck-suppress[errorId, errorId symbolName=arr, ...]";
                suppressions.clear();
                return suppressions;
            }
        }

        suppressions.push_back(std::move(s));
    }

    const std::string extraComment = getExtraComment(comment, end_position);

    if (extraComment.empty())
        return suppressions;

    for (auto &suppression : suppressions)
        suppression.extraComment = extraComment;

    return suppressions;
}

SuppressionList::Suppression SuppressionList::parseLine(std::string line)
{
    SuppressionList::Suppression suppression;

    // Strip any end of line comments
    std::string::size_type endpos = std::min(line.find('#'), line.find("//"));
    if (endpos != std::string::npos) {
        const std::string::size_type offset = line[endpos] == '#' ? 1 : 2;
        suppression.extraComment = trim(line.substr(endpos + offset));
        while (endpos > 0 && std::isspace(line[endpos-1])) {
            endpos--;
        }
        line.resize(endpos);
    }

    const auto parts = splitString(line, '\n');
    const std::string& suppr_l = parts[0];

    const std::string::size_type first_sep = suppr_l.find(':');
    suppression.errorId = suppr_l.substr(0, first_sep);
    if (first_sep != std::string::npos) {
        suppression.fileName = suppr_l.substr(first_sep+1);
        if (!suppression.fileName.empty()) {
            // TODO: this only works with files which have an extension
            // If there is not a dot after the last colon in "file" then
            // the colon is a separator and the contents after the colon
            // is a line number..

            // Get position of last colon
            const std::string::size_type pos = suppression.fileName.rfind(':');

            // if a colon is found and there is no dot after it..
            if (pos != std::string::npos &&
                suppression.fileName.find('.', pos) == std::string::npos)
            {
                // parse out the line number
                const std::string line_s = suppression.fileName.substr(pos+1);
                suppression.fileName.erase(pos);

                if (suppression.fileName.empty())
                    throw std::runtime_error("filename is missing");

                try {
                    suppression.lineNumber = strToInt<int>(line_s);
                } catch (const std::runtime_error& e) {
                    throw std::runtime_error(std::string("invalid line number (") + e.what() + ")");
                }
            }
            suppression.fileName = Path::simplifyPath(suppression.fileName);
        } else {
            throw std::runtime_error("filename is missing");
        }
    }

    // TODO: make this optional - do we even encounter this in production code?
    // when parsing string generated internally by toString() there can be newline
    for (std::size_t i = 1; i < parts.size(); ++i) {
        if (startsWith(parts[i], "symbol="))
            suppression.symbolName = parts[i].substr(7);
        else if (parts[i] == "polyspace=1")
            suppression.isPolyspace = true;
        else
            throw std::runtime_error("unexpected extra '" + parts[i] + "'");
    }

    return suppression;
}

std::string SuppressionList::addSuppressionLine(const std::string &line)
{
    try {
        return addSuppression(parseLine(line));
    } catch (const std::exception& e) {
        return e.what();
    }
}

SuppressionList::Suppression *SuppressionList::findSameParameters(const Suppression &suppression)
{
    const std::vector<Suppression*> *candidates = &mNonHashSuppressions;
    if (suppression.hash > 0) {
        const auto it = mHashSuppressions.find(suppression.hash);
        if (it == mHashSuppressions.end())
            return nullptr;
        candidates = &it->second;
    }
    const auto found = std::find_if(candidates->cbegin(), candidates->cend(), [&](const Suppression *s) {
        return suppression.isSameParameters(*s);
    });
    return found != candidates->cend() ? *found : nullptr;
}

std::string SuppressionList::addSuppression(SuppressionList::Suppression suppression)
{
    std::lock_guard<std::mutex> lg(mSuppressionsSync);

    // Check if suppression is already in list
    if (findSameParameters(suppression)) {
        return "suppression '" + suppression.toString() + "' already exists";
    }

    // Check that errorId is valid..
    if (suppression.errorId.empty() && suppression.hash == 0)
        return "Failed to add suppression. No id.";

    for (std::string::size_type pos = 0; pos < suppression.errorId.length(); ++pos) {
        if (!isAcceptedErrorIdChar(suppression.errorId[pos])) {
            return "Failed to add suppression. Invalid id \"" + suppression.errorId + "\"";
        }
        if (pos == 0 && std::isdigit(suppression.errorId[pos])) {
            return "Failed to add suppression. Invalid id \"" + suppression.errorId + "\"";
        }
    }

    if (!isValidGlobPattern(suppression.errorId))
        return "Failed to add suppression. Invalid glob pattern '" + suppression.errorId + "'.";
    if (!isValidGlobPattern(suppression.fileName))
        return "Failed to add suppression. Invalid glob pattern '" + suppression.fileName + "'.";

    mSuppressions.push_back(std::move(suppression));

    Suppression *const added = &mSuppressions.back();
    if (added->hash > 0)
        mHashSuppressions[added->hash].push_back(added);
    else
        mNonHashSuppressions.push_back(added);

    return "";
}

std::string SuppressionList::addSuppressions(std::list<Suppression> suppressions)
{
    for (auto &newSuppression : suppressions) {
        auto errmsg = addSuppression(std::move(newSuppression));
        if (!errmsg.empty())
            return errmsg;
    }
    return "";
}

bool SuppressionList::updateSuppressionState(const SuppressionList::Suppression& suppression)
{
    std::lock_guard<std::mutex> lg(mSuppressionsSync);

    // Check if suppression is already in list
    Suppression *const foundSuppression = findSameParameters(suppression);
    if (foundSuppression) {
        if (suppression.checked)
            foundSuppression->checked = true;
        if (suppression.matched)
            foundSuppression->matched = true;
        return true;
    }

    return false;
}

void SuppressionList::ErrorMessage::setFileName(std::string s)
{
    mFileName = Path::simplifyPath(std::move(s));
}

bool SuppressionList::Suppression::parseComment(std::string comment, std::string *errorMessage)
{
    if (comment.size() < 2)
        return false;

    if (comment.compare(comment.size() - 2, 2, "*/") == 0)
        comment.erase(comment.size() - 2, 2);

    std::string::size_type extraPos;
    extraComment = getExtraComment(comment, 2, &extraPos);

    if (!extraComment.empty())
        comment.erase(extraPos);

    const std::set<std::string> cppchecksuppress{
        "cppcheck-suppress",
        "cppcheck-suppress-begin",
        "cppcheck-suppress-end",
        "cppcheck-suppress-file",
        "cppcheck-suppress-macro"
    };

    std::istringstream iss(comment.substr(2));
    std::string word;
    iss >> word;
    if (!cppchecksuppress.count(word))
        return false;

    iss >> errorId;
    if (!iss)
        return false;

    const std::string symbolNameString = "symbolName=";

    while (iss) {
        iss >> word;
        if (!iss)
            break;
        if (word.find_first_not_of("+-*/%#;") == std::string::npos)
            break;
        if (startsWith(word, symbolNameString))
            symbolName = word.substr(symbolNameString.size());
        else if (errorMessage && errorMessage->empty())
            *errorMessage = "Bad suppression attribute '" + word + "'. You can write comments in the comment after a ; or //. Valid suppression attributes; symbolName=sym";
    }
    return true;
}

SuppressionList::Suppression::Result SuppressionList::Suppression::isSuppressed(const SuppressionList::ErrorMessage &errmsg) const
{
    if (type == SuppressionList::Type::macro) {
        if (errmsg.macroNames.count(macroName) == 0)
            return Result::None;
        if (hash > 0 && hash != errmsg.hash)
            return Result::Checked;
        if (!errorId.empty() && !matchglob(errorId, errmsg.errorId))
            return Result::Checked;
    } else {
        if ((SuppressionList::Type::unique == type) && (lineNumber != NO_LINE) && (lineNumber != errmsg.lineNumber)) {
            if (!thisAndNextLine || lineNumber + 1 != errmsg.lineNumber)
                return Result::None;
        }
        if (!fileName.empty() && !isFileNameMatch(errmsg.getFileName()))
            return Result::None;
        if (hash > 0 && hash != errmsg.hash)
            return Result::Checked;
        // the empty check is a hack to allow wildcard suppressions on IDs to be marked as checked
        if (!errorId.empty() && (errmsg.errorId.empty() || !matchglob(errorId, errmsg.errorId)))
            return Result::Checked;
        if ((SuppressionList::Type::block == type) && ((errmsg.lineNumber < lineBegin) || (errmsg.lineNumber > lineEnd)))
            return Result::Checked;
    }
    if (!symbolName.empty()) {
        bool matchedSymbol = false;
        for (std::string::size_type pos = 0; pos < errmsg.symbolNames.size();) {
            const std::string::size_type pos2 = errmsg.symbolNames.find('\n',pos);
            std::string symname;
            if (pos2 == std::string::npos) {
                symname = errmsg.symbolNames.substr(pos);
                pos = pos2;
            } else {
                symname = errmsg.symbolNames.substr(pos,pos2-pos);
                pos = pos2+1;
            }
            if (matchglob(symbolName, symname)) {
                matchedSymbol = true;
                break;
            }
        }
        if (!matchedSymbol)
            return Result::Checked;
    }
    return Result::Matched;
}

bool SuppressionList::Suppression::isFileNameMatch(const std::string &errorFileName) const
{
    const auto it = mFileNameMatchCache.find(errorFileName);
    if (it != mFileNameMatchCache.end())
        return it->second;

    const bool result = PathMatch::match(fileName, errorFileName);

    if (mFileNameMatchCache.size() >= mFileNameMatchCacheMaxEntries)
        mFileNameMatchCache.clear();

    mFileNameMatchCache.emplace(errorFileName, result);
    return result;
}

bool SuppressionList::Suppression::isMatch(const SuppressionList::ErrorMessage &errmsg)
{
    switch (isSuppressed(errmsg)) {
    case Result::None:
        return false;
    case Result::Checked:
        checked = true;
        return false;
    case Result::Matched:
        checked = true;
        matched = true;
        return true;
    }
    cppcheck::unreachable();
}

bool SuppressionList::isSuppressed(const SuppressionList::ErrorMessage &errmsg, bool global)
{
    std::lock_guard<std::mutex> lg(mSuppressionsSync);

    // TODO: handle unmatchedPolyspaceSuppression?
    const bool unmatchedSuppression(errmsg.errorId == "unmatchedSuppression");
    bool returnValue = false;
    const auto match = [&](const std::vector<Suppression*> &suppressions) {
        for (Suppression *s : suppressions) {
            if (!global && !s->isLocal())
                continue;
            if (unmatchedSuppression && s->errorId != errmsg.errorId)
                continue;
            if (s->isMatch(errmsg))
                returnValue = true;
        }
    };
    match(mNonHashSuppressions);
    if (errmsg.hash != 0) {
        const auto it = mHashSuppressions.find(errmsg.hash);
        if (it != mHashSuppressions.end())
            match(it->second);
    }
    return returnValue;
}

bool SuppressionList::isSuppressedExplicitly(const SuppressionList::ErrorMessage &errmsg, bool global)
{
    std::lock_guard<std::mutex> lg(mSuppressionsSync);

    const auto match = [&](const std::vector<Suppression*> &suppressions) {
        return std::any_of(suppressions.cbegin(), suppressions.cend(), [&](Suppression *s) {
            if (!global && !s->isLocal())
                return false;
            if (s->errorId != errmsg.errorId) // Error id must match exactly
                return false;
            return s->isMatch(errmsg);
        });
    };
    if (match(mNonHashSuppressions))
        return true;
    if (errmsg.hash != 0) {
        const auto it = mHashSuppressions.find(errmsg.hash);
        if (it != mHashSuppressions.end())
            return match(it->second);
    }
    return false;
}

bool SuppressionList::isSuppressed(const ::ErrorMessage &errmsg, const std::set<std::string>& macroNames)
{
    {
        std::lock_guard<std::mutex> lg(mSuppressionsSync);

        if (mSuppressions.empty())
            return false;
    }
    return isSuppressed(SuppressionList::ErrorMessage::fromErrorMessage(errmsg, macroNames));
}

void SuppressionList::dump(std::ostream & out, const std::string& filePath) const
{
    std::lock_guard<std::mutex> lg(mSuppressionsSync);

    cppcheck::schemas::DumpSuppressionsSchema schema;
    for (const Suppression &suppression : mSuppressions) {
        if (suppression.isInline && !suppression.fileName.empty() && !filePath.empty() && filePath != suppression.fileName)
            continue;
        cppcheck::schemas::DumpSuppressionSchema entry;
        entry.errorId = suppression.errorId;
        if (!suppression.fileName.empty())
            entry.fileName = suppression.fileName;
        if (suppression.lineNumber != Suppression::NO_LINE)
            entry.lineNumber = suppression.lineNumber;
        if (suppression.thisAndNextLine)
            entry.thisAndNextLine = true;
        if (!suppression.symbolName.empty())
            entry.symbolName = suppression.symbolName;
        if (suppression.hash > 0)
            entry.hash = suppression.hash;
        if (suppression.lineBegin != Suppression::NO_LINE)
            entry.lineBegin = suppression.lineBegin;
        if (suppression.lineEnd != Suppression::NO_LINE)
            entry.lineEnd = suppression.lineEnd;
        if (suppression.type == SuppressionList::Type::file)
            entry.type = std::string("file");
        else if (suppression.type == SuppressionList::Type::block)
            entry.type = std::string("block");
        else if (suppression.type == SuppressionList::Type::blockBegin)
            entry.type = std::string("blockBegin");
        else if (suppression.type == SuppressionList::Type::blockEnd)
            entry.type = std::string("blockEnd");
        else if (suppression.type == SuppressionList::Type::macro)
            entry.type = std::string("macro");
        entry.is_inline = suppression.isInline;
        if (!suppression.extraComment.empty())
            entry.comment = suppression.extraComment;
        schema.suppression.push_back(std::move(entry));
    }
    out << cppcheck::schemas::writeDumpSuppressionsXml(schema);
}

std::list<SuppressionList::Suppression> SuppressionList::getUnmatchedLocalSuppressions(const FileWithDetails &file) const
{
    std::lock_guard<std::mutex> lg(mSuppressionsSync);

    std::list<Suppression> result;
    for (const Suppression &s : mSuppressions) {
        if (s.isInline)
            continue;
        if (s.matched)
            continue;
        if ((s.lineNumber != Suppression::NO_LINE) && !s.checked)
            continue;
        if (s.type == SuppressionList::Type::macro)
            continue;
        if (s.hash > 0)
            continue;
        if (s.errorId == ID_CHECKERSREPORT)
            continue;
        if (!s.isLocal() || !PathMatch::match(s.fileName, file.spath()))
            continue;
        result.push_back(s);
    }
    return result;
}

std::list<SuppressionList::Suppression> SuppressionList::getUnmatchedGlobalSuppressions() const
{
    std::lock_guard<std::mutex> lg(mSuppressionsSync);

    std::list<Suppression> result;
    for (const Suppression &s : mSuppressions) {
        if (s.isInline)
            continue;
        if (s.matched)
            continue;
        if (!s.checked && s.isWildcard())
            continue;
        if (s.hash > 0)
            continue;
        if (s.errorId == ID_CHECKERSREPORT)
            continue;
        if (s.isLocal())
            continue;
        result.push_back(s);
    }
    return result;
}

std::list<SuppressionList::Suppression> SuppressionList::getUnmatchedInlineSuppressions() const
{
    std::list<SuppressionList::Suppression> result;
    for (const SuppressionList::Suppression &s : SuppressionList::mSuppressions) {
        if (!s.isInline)
            continue;
        // TODO: remove this and markUnmatchedInlineSuppressionsAsChecked()?
        if (!s.checked)
            continue;
        if (s.matched)
            continue;
        if (s.hash > 0)
            continue;
        result.push_back(s);
    }
    return result;
}

std::list<SuppressionList::Suppression> SuppressionList::getSuppressions() const
{
    std::lock_guard<std::mutex> lg(mSuppressionsSync);

    return mSuppressions;
}

void SuppressionList::markUnmatchedInlineSuppressionsAsChecked(const TokenList &tokenlist) {
    std::lock_guard<std::mutex> lg(mSuppressionsSync);

    int currLineNr = -1;
    int currFileIdx = -1;
    for (const Token *tok = tokenlist.front(); tok; tok = tok->next()) {
        if (currFileIdx != tok->fileIndex() || currLineNr != tok->linenr()) {
            currLineNr = tok->linenr();
            currFileIdx = tok->fileIndex();
            // checked is not used for hash suppressions
            for (Suppression *s : mNonHashSuppressions) {
                Suppression &suppression = *s;
                if (suppression.type == SuppressionList::Type::unique) {
                    if (!suppression.checked && (suppression.lineNumber == currLineNr) && (suppression.fileName == tokenlist.file(tok))) {
                        suppression.checked = true;
                    }
                } else if (suppression.type == SuppressionList::Type::block) {
                    if ((!suppression.checked && (suppression.lineBegin <= currLineNr) && (suppression.lineEnd >= currLineNr) && (suppression.fileName == tokenlist.file(tok)))) {
                        suppression.checked = true;
                    }
                } else if (!suppression.checked && suppression.fileName == tokenlist.file(tok)) {
                    suppression.checked = true;
                }
            }
        }
    }
}

std::string SuppressionList::Suppression::toString() const
{
    std::string s;
    s += errorId;
    if (!fileName.empty()) {
        s += ':';
        s += fileName;
        if (lineNumber != -1) {
            s += ':';
            s += std::to_string(lineNumber);
        }
    }
    if (!symbolName.empty())
        s += "\nsymbol=" + symbolName;
    if (isPolyspace)
        s += "\npolyspace=1";
    return s;
}

polyspace::Parser::Parser(const Settings &settings)
{
    const bool haveMisraAddon = std::any_of(settings.addonInfos.cbegin(),
                                            settings.addonInfos.cend(),
                                            [] (const AddonInfo &info) {
        return info.name == "misra";
    });

    if (haveMisraAddon) {
        mFamilyMap["MISRA-C3"] = "misra-c2012-";
        mFamilyMap["MISRA2012"] = "misra-c2012-";
    }

    const auto matchArg = [&](const std::string &arg) {
        const std::string args = settings.premiumArgs;
        const std::string::size_type pos = args.find(arg);

        if (pos == std::string::npos)
            return false;

        const char prevChar = (pos > 0) ? args[pos - 1] : ' ';
        const char nextChar = (pos + arg.size() < args.size()) ? args[pos + arg.size()] : ' ';

        return prevChar == ' ' && (nextChar == ' ' || nextChar == ':');
    };

    if (matchArg("--misra-c-2012")) {
        mFamilyMap["MISRA-C3"] = "premium-misra-c-2012-";
        mFamilyMap["MISRA2012"] = "premium-misra-c-2012-";
    }

    if (matchArg("--misra-c-2023"))
        mFamilyMap["MISRA-C-2023"] = "premium-misra-c-2023-";

    if (matchArg("--misra-cpp-2008") || matchArg("--misra-c++-2008"))
        mFamilyMap["MISRA-CPP"] = "premium-misra-cpp-2008-";

    if (matchArg("--misra-cpp-2023") || matchArg("--misra-c++-2023"))
        mFamilyMap["MISRA-CPP-2023"] = "premium-misra-cpp-2023-";

    if (matchArg("--cert-c") || matchArg("--cert-c-2016"))
        mFamilyMap["CERT-C"] = "premium-cert-c-";

    if (matchArg("--cert-cpp") || matchArg("--cert-c++") ||
        matchArg("--cert-cpp-2016") || matchArg("--cert-c++-2016"))
        mFamilyMap["CERT-CPP"] = "premium-cert-cpp-";

    if (matchArg("--autosar"))
        mFamilyMap["AUTOSAR-CPP14"] = "premium-autosar-";
}

polyspace::CommentKind polyspace::Parser::parseKind(const std::string& comment, std::string::size_type& pos)
{
    const std::string::size_type pos1 = pos;
    pos = comment.find_first_of(" \t", pos);
    if (pos >= comment.size())
        return CommentKind::Invalid;

    const std::string token = comment.substr(pos1, pos-pos1);

    if (token == "polyspace")
        return CommentKind::Regular;

    if (token == "polyspace-begin")
        return CommentKind::Begin;

    if (token == "polyspace-end")
        return CommentKind::End;

    return CommentKind::Invalid;
}


std::list<SuppressionList::Suppression> polyspace::Parser::parse(const std::string &comment, int line, const std::string &filename) const
{
    // Syntax for a polyspace suppression:
    // https://se.mathworks.com/help/bugfinder/ug/annotate-hide-known-acceptable-polyspace-results-web-browser.html

    std::list<SuppressionList::Suppression> ret;

    if (mFamilyMap.empty())
        return ret;

    for (std::string::size_type pos = comment.find_first_not_of("/* "); pos < comment.size();) {
        // polyspace
        const auto polyspaceKind = parseKind(comment, pos);
        if (polyspaceKind == CommentKind::Invalid)
            break;

        // optional range
        const int rangeValue = parseRange(comment, pos);

        // ids..
        const std::set<std::string> ids = parseIds(comment, pos);

        // skip justification
        if (pos < comment.size() && comment[pos] == '[') {
            pos = comment.find(']',pos+1);
            if (pos >= comment.size())
                break;
            pos = comment.find_first_not_of(" \t", pos+1);
            if (pos >= comment.size())
                break;
        }

        // extra comment
        std::string extraComment;
        if (pos < comment.size() && comment[pos] == '\"') {
            const std::string::size_type p1 = pos + 1;
            pos = comment.find('\"',p1);
            if (pos >= comment.size())
                break;
            extraComment = comment.substr(p1, pos-p1);
        }

        for (const std::string& errorId: ids) {
            SuppressionList::Suppression suppr;
            suppr.errorId = errorId;
            suppr.isInline = true;
            suppr.isPolyspace = true;
            suppr.fileName = filename;
            suppr.lineNumber = line;
            suppr.extraComment = extraComment;

            if (rangeValue > 0) {
                suppr.type = SuppressionList::Type::block;
                suppr.lineBegin = line;
                suppr.lineEnd = line + rangeValue;
            }
            else if (polyspaceKind == polyspace::CommentKind::Regular)
                suppr.type = SuppressionList::Type::unique;
            else if (polyspaceKind == polyspace::CommentKind::Begin)
                suppr.type = SuppressionList::Type::blockBegin;
            else
                suppr.type = SuppressionList::Type::blockEnd;

            ret.emplace_back(suppr);
        }

        // proceed to next "polyspace" if it exists
        if (pos < comment.size())
            pos = comment.find("polyspace", pos);
    }

    return ret;
}


int polyspace::Parser::parseRange(const std::string& comment, std::string::size_type& pos) {
    pos = comment.find_first_not_of(" \t", pos);
    if (pos >= comment.size())
        return 0;
    if (comment[pos] != '+')
        return 0;
    const std::string::size_type startpos = pos + 1;
    std::string::size_type endpos = comment.find_first_of(" \t", startpos);
    if (endpos > comment.size())
        return 0;
    const std::string range = comment.substr(startpos, endpos-startpos);
    try {
        int ret = std::stoi(range);
        pos = endpos;
        return ret;
    } catch (const std::invalid_argument &) {}
    return 0;
}

std::vector<std::pair<std::string, std::string>> polyspace::Parser::parseFamilyRules(const std::string& comment, std::string::size_type& pos) {
    std::vector<std::pair<std::string, std::string>> fr;
    std::string family;
    std::string rule;
    enum class State: uint8_t { family, colon, rule, rule_or_family } state = State::family;
    const std::string::size_type endpos = startsWith(comment, "/*") ? comment.size() - 2 : comment.size();
    for (; pos <= endpos; ++pos) {
        const char c = comment[pos];
        if (std::strchr("[\"", c))
            break;
        switch (state) {
        case State::family:
            if (std::isalnum(c) || std::strchr("-_.",c))
                family += c;
            else if (!family.empty() && std::strchr(" \t:",c))
                state = State::colon;
            break;
        case State::colon:
            if (!std::strchr(" \t:", c)) {
                rule.clear();
                --pos;
                state = State::rule;
            }
            break;
        case State::rule:
            if (std::strchr(", \t",c)) {
                if (!rule.empty()) {
                    fr.emplace_back(family,rule);
                    rule.clear();
                    if (c != ',')
                        state = State::rule_or_family;
                }
            }
            else
                rule += c;
            break;
        case State::rule_or_family:
            rule.clear();
            if (std::isalnum(c)) {
                --pos;
                family.clear();
                state = State::family;
            } else if (c == ',') {
                --pos;
                state = State::rule;
            }
            break;
        }
    }
    if (!family.empty() && !rule.empty())
        fr.emplace_back(family,rule);
    return fr;
}

std::set<std::string> polyspace::Parser::parseIds(const std::string& comment, std::string::size_type& pos) const {
    std::set<std::string> ids;
    for (const auto& fr: parseFamilyRules(comment,pos)) {
        const auto it = mFamilyMap.find(fr.first);
        if (it != mFamilyMap.cend())
            ids.emplace(it->second + fr.second);
    }
    return ids;
}


bool polyspace::isPolyspaceComment(const std::string &comment)
{
    const std::string polyspace = "polyspace";
    const std::string::size_type pos = comment.find_first_not_of("/* ");
    if (pos == std::string::npos)
        return false;
    return comment.compare(pos, polyspace.size(), polyspace, 0, polyspace.size()) == 0;
}
