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

#include "cmdlineparser.h"

#include "addoninfo.h"
#include "check.h"
#include "checks.h"
#include "checkers.h"
#include "color.h"
#include "config.h"
#include "cppcheck.h"
#include "errorlogger.h"
#include "errortypes.h"
#include "filelister.h"
#include "filesettings.h"
#include "importproject.h"
#include "library.h"
#include "path.h"
#include "pathmatch.h"
#include "platform.h"
#include "settings.h"
#include "standards.h"
#include "suppressions.h"
#include "utils.h"

#include "frontend.h"

#include <rfl/Rename.hpp>
#include <rfl/cli.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib> // EXIT_FAILURE
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <list>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef HAVE_RULES
#include "regex.h"
#include "rule.h"

// xml is used for rules
#include "xml.h"

#include <memory>
#endif

static bool addFilesToList(const std::string& fileList, std::vector<std::string>& pathNames)
{
    std::istream *files;
    std::ifstream infile;
    if (fileList == "-") { // read from stdin
        files = &std::cin;
    } else {
        infile.open(fileList);
        if (!infile.is_open())
            return false;
        files = &infile;
    }
    std::string fileName;
    while (std::getline(*files, fileName)) { // next line
        if (!fileName.empty()) {
            pathNames.emplace_back(std::move(fileName));
        }
    }

    return true;
}

static bool addIncludePathsToList(const std::string& fileList, std::list<std::string>& pathNames)
{
    std::ifstream files(fileList);
    if (files) {
        std::string pathName;
        while (std::getline(files, pathName)) { // next line
            if (!pathName.empty()) {
                pathName = Path::removeQuotationMarks(std::move(pathName));
                pathName = Path::fromNativeSeparators(std::move(pathName));

                // If path doesn't end with / or \, add it
                if (!endsWith(pathName, '/'))
                    pathName += '/';

                pathNames.emplace_back(std::move(pathName));
            }
        }
        return true;
    }
    return false;
}

static bool addPathsToSet(const std::string& fileName, std::set<std::string>& set)
{
    std::list<std::string> templist;
    if (!addIncludePathsToList(fileName, templist))
        return false;
    set.insert(templist.cbegin(), templist.cend());
    return true;
}

namespace {
    using rfl::cli::Opt;
    using namespace rfl::cli::attr;

    using CppcheckCliProcessors = rfl::Processors<
        rfl::DefaultIfMissing,
        rfl::NoExtraFields,
        rfl::cli::PureFlags,
        rfl::cli::LastValueWins,
        rfl::cli::RequireEquals,
        rfl::cli::ListDelimiter<"">>;

    struct CppcheckArgs {
        Opt<std::vector<std::string>, ValueName<"addon">,
            Desc<"Execute addon, e.g. --addon=misra. If options must be provided a json configuration is needed.">>
            addon{};
        Opt<std::optional<std::string>, ValueName<"python interpreter">,
            Desc<"Python interpreter for addons (default tries \"python3\", then \"python\").">>
            addon_python{};
        Opt<bool, Hidden> analyze_all_vs_configs{false};
        Opt<bool, Short<"CC">,
            Desc<"Do not remove comments during preprocessing (requires -E).">>
            keep_comments{false};
        Opt<std::optional<std::string>, ValueName<"dir">,
            Desc<"Cppcheck work folder for whole program analysis and cached results.">>
            cppcheck_build_dir{};
        Opt<bool, Desc<"Check cppcheck configuration. Normal code analysis is disabled.">>
            check_config{false};
        Opt<bool, Hidden> check_headers{false};
        Opt<std::optional<std::string>, ValueName<"level">, DefaultHint<"normal">,
            Desc<"Configure valueflow analysis depth: reduced, normal, or exhaustive.">>
            check_level{};
        Opt<bool, Desc<"Show information messages when library files have incomplete info.">>
            check_library{false};
        Opt<bool, Hidden> check_unused_templates{false};
        Opt<std::optional<std::string>, Hidden> check_version{};
        Opt<std::optional<std::string>, ValueName<"file">,
            Desc<"Write a report of all active checkers to the given file.">>
            checkers_report{};
        Opt<std::optional<std::string>, Hidden> checks_max_time{};
        Opt<std::optional<std::string>, Implicit<"clang">, ValueName<"path">,
            Deprecated<"Use Clang parser instead of the builtin Cppcheck parser (will be removed in Cppcheck 2.27).">>
            clang{};
        Opt<std::optional<std::string>, Implicit<"clang-tidy">, Hidden> clang_tidy{};
        Opt<std::vector<std::string>, ValueName<"dir">,
            Desc<"Path prefix to exclude from configuration checking in headers.">>
            config_exclude{};
        Opt<std::vector<std::string>, ValueName<"file">,
            Desc<"A file containing a list of config-excludes.">>
            config_excludes_file{};
        Opt<bool, Hidden> cpp_header_probe{false};
        Opt<bool, Hidden> debug{false};
        Opt<bool, Hidden> debug_analyzerinfo{false};
        Opt<bool, Hidden> debug_ast{false};
        Opt<bool, Hidden> debug_clang_output{false};
        Opt<bool, Hidden> debug_ignore{false};
        Opt<bool, Hidden> debug_ipc{false};
        Opt<std::optional<std::string>, Implicit<"all">, Hidden> debug_lookup{};
        Opt<bool, Hidden> debug_normal{false};
        Opt<bool, Hidden> debug_simplified{false};
        Opt<bool, Hidden> debug_symdb{false};
        Opt<bool, Hidden> debug_template{false};
        Opt<bool, Hidden> debug_valueflow{false};
        Opt<bool, Hidden> debug_warnings{false};
        Opt<std::vector<std::string>, ValueName<"severity">,
            Desc<"Disable checks with the given severity (see --enable).">>
            disable{};
        Opt<bool, Hidden> doc{false};
        Opt<bool, Desc<"Dump xml data (.dump) for each translation unit.">>
            dump{false};
        Opt<std::vector<std::string>, Short<"D">, ValueName<"ID">,
            Desc<"Define preprocessor symbol (e.g. -DDEBUG=1 -D__cplusplus).">>
            define{};
        Opt<bool, Short<"E">,
            Desc<"Print preprocessor output on stdout and don't do any further processing.">>
            preprocess_only{false};
        Opt<bool, Hidden> emit_duplicates{false};
        Opt<std::vector<std::string>, ValueName<"severity">,
            Desc<"Enable additional checks (comma-separated: warning, performance, portability, information, style, unusedFunction, missingInclude, all).">>
            enable{};
        Opt<std::optional<std::string>, ValueName<"n">, DefaultHint<"0">,
            Desc<"Exit code returned if errors are found.">>
            error_exitcode{};
        Opt<bool, Desc<"Print a list of all error messages in XML format.">>
            errorlist{false};
        Opt<std::optional<std::string>, Implicit<"stdout">, Hidden> exception_handling{};
        Opt<std::optional<std::string>, Hidden> executor{};
        Opt<std::vector<std::string>, ValueName<"spec">,
            Desc<"Error ID which should not result in a non-zero exitcode.">>
            exitcode_suppress{};
        Opt<std::vector<std::string>, ValueName<"file">,
            Desc<"File of error IDs that should not cause a non-zero exitcode.">>
            exitcode_suppressions{};
        Opt<std::vector<std::string>, ValueName<"str">,
            Desc<"Analyze only files matching filter ('-' reads from stdin, '+' uses CLI files as filters).">>
            file_filter{};
        Opt<std::vector<std::string>, ValueName<"file">,
            Desc<"Text file listing files to check, one per line ('-' reads from stdin).">>
            file_list{};
        Opt<bool, Hidden> filesdir{false};
        Opt<bool, Short<"f">,
            Desc<"Force checking of all configurations in files (overrides --max-configs).">>
            force{false};
        Opt<bool, Desc<"Treat char type as signed.">> fsigned_char{false};
        Opt<bool, Desc<"Treat char type as unsigned.">> funsigned_char{false};
        Opt<bool, Short<"h">, Desc<"Print this help.">> help{false};
        Opt<std::vector<std::string>, Short<"I">, ValueName<"dir">,
            Desc<"Directory to search for include files.">>
            include_dir{};
        Opt<std::vector<std::string>, ValueName<"file">,
            Desc<"Text file listing include directories, one per line.">>
            includes_file{};
        Opt<std::vector<std::string>, ValueName<"file">,
            Desc<"Force inclusion of a file before the checked file.">>
            include{};
        Opt<std::vector<std::string>, Short<"i">, ValueName<"str">,
            Desc<"Ignore files or directories matching glob pattern.">>
            ignore{};
        Opt<bool, Desc<"Allow reporting inconclusive analysis results.">>
            inconclusive{false};
        Opt<bool, Desc<"Enable inline suppressions (e.g. '// cppcheck-suppress warningId').">>
            inline_suppr{false};
        Opt<std::optional<std::string>, Short<"j">, ValueName<"jobs">,
            Desc<"Start <jobs> threads to do the checking simultaneously.">>
            jobs{};
        Opt<std::optional<std::string>, Short<"l">, ValueName<"load">,
            Desc<"Do not start new threads if load average is at least <load>.">>
            load_average{};
        Opt<std::optional<std::string>, Short<"x">, ValueName<"language">,
            Desc<"Force checking all files as the given language (c or c++).">>
            language{};
        Opt<std::vector<std::string>, ValueName<"cfg">,
            Desc<"Load library configuration file <cfg> (std.cfg is loaded automatically).">>
            library{};
        Opt<std::optional<std::string>, ValueName<"limit">, DefaultHint<"12">,
            Desc<"Maximum number of #ifdef configurations to check in a file (overrides --force).">>
            max_configs{};
        Opt<std::optional<std::string>, ValueName<"N">, DefaultHint<"2">,
            Desc<"Max depth in whole program analysis.">>
            max_ctu_depth{};
        Opt<std::optional<std::string>, Hidden> max_template_recursion{};
        Opt<bool, Hidden> no_analyze_all_vs_configs{false};
        Opt<bool, Hidden> no_check_headers{false};
        Opt<bool, Hidden> no_check_unused_templates{false};
        Opt<bool, Hidden> no_cppcheck_build_dir{false};
        Opt<bool, Hidden> no_cpp_header_probe{false};
        Opt<bool, Hidden> no_safety{false};
        Opt<std::optional<std::string>, ValueName<"file">,
            Desc<"Write results to file rather than standard error.">>
            output_file{};
        Opt<std::optional<std::string>, ValueName<"format">,
            Desc<"Output format: text, sarif, xml (deprecated), xmlv2, or xmlv3.">>
            output_format{};
        Opt<std::optional<std::string>, Hidden> performance_valueflow_max_time{};
        Opt<std::optional<std::string>, Hidden> performance_valueflow_max_if_count{};
        Opt<std::optional<std::string>, Hidden> performance_valueflow_max_iterations{};
        Opt<std::optional<std::string>, ValueName<"type|file">,
            Desc<"Platform-specific types and sizes (unix32, unix64, win32A, win32W, win64, avr8, elbrus-e1cp, pic8, pic8-enhanced, pic16, mips32, native, unspecified, or XML file).">>
            platform{};
        Opt<std::optional<std::string>, ValueName<"path">,
            Desc<"Generate Clang-plist output files in folder.">>
            plist_output{};
        Opt<std::vector<std::string>, Group<"Premium">, ValueName<"option">,
            Desc<"Cppcheck Premium option (autosar, cert-c, cert-c++, misra-c-2012/2023/2025, misra-c++-2008/2023, bughunting, metrics, safety, safety-off; append :all for coding standards).">>
            premium{};
        Opt<std::optional<std::string>, Group<"Premium">, ValueName<"BITS">,
            Desc<"Integer precision to use in Cert C INT35-C analysis.">>
            premium_cert_c_int_precision{};
        Opt<std::optional<std::string>, Group<"Premium">, ValueName<"path">,
            Desc<"Provide Cppcheck Premium license file path.">>
            premium_license_file{};
        Opt<std::vector<std::string>, ValueName<"file">,
            Desc<"Run Cppcheck on project (*.sln, *.slnx, *.vcxproj, compile_commands.json, *.bpr).">>
            project{};
        Opt<std::optional<std::string>, ValueName<"config">,
            Desc<"Limit Visual Studio project configuration (e.g. 'Release|Win32').">>
            project_configuration{};
        Opt<bool, Short<"q">,
            Desc<"Do not show progress reports (not mutually exclusive with --verbose).">>
            quiet{false};
        Opt<std::vector<std::string>, Short<"rp">, Implicit<"">, ValueName<"paths">,
            Desc<"Use relative paths in output (separate multiple base paths by ';').">>
            relative_paths{};
        Opt<std::optional<std::string>, Implicit<"10">,
            Desc<"Report progress messages while checking a file (single job only).">>
            report_progress{};
        Opt<std::optional<std::string>, ValueName<"type">,
            Desc<"Add guideline/classification fields: normal, autosar, cert-c-2016, cert-cpp-2016, misra-c-2012, misra-c-2023, misra-c-2025, misra-cpp-2008, misra-cpp-2023.">>
            report_type{};
        Opt<std::vector<std::string>, ValueName<"rule">,
            Desc<"Match regular expression.">>
            rule{};
        Opt<std::vector<std::string>, ValueName<"file">,
            Desc<"Use given rule XML file.">>
            rule_file{};
        Opt<bool, Desc<"Enable safety-certified checking mode.">>
            safety{false};
        Opt<std::optional<std::string>, ValueName<"mode">, DefaultHint<"none">,
            Desc<"Show timing information: none, file, file-total, summary, top5_file, top5_summary.">>
            showtime{};
        Opt<std::vector<std::string>, ValueName<"id">,
            Desc<"Set language standard: c89, c99, c11, c++03, c++11, c++14, c++17, c++20.">>
            std{};
        Opt<std::vector<std::string>, ValueName<"spec">,
            Desc<"Suppress warnings matching [error id]:[filename]:[line].">>
            suppress{};
        Opt<std::vector<std::string>, ValueName<"file">,
            Desc<"Suppress warnings listed in the given file.">>
            suppressions_list{};
        Opt<std::vector<std::string>, ValueName<"file">,
            Desc<"Suppress warnings listed in an XML file.">>
            suppress_xml{};
        rfl::Rename<"template",
            Opt<std::optional<std::string>, ValueName<"'<text>'">,
                Desc<"Format error messages (pre-defined: gcc, cppcheck1, vs, edit, simple).">>>
            template_format{};
        Opt<std::optional<std::string>, ValueName<"'<text>'">,
            Desc<"Format error message location.">>
            template_location{};
        Opt<std::optional<std::string>, Hidden> template_max_time{};
        Opt<std::optional<std::string>, Hidden> typedef_max_time{};
        Opt<std::vector<std::string>, Short<"U">, ValueName<"ID">,
            Desc<"Undefine preprocessor symbol (e.g. -UDEBUG).">>
            undefine{};
        Opt<std::optional<std::string>, Hidden> valueflow_max_iterations{};
        Opt<bool, Short<"v">,
            Desc<"Output more detailed error information (not mutually exclusive with --quiet).">>
            verbose{false};
        Opt<bool, Desc<"Print out version number.">>
            version{false};
        Opt<bool, Desc<"Write results in xml format to error stream (stderr).">>
            xml{false};
        Opt<std::optional<std::string>, Hidden> xml_version{};
        Opt<std::vector<std::string>, Positional> files{};
    };

    template<typename T>
    bool parseNumberValue(CmdLineLogger& logger, const std::string& optPrefix, const std::string& val, T& num, bool mustBePositive = false)
    {
        T tmp;
        std::string err;
        if (!strToInt(val, tmp, &err)) {
            logger.printError("argument to '" + optPrefix + "' is not valid - " + err + ".");
            return false;
        }
        if (mustBePositive && tmp < 0) {
            logger.printError("argument to '" + optPrefix + "' needs to be a positive integer.");
            return false;
        }
        num = tmp;
        return true;
    }

    std::string findRawArg(int argc, const char* const argv[], const std::string_view prefix)
    {
        for (int i = 1; i < argc; ++i) {
            const std::string_view a(argv[i]);
            if (a == prefix || (a.size() > prefix.size() && a.starts_with(prefix) && a[prefix.size()] == '='))
                return std::string(a);
        }
        return std::string(prefix);
    }

    void reportCliParseError(CmdLineLogger& logger, const std::string& err, int argc, const char* const argv[], bool isPremium)
    {
        constexpr std::string_view missingPrefix = "Missing value for option '";
        constexpr std::string_view unknownLongPrefix = "Unknown option '";
        constexpr std::string_view unknownShortPrefix = "Unknown short argument: ";

        if (err.starts_with(missingPrefix)) {
            const std::size_t endQuote = err.find('\'', missingPrefix.size());
            if (endQuote != std::string::npos) {
                const std::string opt = err.substr(missingPrefix.size(), endQuote - missingPrefix.size());
                if (opt == "-x") {
                    logger.printError("no language given to '-x' option.");
                    return;
                }
                if (opt.size() == 2 && opt[0] == '-') {
                    logger.printError("argument to '" + opt + "' is missing.");
                    return;
                }
                logger.printError("unrecognized command line option: \"" + findRawArg(argc, argv, opt) + "\".");
                return;
            }
        }
        if (err.starts_with(unknownLongPrefix)) {
            const std::size_t endQuote = err.find('\'', unknownLongPrefix.size());
            if (endQuote != std::string::npos) {
                const std::string opt = err.substr(unknownLongPrefix.size(), endQuote - unknownLongPrefix.size());
                if (isPremium && opt.starts_with("--premium-")) {
                    logger.printError("invalid --premium option '" + opt.substr(10) + "'.");
                    return;
                }
                logger.printError("unrecognized command line option: \"" + findRawArg(argc, argv, opt) + "\".");
                return;
            }
        }
        if (err.starts_with(unknownShortPrefix)) {
            const std::size_t inPos = err.find(" (in '");
            if (inPos != std::string::npos) {
                const std::size_t start = inPos + 6;
                const std::size_t endQuote = err.find('\'', start);
                if (endQuote != std::string::npos) {
                    logger.printError("unrecognized command line option: \"" + err.substr(start, endQuote - start) + "\".");
                    return;
                }
            }
            const std::size_t dotPos = err.find('.', unknownShortPrefix.size());
            const std::string opt = err.substr(unknownShortPrefix.size(),
                                               dotPos == std::string::npos ? std::string::npos : dotPos - unknownShortPrefix.size());
            logger.printError("unrecognized command line option: \"" + findRawArg(argc, argv, opt) + "\".");
            return;
        }
        logger.printError(err);
    }

    class XMLErrorMessagesLogger : public ErrorLogger
    {
        void reportOut(const std::string & outmsg, Color /*c*/ = Color::Reset) override
        {
            std::cout << outmsg << std::endl;
        }

        void reportErr(const ErrorMessage &msg) override
        {
            reportOut(msg.toXML());
        }

        void reportMetric(const std::string &metric) override
        {
            /* Not used here */
            (void) metric;
        }

        void reportProgress(const std::string & /*filename*/, const char /*stage*/[], const std::size_t /*value*/) override
        {}
    };
}

CmdLineParser::CmdLineParser(CmdLineLogger &logger, Settings &settings, Suppressions &suppressions)
    : mLogger(logger)
    , mSettings(settings)
    , mSuppressions(suppressions)
{}

bool CmdLineParser::fillSettingsFromArgs(int argc, const char* const argv[])
{
    const Result result = parseFromArgs(argc, argv);

    switch (result) {
    case Result::Success:
        break;
    case Result::Exit:
        Settings::terminate();
        return true;
    case Result::Fail:
        return false;
    }

    // Libraries must be loaded before FileLister is executed to ensure markup files will be
    // listed properly.
    if (!loadLibraries(mSettings))
        return false;

    if (!loadAddons(mSettings))
        return false;

    // Check that all include paths exist
    {
        for (auto iter = mSettings.includePaths.cbegin();
             iter != mSettings.includePaths.cend();
             ) {
            const std::string path(Path::toNativeSeparators(*iter));
            if (Path::isDirectory(path))
                ++iter;
            else {
                // TODO: this bypasses the template format and other settings
                // If the include path is not found, warn user and remove the non-existing path from the list.
                if (mSettings.severity.isEnabled(Severity::information))
                    std::cout << "(information) Couldn't find path given by -I '" << path << '\'' << std::endl;
                iter = mSettings.includePaths.erase(iter);
            }
        }
    }

    // Output a warning for the user if he tries to exclude headers
    const std::vector<std::string>& ignored = mIgnoredPaths;
    const bool warn = std::any_of(ignored.cbegin(), ignored.cend(), [](const std::string& i) {
        return Path::isHeader(i);
    });
    if (warn) {
        mLogger.printMessage("filename exclusion does not apply to header (.h and .hpp) files.");
        mLogger.printMessage("Please use --suppress for ignoring results from the header files.");
    }

    const std::vector<std::string>& pathnamesRef = mPathNames;
    const std::list<FileSettings>& fileSettingsRef = mFileSettings;

    // the inputs can only be used exclusively - CmdLineParser should already handle this
    assert(!(!pathnamesRef.empty() && !fileSettingsRef.empty()));

    if (!fileSettingsRef.empty()) {
        std::list<FileSettings> fileSettings;
        if (!mSettings.fileFilters.empty()) {
            // filter only for the selected filenames from all project files
            PathMatch filtermatcher(mSettings.fileFilters, Path::getCurrentPath());
            std::copy_if(fileSettingsRef.cbegin(), fileSettingsRef.cend(), std::back_inserter(fileSettings), [&](const FileSettings &fs) {
                return filtermatcher.match(fs.filename());
            });
            if (fileSettings.empty()) {
                for (const std::string& f: mSettings.fileFilters)
                    mLogger.printError("could not find any files matching the filter:" + f);
                return false;
            }
        }
        else {
            fileSettings = fileSettingsRef;
        }

        // TODO: de-duplicate

        mFileSettings.clear();

        frontend::applyLang(fileSettings, mSettings, mEnforcedLang);

        // sort the markup last
        std::copy_if(fileSettings.cbegin(), fileSettings.cend(), std::back_inserter(mFileSettings), [&](const FileSettings &fs) {
            return !mSettings.library.markupFile(fs.filename()) || !mSettings.library.processMarkupAfterCode(fs.filename());
        });

        std::copy_if(fileSettings.cbegin(), fileSettings.cend(), std::back_inserter(mFileSettings), [&](const FileSettings &fs) {
            return mSettings.library.markupFile(fs.filename()) && mSettings.library.processMarkupAfterCode(fs.filename());
        });

        if (mFileSettings.empty()) {
            mLogger.printError("could not find or open any of the paths given.");
            return false;
        }
    }

    if (!pathnamesRef.empty()) {
        std::list<FileWithDetails> filesResolved;
        // Execute recursiveAddFiles() to each given file parameter
        // TODO: verbose log which files were ignored?
        const PathMatch matcher(ignored, Path::getCurrentPath());
        for (const std::string &pathname : pathnamesRef) {
            const std::string err = FileLister::recursiveAddFiles(filesResolved, Path::toNativeSeparators(pathname), mSettings.library.markupExtensions(), matcher, mSettings.debugignore);
            if (!err.empty()) {
                // TODO: bail out?
                mLogger.printMessage(err);
            }
        }

        if (filesResolved.empty()) {
            mLogger.printError("could not find or open any of the paths given.");
            // TODO: PathMatch should provide the information if files were ignored
            if (!ignored.empty())
                mLogger.printMessage("Maybe all paths were ignored?");
            return false;
        }

        std::list<FileWithDetails> files;
        if (!mSettings.fileFilters.empty()) {
            files = filterFiles(mSettings.fileFilters, filesResolved);
            if (files.empty()) {
                for (const std::string& f: mSettings.fileFilters)
                    mLogger.printError("could not find any files matching the filter:" + f);
                return false;
            }
        }
        else {
            files = std::move(filesResolved);
        }

        // de-duplicate files
        {
            auto it = files.begin();
            while (it != files.end()) {
                const std::string& absname = it->abspath();
                // TODO: log if duplicated files were dropped
                files.erase(std::remove_if(std::next(it), files.end(), [&](const FileWithDetails& entry) {
                    return entry.abspath() == absname;
                }), files.end());
                ++it;
            }
        }

        frontend::applyLang(files, mSettings, mEnforcedLang);

        // sort the markup last
        std::copy_if(files.cbegin(), files.cend(), std::inserter(mFiles, mFiles.end()), [&](const FileWithDetails& entry) {
            return !mSettings.library.markupFile(entry.path()) || !mSettings.library.processMarkupAfterCode(entry.path());
        });

        std::copy_if(files.cbegin(), files.cend(), std::inserter(mFiles, mFiles.end()), [&](const FileWithDetails& entry) {
            return mSettings.library.markupFile(entry.path()) && mSettings.library.processMarkupAfterCode(entry.path());
        });

        if (mFiles.empty()) {
            mLogger.printError("could not find or open any of the paths given.");
            return false;
        }
    }

    return true;
}

// TODO: normalize/simplify/native all path parameters
// TODO: error out on all missing given files/paths
CmdLineParser::Result CmdLineParser::parseFromArgs(int argc, const char* const argv[])
{
    mSettings.exename = Path::getCurrentExecutablePath(argv[0]);

    bool xmlOptionProvided = false;
    bool outputFormatOptionProvided = false;

    // default to --check-level=normal from CLI for now
    mSettings.setCheckLevel(Settings::CheckLevel::normal);

    // read --debug-lookup early so the option is available for the cppcheck.cfg loading
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--debug-lookup") == 0)
            mSettings.debuglookup = true;
        else if (std::strncmp(argv[i], "--debug-lookup=", 15) == 0) {
            const std::string lookup = argv[i] + 15;
            if (lookup == "all")
                mSettings.debuglookup = true;
            else if (lookup == "addon")
                mSettings.debuglookupAddon = true;
            else if (lookup == "config")
                mSettings.debuglookupConfig = true;
            else if (lookup == "library")
                mSettings.debuglookupLibrary = true;
            else if (lookup == "platform")
                mSettings.debuglookupPlatform = true;
            else {
                mLogger.printError("unknown lookup '" + lookup + "'");
                return Result::Fail;
            }
        }
    }

    if (!loadCppcheckCfg())
        return Result::Fail;

    if (argc <= 1) {
        printHelp();
        return Result::Exit;
    }

    // check for exclusive options (take precedence over invalid other options)
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--doc") == 0) {
            std::ostringstream doc;
            for (const Check * const c : CheckInstances::get()) {
                const std::string& name(c->name());
                const std::string info(c->classInfo());
                if (!name.empty() && !info.empty())
                    doc << "## " << name << " ##\n"
                        << info << "\n";
            }
            mLogger.printRaw(doc.str());
            return Result::Exit;
        }
        if (std::strcmp(argv[i], "--errorlist") == 0) {
            {
                XMLErrorMessagesLogger xmlLogger;
                std::cout << ErrorMessage::getXMLHeader(mSettings.cppcheckCfgProductName, 2);
                CppCheck::getErrorMessages(xmlLogger);
                std::cout << ErrorMessage::getXMLFooter(2) << std::endl;
            }
            return Result::Exit;
        }
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            printHelp();
            return Result::Exit;
        }
        if (std::strcmp(argv[i], "--filesdir") == 0) {
#ifdef FILESDIR
            mLogger.printRaw(FILESDIR);
#endif
            return Result::Exit;
        }
        if (std::strcmp(argv[i], "--version") == 0) {
            mLogger.printRaw(getVersion());
            return Result::Exit;
        }
    }

    // Parse all options and positional files via reflect-cpp CLI.
    auto parsed = rfl::cli::read_detailed<CppcheckArgs, CppcheckCliProcessors>(argc, argv);
    if (!parsed) {
        reportCliParseError(mLogger, parsed.error().what(), argc, argv, mSettings.premium);
        return Result::Fail;
    }
    const auto& occurrences = parsed->occurrences;
    const auto& positionalFiles = parsed->value.files.value();

    bool debug = false;
    bool inputAsFilter = false; // set by: --file-filter=+

    ImportProject::Type projectType = ImportProject::Type::NONE;
    ImportProject project;
    std::string vsConfig;

    std::string platform;
    char defaultSign = '\0';

    std::vector<std::string> lookupPaths{
        Path::getCurrentPath(), // TODO: do we want to look in CWD?
        Path::getPathFromFilename(mSettings.exename),
    };

    bool executorAuto = true;

    for (const auto& [opt, val] : occurrences) {
        if (opt == "define") {
            std::string define = val;
            if (define.find('=') == std::string::npos)
                define += "=1";
            if (!mSettings.userDefines.empty())
                mSettings.userDefines += ";";
            mSettings.userDefines += define;
        }
        else if (opt == "preprocess-only") {
            mSettings.preprocessOnly = true;
            mSettings.quiet = true;
        }
        else if (opt == "keep-comments") {
            mSettings.keepComments = true;
        }
        else if (opt == "include-dir") {
            std::string path = Path::fromNativeSeparators(Path::removeQuotationMarks(val));
            if (!endsWith(path, '/'))
                path += '/';
            mSettings.includePaths.emplace_back(std::move(path));
        }
        else if (opt == "undefine") {
            mSettings.userUndefs.insert(val);
        }
        else if (opt == "addon") {
            mSettings.addons.emplace(val);
        }
        else if (opt == "addon-python") {
            mSettings.addonPython = val;
        }
        else if (opt == "analyze-all-vs-configs") {
            mSettings.analyzeAllVsConfigs = true;
            mAnalyzeAllVsConfigsSetOnCmdLine = true;
        }
        else if (opt == "check-config") {
            mSettings.checkConfiguration = true;
        }
        else if (opt == "check-headers") {
            mSettings.checkHeaders = true;
        }
        else if (opt == "check-level") {
            Settings::CheckLevel level = Settings::CheckLevel::normal;
            if (val == "reduced")
                level = Settings::CheckLevel::reduced;
            else if (val == "normal")
                level = Settings::CheckLevel::normal;
            else if (val == "exhaustive")
                level = Settings::CheckLevel::exhaustive;
            else {
                mLogger.printError("unknown '--check-level' value '" + val + "'.");
                return Result::Fail;
            }
            mSettings.setCheckLevel(level);
        }
        else if (opt == "check-library") {
            mSettings.checkLibrary = true;
        }
        else if (opt == "check-unused-templates") {
            mSettings.checkUnusedTemplates = true;
        }
        else if (opt == "check-version") {
            if (!loadCppcheckCfg())
                return Result::Fail;
            if (getVersion() != val) {
                mLogger.printError("--check-version check failed. Aborting.");
                return Result::Fail;
            }
        }
        else if (opt == "checkers-report") {
            mSettings.checkersReportFilename = val;
        }
        else if (opt == "checks-max-time") {
            if (!parseNumberValue(mLogger, "--checks-max-time=", val, mSettings.checksMaxTime, true))
                return Result::Fail;
        }
        else if (opt == "clang") {
            mSettings.clang = true;
            mSettings.clangExecutable = val;
        }
        else if (opt == "clang-tidy") {
            mSettings.clangTidy = true;
            mSettings.clangTidyExecutable = val;
        }
        else if (opt == "config-exclude") {
            mSettings.configExcludePaths.insert(Path::fromNativeSeparators(val));
        }
        else if (opt == "config-excludes-file") {
            if (!addPathsToSet(val, mSettings.configExcludePaths)) {
                mLogger.printError("unable to open config excludes file at '" + val + "'");
                return Result::Fail;
            }
        }
        else if (opt == "cppcheck-build-dir") {
            std::string path = Path::fromNativeSeparators(val);
            if (path.empty()) {
                mLogger.printError("no path has been specified for --cppcheck-build-dir");
                return Result::Fail;
            }
            if (endsWith(path, '/'))
                path.pop_back();
            mSettings.buildDir = std::move(path);
        }
        else if (opt == "cpp-header-probe") {
            mSettings.cppHeaderProbe = true;
        }
        else if (opt == "debug-analyzerinfo") {
            mSettings.debugainfo = true;
        }
        else if (opt == "debug-ast") {
            mSettings.debugast = true;
        }
        else if (opt == "debug-clang-output") {
            mSettings.debugClangOutput = true;
        }
        else if (opt == "debug-ignore") {
            mSettings.debugignore = true;
        }
        else if (opt == "debug-ipc") {
            mSettings.debugipc = true;
        }
        else if (opt == "debug" || opt == "debug-normal") {
            debug = true;
        }
        else if (opt == "debug-lookup") {
            continue; // already handled above
        }
        else if (opt == "debug-simplified") {
            mSettings.debugSimplified = true;
        }
        else if (opt == "debug-symdb") {
            mSettings.debugsymdb = true;
        }
        else if (opt == "debug-template") {
            mSettings.debugtemplate = true;
        }
        else if (opt == "debug-valueflow") {
            mSettings.debugvalueflow = true;
        }
        else if (opt == "debug-warnings") {
            mSettings.debugwarnings = true;
        }
        else if (opt == "disable") {
            const std::string errmsg = mSettings.removeEnabled(val);
            if (!errmsg.empty()) {
                mLogger.printError(errmsg);
                return Result::Fail;
            }
        }
        else if (opt == "dump") {
            mSettings.dump = true;
        }
        else if (opt == "emit-duplicates") {
            mSettings.emitDuplicates = true;
        }
        else if (opt == "enable") {
            const std::string errmsg = mSettings.addEnabled(val);
            if (!errmsg.empty()) {
                mLogger.printError(errmsg);
                return Result::Fail;
            }
            // when "style" is enabled, also enable "warning", "performance" and "portability"
            if (val.find("style") != std::string::npos) {
                mSettings.addEnabled("warning");
                mSettings.addEnabled("performance");
                mSettings.addEnabled("portability");
            }
        }
        else if (opt == "error-exitcode") {
            if (!parseNumberValue(mLogger, "--error-exitcode=", val, mSettings.exitCode))
                return Result::Fail;
        }
        else if (opt == "exception-handling") {
#if defined(USE_WINDOWS_SEH) || defined(USE_UNIX_SIGNAL_HANDLING)
            if (val != "stderr" && val != "stdout") {
                mLogger.printError("invalid '--exception-handling' argument");
                return Result::Fail;
            }
            mSettings.exceptionHandling = true;
            mSettings.exceptionOutput = (val == "stderr") ? stderr : stdout;
#else
            mLogger.printError("Option --exception-handling is not supported since Cppcheck has not been built with any exception handling enabled.");
            return Result::Fail;
#endif
        }
        else if (opt == "executor") {
            if (val == "auto") {
                executorAuto = true;
                mSettings.executor = Settings::defaultExecutor();
            }
            else if (val == "thread") {
#if defined(HAS_THREADING_MODEL_THREAD)
                executorAuto = false;
                mSettings.executor = Settings::ExecutorType::Thread;
#else
                mLogger.printError("executor type 'thread' cannot be used as Cppcheck has not been built with a respective threading model.");
                return Result::Fail;
#endif
            }
            else if (val == "process") {
#if defined(HAS_THREADING_MODEL_FORK)
                executorAuto = false;
                mSettings.executor = Settings::ExecutorType::Process;
#else
                mLogger.printError("executor type 'process' cannot be used as Cppcheck has not been built with a respective threading model.");
                return Result::Fail;
#endif
            }
            else {
                mLogger.printError("unknown executor: '" + val + "'.");
                return Result::Fail;
            }
        }
        else if (opt == "exitcode-suppress") {
            const std::string errmsg(mSuppressions.nofail.addSuppressionLine(val));
            if (!errmsg.empty()) {
                mLogger.printError(errmsg);
                return Result::Fail;
            }
        }
        else if (opt == "exitcode-suppressions") {
            std::ifstream f(val);
            if (!f.is_open()) {
                mLogger.printError("couldn't open the file: \"" + val + "\".");
                return Result::Fail;
            }
            const std::string errmsg(mSuppressions.nofail.parseFile(f));
            if (!errmsg.empty()) {
                mLogger.printError(errmsg);
                return Result::Fail;
            }
        }
        else if (opt == "file-filter") {
            if (val == "-") {
                if (!addFilesToList(val, mSettings.fileFilters)) {
                    mLogger.printError("Failed: --file-filter=-");
                    return Result::Fail;
                }
            } else if (val == "+") {
                inputAsFilter = true;
            } else {
                mSettings.fileFilters.emplace_back(val);
            }
        }
        else if (opt == "file-list") {
            if (!addFilesToList(val, mPathNames)) {
                mLogger.printError("couldn't open the file: \"" + val + "\".");
                return Result::Fail;
            }
        }
        else if (opt == "force") {
            mSettings.force = true;
            mSettings.maxConfigsOption = Settings::maxConfigsNotAssigned;
        }
        else if (opt == "fsigned-char") {
            defaultSign = 's';
        }
        else if (opt == "funsigned-char") {
            defaultSign = 'u';
        }
        else if (opt == "ignore") {
            if (!val.empty())
                mIgnoredPaths.emplace_back(val);
        }
        else if (opt == "include") {
            mSettings.userIncludes.emplace_back(Path::fromNativeSeparators(val));
        }
        else if (opt == "includes-file") {
            if (!addIncludePathsToList(val, mSettings.includePaths)) {
                mLogger.printError("unable to open includes file at '" + val + "'");
                return Result::Fail;
            }
        }
        else if (opt == "inconclusive") {
            mSettings.certainty.enable(Certainty::inconclusive);
        }
        else if (opt == "inline-suppr") {
            mSettings.inlineSuppressions = true;
        }
        else if (opt == "jobs") {
            unsigned int tmp;
            std::string err;
            if (!strToInt(val, tmp, &err)) {
                mLogger.printError("argument to '-j' is not valid - " + err + ".");
                return Result::Fail;
            }
            if (tmp == 0) {
                mLogger.printError("argument for '-j' must be greater than 0.");
                return Result::Fail;
            }
            if (tmp > 1024) {
                mLogger.printError("argument for '-j' is allowed to be 1024 at max.");
                return Result::Fail;
            }
            mSettings.jobs = tmp;
        }
        else if (opt == "load-average") {
#ifdef HAS_THREADING_MODEL_FORK
            int tmp;
            std::string err;
            if (!strToInt(val, tmp, &err)) {
                mLogger.printError("argument to '-l' is not valid - " + err + ".");
                return Result::Fail;
            }
            mSettings.loadAverage = tmp;
#else
            mLogger.printError("Option -l cannot be used as Cppcheck has not been built with fork threading model.");
            return Result::Fail;
#endif
        }
        else if (opt == "language") {
            if (val == "c")
                mEnforcedLang = Standards::Language::C;
            else if (val == "c++")
                mEnforcedLang = Standards::Language::CPP;
            else {
                mLogger.printError("unknown language '" + val + "' enforced.");
                return Result::Fail;
            }
        }
        else if (opt == "library") {
            std::vector<std::string> libs = splitString(val, ',');
            for (auto& l : libs) {
                if (l.empty()) {
                    mLogger.printError("empty library specified.");
                    return Result::Fail;
                }
                mSettings.libraries.emplace_back(std::move(l));
            }
        }
        else if (opt == "max-configs") {
            int tmp;
            if (!parseNumberValue(mLogger, "--max-configs=", val, tmp))
                return Result::Fail;
            if (tmp < 1) {
                mLogger.printError("argument to '--max-configs=' must be greater than 0.");
                return Result::Fail;
            }
            mSettings.maxConfigsOption = tmp;
            mSettings.force = false;
        }
        else if (opt == "max-ctu-depth") {
            int temp = 0;
            if (!parseNumberValue(mLogger, "--max-ctu-depth=", val, temp))
                return Result::Fail;
            if (temp > 10) {
                mLogger.printMessage("--max-ctu-depth is being capped at 10. This limitation will be removed in a future Cppcheck version.");
                temp = 10;
            }
            mSettings.maxCtuDepth = temp;
        }
        else if (opt == "max-template-recursion") {
            if (!parseNumberValue(mLogger, "--max-template-recursion=", val, mSettings.maxTemplateRecursion))
                return Result::Fail;
        }
        else if (opt == "no-analyze-all-vs-configs") {
            mSettings.analyzeAllVsConfigs = false;
            mAnalyzeAllVsConfigsSetOnCmdLine = true;
        }
        else if (opt == "no-check-headers") {
            mSettings.checkHeaders = false;
        }
        else if (opt == "no-check-unused-templates") {
            mSettings.checkUnusedTemplates = false;
        }
        else if (opt == "no-cppcheck-build-dir") {
            mSettings.buildDir.clear();
        }
        else if (opt == "no-cpp-header-probe") {
            mSettings.cppHeaderProbe = false;
        }
        else if (opt == "no-safety") {
            mSettings.safety = false;
        }
        else if (opt == "output-file") {
            mSettings.outputFile = Path::simplifyPath(val);
        }
        else if (opt == "output-format") {
            if (xmlOptionProvided) {
                outputFormatOptionMixingError();
                return Result::Fail;
            }
            if (val == "text")
                mSettings.outputFormat = Settings::OutputFormat::text;
            else if (val == "sarif")
                mSettings.outputFormat = Settings::OutputFormat::sarif;
            else if (val == "xml")
                mSettings.outputFormat = Settings::OutputFormat::xml;
            else if (val == "xmlv2") {
                mSettings.outputFormat = Settings::OutputFormat::xml;
                mSettings.xml_version = 2;
            } else if (val == "xmlv3") {
                mSettings.outputFormat = Settings::OutputFormat::xml;
                mSettings.xml_version = 3;
            } else {
                mLogger.printError("argument to '--output-format=' must be 'text', 'sarif', 'xml' (deprecated), 'xmlv2' or 'xmlv3'.");
                return Result::Fail;
            }
            mSettings.plistOutput = "";
            outputFormatOptionProvided = true;
        }
        else if (opt == "performance-valueflow-max-time") {
            if (!parseNumberValue(mLogger, "--performance-valueflow-max-time=", val, mSettings.vfOptions.maxTime, true))
                return Result::Fail;
        }
        else if (opt == "performance-valueflow-max-if-count") {
            if (!parseNumberValue(mLogger, "--performance-valueflow-max-if-count=", val, mSettings.vfOptions.maxIfCount, true))
                return Result::Fail;
        }
        else if (opt == "performance-valueflow-max-iterations") {
            if (!parseNumberValue(mLogger, "--performance-valueflow-max-iterations=", val, mSettings.vfOptions.maxIterations, true))
                return Result::Fail;
        }
        else if (opt == "platform") {
            if (val.empty()) {
                mLogger.printError("empty platform specified.");
                return Result::Fail;
            }
            platform = val;
        }
        else if (opt == "plist-output") {
            std::string path = Path::simplifyPath(val);
            if (path.empty())
                path = ".";

            const std::string plistOutput = Path::toNativeSeparators(path);
            if (!Path::isDirectory(plistOutput)) {
                mLogger.printError("plist folder does not exist: '" + plistOutput + "'.");
                return Result::Fail;
            }

            if (!endsWith(path, '/'))
                path += '/';

            mSettings.outputFormat = Settings::OutputFormat::plist;
            mSettings.plistOutput = std::move(path);
        }
        else if (opt == "premium" || opt == "premium-cert-c-int-precision" || opt == "premium-license-file") {
            if (!mSettings.premium) {
                mLogger.printError("unrecognized command line option: \"" + findRawArg(argc, argv, "--" + opt) + "\".");
                return Result::Fail;
            }
            if (opt == "premium") {
                static const std::set<std::string> valid{
                    "autosar", "cert-c-2016", "cert-c++-2016", "cert-cpp-2016",
                    "cert-c", "cert-c++", "metrics",
                    "misra-c-2012", "misra-c-2023", "misra-c-2025",
                    "misra-c++-2008", "misra-cpp-2008", "misra-c++-2023", "misra-cpp-2023",
                    "bughunting", "safety", "safety-profiles", "debug-progress"};

                if (val == "safety-off") {
                    mSettings.safety = false;
                    continue;
                }
                if (val == "safety")
                    mSettings.safety = true;
                const bool isCodingStandard = startsWith(val, "autosar") || startsWith(val, "cert-") || startsWith(val, "misra-") || val == "safety-profiles";
                const std::string p3(endsWith(val, ":all") && isCodingStandard ? val.substr(0, val.rfind(':')) : val);
                if (!valid.count(p3)) {
                    mLogger.printError("invalid --premium option '" + val + "'.");
                    return Result::Fail;
                }
                if (!mSettings.premiumArgs.empty())
                    mSettings.premiumArgs += " ";
                if (val.find(' ') != std::string::npos)
                    mSettings.premiumArgs += "\"--" + val + "\"";
                else
                    mSettings.premiumArgs += "--" + val;
                if (isCodingStandard) {
                    mSettings.addEnabled("warning");
                    mSettings.addEnabled("portability");
                }
            }
            else if (opt == "premium-cert-c-int-precision") {
                int tmp;
                if (!parseNumberValue(mLogger, "--premium-cert-c-int-precision=", val, tmp, true))
                    return Result::Fail;
                if (!mSettings.premiumArgs.empty())
                    mSettings.premiumArgs += " ";
                const std::string p = "cert-c-int-precision=" + val;
                mSettings.premiumArgs += (p.find(' ') != std::string::npos) ? ("\"--" + p + "\"") : ("--" + p);
            }
            else if (opt == "premium-license-file") {
                if (!mSettings.premiumArgs.empty())
                    mSettings.premiumArgs += " ";
                const std::string p = "license-file=" + val;
                mSettings.premiumArgs += (p.find(' ') != std::string::npos) ? ("\"--" + p + "\"") : ("--" + p);
            }
        }
        else if (opt == "project") {
            if (projectType != ImportProject::Type::NONE) {
                mLogger.printError("multiple --project options are not supported.");
                return Result::Fail;
            }

            std::string projectFile = val;
            projectType = project.import(projectFile, &mSettings, &mSuppressions);
            if (projectType == ImportProject::Type::CPPCHECK_GUI) {
                for (const std::string &lib : project.guiProject.libraries)
                    mSettings.libraries.emplace_back(lib);

                const auto& excludedPaths = project.guiProject.excludedPaths;
                std::copy(excludedPaths.cbegin(), excludedPaths.cend(), std::back_inserter(mIgnoredPaths));

                if (!project.guiProject.platform.empty())
                    platform = project.guiProject.platform;

                // look for external files relative to project first
                lookupPaths.insert(lookupPaths.cbegin(), Path::getPathFromFilename(projectFile));

                const auto& projectFileGui = project.guiProject.projectFile;
                if (!projectFileGui.empty()) {
                    // read underlying project
                    projectFile = projectFileGui;
                    projectType = project.import(projectFileGui, &mSettings, &mSuppressions);
                    if (projectType == ImportProject::Type::CPPCHECK_GUI) {
                        mLogger.printError("nested Cppcheck GUI projects are not supported.");
                        return Result::Fail;
                    }
                }
            }
            if (projectType == ImportProject::Type::COMPILE_DB)
                mSettings.maxConfigsProject = 1;
            if (projectType == ImportProject::Type::VS_SLN ||
                projectType == ImportProject::Type::VS_SLNX ||
                projectType == ImportProject::Type::VS_VCXPROJ) {
                mSettings.libraries.emplace_back("windows");
            }
            for (const auto &error : project.errors)
                mLogger.printError(error);
            if (projectType == ImportProject::Type::MISSING) {
                mLogger.printError("failed to open project '" + projectFile + "'. The file does not exist.");
                return Result::Fail;
            }
            if (projectType == ImportProject::Type::UNKNOWN) {
                mLogger.printError("failed to load project '" + projectFile + "'. The format is unknown.");
                return Result::Fail;
            }
            if (projectType == ImportProject::Type::FAILURE) {
                mLogger.printError("failed to load project '" + projectFile + "'. An error occurred.");
                return Result::Fail;
            }
        }
        else if (opt == "project-configuration") {
            vsConfig = val;
            if (vsConfig.empty()) {
                mLogger.printError("--project-configuration parameter is empty.");
                return Result::Fail;
            }
            if (projectType != ImportProject::Type::VS_SLN &&
                projectType != ImportProject::Type::VS_SLNX &&
                projectType != ImportProject::Type::VS_VCXPROJ) {
                mLogger.printError("--project-configuration has no effect - no Visual Studio project provided.");
                return Result::Fail;
            }
        }
        else if (opt == "quiet") {
            mSettings.quiet = true;
        }
        else if (opt == "relative-paths") {
            mSettings.relativePaths = true;
            if (!val.empty()) {
                std::string paths = val;
                for (;;) {
                    const std::string::size_type pos = paths.find(';');
                    if (pos == std::string::npos) {
                        mSettings.basePaths.emplace_back(Path::fromNativeSeparators(std::move(paths)));
                        break;
                    }
                    mSettings.basePaths.emplace_back(Path::fromNativeSeparators(paths.substr(0, pos)));
                    paths.erase(0, pos + 1);
                }
            } else {
                for (int i = 1; i < argc; ++i) {
                    if (std::strcmp(argv[i], "-rp=") == 0 || std::strcmp(argv[i], "--relative-paths=") == 0) {
                        mLogger.printError("no paths specified for the '" + std::string(argv[i]) + "' option.");
                        return Result::Fail;
                    }
                }
            }
        }
        else if (opt == "report-progress") {
            if (!parseNumberValue(mLogger, "--report-progress=", val, mSettings.reportProgress, true))
                return Result::Fail;
        }
        else if (opt == "report-type") {
            if (val == "normal") {
                mSettings.reportType = ReportType::normal;
            } else if (val == "autosar") {
                mSettings.reportType = ReportType::autosar;
            } else if (val == "cert-c-2016") {
                mSettings.reportType = ReportType::certC;
            } else if (val == "cert-cpp-2016") {
                mSettings.reportType = ReportType::certCpp;
            } else if (val == "misra-c-2012") {
                mSettings.reportType = ReportType::misraC2012;
            } else if (val == "misra-c-2023") {
                mSettings.reportType = ReportType::misraC2023;
            } else if (val == "misra-c-2025") {
                mSettings.reportType = ReportType::misraC2025;
            } else if (val == "misra-cpp-2008") {
                mSettings.reportType = ReportType::misraCpp2008;
            } else if (val == "misra-cpp-2023") {
                mSettings.reportType = ReportType::misraCpp2023;
            } else {
                mLogger.printError("Unknown report type '" + val + "'");
                return Result::Fail;
            }
        }
        else if (opt == "rule") {
#ifdef HAVE_RULES
            Rule rule;
            rule.pattern = val;

            if (rule.pattern.empty()) {
                mLogger.printError("no rule pattern provided.");
                return Result::Fail;
            }

            std::string regex_err;
            auto regex = Regex::create(rule.pattern, Regex::Engine::Pcre, regex_err);
            if (!regex) {
                mLogger.printError("failed to compile rule pattern '" + rule.pattern + "' (" + regex_err + ").");
                return Result::Fail;
            }
            rule.regex = std::move(regex);
            mSettings.rules.emplace_back(std::move(rule));
#else
            mLogger.printError("Option --rule cannot be used as Cppcheck has not been built with rules support.");
            return Result::Fail;
#endif
        }
        else if (opt == "rule-file") {
#ifdef HAVE_RULES
            const std::string& ruleFile = val;
            tinyxml2::XMLDocument doc;
            const tinyxml2::XMLError err = doc.LoadFile(ruleFile.c_str());
            if (err == tinyxml2::XML_SUCCESS) {
                const tinyxml2::XMLElement *node = doc.FirstChildElement();
                if (node && strcmp(node->Value(), "rules") == 0)
                    node = node->FirstChildElement("rule");
                for (; node && strcmp(node->Value(), "rule") == 0; node = node->NextSiblingElement()) {
                    Rule rule;
                    Regex::Engine regexEngine = Regex::Engine::Pcre;

                    for (const tinyxml2::XMLElement *subnode = node->FirstChildElement(); subnode; subnode = subnode->NextSiblingElement()) {
                        const char * const subname = subnode->Name();
                        const char * const subtext = subnode->GetText();
                        if (std::strcmp(subname, "tokenlist") == 0) {
                            rule.tokenlist = empty_if_null(subtext);
                        }
                        else if (std::strcmp(subname, "pattern") == 0) {
                            rule.pattern = empty_if_null(subtext);
                        }
                        else if (std::strcmp(subname, "message") == 0) {
                            for (const tinyxml2::XMLElement *msgnode = subnode->FirstChildElement(); msgnode; msgnode = msgnode->NextSiblingElement()) {
                                const char * const msgname = msgnode->Name();
                                const char * const msgtext = msgnode->GetText();
                                if (std::strcmp(msgname, "severity") == 0) {
                                    rule.severity = severityFromString(empty_if_null(msgtext));
                                }
                                else if (std::strcmp(msgname, "id") == 0) {
                                    rule.id = empty_if_null(msgtext);
                                }
                                else if (std::strcmp(msgname, "summary") == 0) {
                                    rule.summary = empty_if_null(msgtext);
                                }
                                else {
                                    mLogger.printError("unable to load rule-file '" + ruleFile + "' - unknown element '" + msgname + "' encountered in 'message'.");
                                    return Result::Fail;
                                }
                            }
                        }
                        else if (std::strcmp(subname, "engine") == 0) {
                            const char * const engine = empty_if_null(subtext);
                            if (std::strcmp(engine, "pcre") == 0) {
                                regexEngine = Regex::Engine::Pcre;
                            }
                            else {
                                mLogger.printError(std::string("unknown regex engine '") + engine + "'.");
                                return Result::Fail;
                            }
                        }
                        else {
                            mLogger.printError("unable to load rule-file '" + ruleFile + "' - unknown element '" + subname + "' encountered in 'rule'.");
                            return Result::Fail;
                        }
                    }

                    if (rule.pattern.empty()) {
                        mLogger.printError("unable to load rule-file '" + ruleFile + "' - a rule is lacking a pattern.");
                        return Result::Fail;
                    }
                    if (rule.id.empty()) {
                        mLogger.printError("unable to load rule-file '" + ruleFile + "' - a rule is lacking an id.");
                        return Result::Fail;
                    }
                    if (rule.tokenlist.empty()) {
                        mLogger.printError("unable to load rule-file '" + ruleFile + "' - a rule is lacking a tokenlist.");
                        return Result::Fail;
                    }
                    if (rule.tokenlist != "normal" && rule.tokenlist != "define" && rule.tokenlist != "raw") {
                        mLogger.printError("unable to load rule-file '" + ruleFile + "' - a rule is using the unsupported tokenlist '" + rule.tokenlist + "'.");
                        return Result::Fail;
                    }

                    std::string regex_err;
                    auto regex = Regex::create(rule.pattern, regexEngine, regex_err);
                    if (!regex) {
                        mLogger.printError("unable to load rule-file '" + ruleFile + "' - pattern '" + rule.pattern + "' failed to compile (" + regex_err + ").");
                        return Result::Fail;
                    }
                    rule.regex = std::move(regex);

                    if (rule.severity == Severity::none) {
                        mLogger.printError("unable to load rule-file '" + ruleFile + "' - a rule has an invalid severity.");
                        return Result::Fail;
                    }

                    mSettings.rules.emplace_back(std::move(rule));
                }
            } else {
                mLogger.printError("unable to load rule-file '" + ruleFile + "' (" + tinyxml2::XMLDocument::ErrorIDToName(err) + ").");
                return Result::Fail;
            }
#else
            mLogger.printError("Option --rule-file cannot be used as Cppcheck has not been built with rules support.");
            return Result::Fail;
#endif
        }
        else if (opt == "safety") {
            mSettings.safety = true;
        }
        else if (opt == "showtime") {
            if (val == "file")
                mSettings.showtime = Settings::ShowTime::FILE;
            else if (val == "file-total")
                mSettings.showtime = Settings::ShowTime::FILE_TOTAL;
            else if (val == "summary")
                mSettings.showtime = Settings::ShowTime::SUMMARY;
            else if (val == "top5_file")
                mSettings.showtime = Settings::ShowTime::TOP5_FILE;
            else if (val == "top5_summary")
                mSettings.showtime = Settings::ShowTime::TOP5_SUMMARY;
            else if (val == "none")
                mSettings.showtime = Settings::ShowTime::NONE;
            else if (val.empty()) {
                mLogger.printError("no mode provided for --showtime");
                return Result::Fail;
            }
            else {
                mLogger.printError("unrecognized --showtime mode: '" + val + "'. Supported modes: file, file-total, summary, top5_file, top5_summary.");
                return Result::Fail;
            }
        }
        else if (opt == "std") {
            if (!mSettings.standards.setStd(val)) {
                mLogger.printError("unknown --std value '" + val + "'");
                return Result::Fail;
            }
        }
        else if (opt == "suppress") {
            const std::string errmsg(mSuppressions.nomsg.addSuppressionLine(val));
            if (!errmsg.empty()) {
                mLogger.printError(errmsg);
                return Result::Fail;
            }
        }
        else if (opt == "suppressions-list") {
            std::ifstream f(val);
            if (!f.is_open()) {
                std::string message("couldn't open the file: \"");
                message += val;
                message += "\".";
                if (std::count(val.cbegin(), val.cend(), ',') > 0 ||
                    std::count(val.cbegin(), val.cend(), '.') > 1) {
                    message += "\nIf you want to pass two files, you can do it e.g. like this:";
                    message += "\n    cppcheck --suppressions-list=a.txt --suppressions-list=b.txt file.cpp";
                }
                mLogger.printError(message);
                return Result::Fail;
            }
            const std::string errmsg(mSuppressions.nomsg.parseFile(f));
            if (!errmsg.empty()) {
                mLogger.printError(errmsg);
                return Result::Fail;
            }
        }
        else if (opt == "suppress-xml") {
            const std::string errmsg(mSuppressions.nomsg.parseXmlFile(val.c_str()));
            if (!errmsg.empty()) {
                mLogger.printError(errmsg);
                return Result::Fail;
            }
        }
        else if (opt == "template") {
            mSettings.templateFormat = val;
            if (mSettings.templateFormat == "gcc") {
                mSettings.templateFormat = "{bold}{file}:{line}:{column}: {magenta}warning:{default} {message} [{id}]{reset}\\n{code}";
                mSettings.templateLocation = "{bold}{file}:{line}:{column}: {dim}note:{reset} {info}\\n{code}";
            } else if (mSettings.templateFormat == "daca2") {
                mSettings.daca = true;
                mSettings.templateFormat = "{file}:{line}:{column}: {severity}:{inconclusive:inconclusive:} {message} [{id}]";
                mSettings.templateLocation = "{file}:{line}:{column}: note: {info}";
            } else if (mSettings.templateFormat == "vs")
                mSettings.templateFormat = "{file}({line}): {severity}: {message}";
            else if (mSettings.templateFormat == "edit")
                mSettings.templateFormat = "{file} +{line}: {severity}: {message}";
            else if (mSettings.templateFormat == "cppcheck1")
                mSettings.templateFormat = "{callstack}: ({severity}{inconclusive:, inconclusive}) {message}";
            else if (mSettings.templateFormat == "selfcheck") {
                mSettings.templateFormat = "{file}:{line}:{column}: {severity}:{inconclusive:inconclusive:} {message} [{id}]\\n{code}";
                mSettings.templateLocation = "{file}:{line}:{column}: note: {info}\\n{code}";
                mSettings.daca = true;
            } else if (mSettings.templateFormat == "simple") {
                mSettings.templateFormat = "{file}:{line}:{column}: {severity}:{inconclusive:inconclusive:} {message} [{id}]";
                mSettings.templateLocation = "";
            }
        }
        else if (opt == "template-location") {
            mSettings.templateLocation = val;
        }
        else if (opt == "template-max-time") {
            if (!parseNumberValue(mLogger, "--template-max-time=", val, mSettings.templateMaxTime))
                return Result::Fail;
        }
        else if (opt == "typedef-max-time") {
            if (!parseNumberValue(mLogger, "--typedef-max-time=", val, mSettings.typedefMaxTime))
                return Result::Fail;
        }
        else if (opt == "valueflow-max-iterations") {
            if (!parseNumberValue(mLogger, "--valueflow-max-iterations=", val, mSettings.vfOptions.maxIterations))
                return Result::Fail;
        }
        else if (opt == "verbose") {
            mSettings.verbose = true;
        }
        else if (opt == "xml") {
            if (outputFormatOptionProvided) {
                outputFormatOptionMixingError();
                return Result::Fail;
            }
            mSettings.outputFormat = Settings::OutputFormat::xml;
            xmlOptionProvided = true;
        }
        else if (opt == "xml-version") {
            if (outputFormatOptionProvided) {
                outputFormatOptionMixingError();
                return Result::Fail;
            }
            int tmp;
            if (!parseNumberValue(mLogger, "--xml-version=", val, tmp))
                return Result::Fail;
            if (tmp != 2 && tmp != 3) {
                mLogger.printError("'--xml-version' can only be 2 or 3.");
                return Result::Fail;
            }
            mSettings.xml_version = tmp;
            mSettings.outputFormat = Settings::OutputFormat::xml;
            xmlOptionProvided = true;
        }
    }

    for (const auto& f : positionalFiles) {
        mPathNames.emplace_back(Path::fromNativeSeparators(Path::removeQuotationMarks(f)));
    }

    // TODO: bail out?
    if (!executorAuto && mSettings.useSingleJob())
        mLogger.printMessage("'--executor' has no effect as only a single job will be used.");

    // Default template format..
    if (mSettings.templateFormat.empty()) {
        mSettings.templateFormat = "{bold}{file}:{line}:{column}: {red}{inconclusive:{magenta}}{severity}:{inconclusive: inconclusive:}{default} {message} [{id}]{reset}\\n{code}";
        if (mSettings.templateLocation.empty())
            mSettings.templateLocation = "{bold}{file}:{line}:{column}: {dim}note:{reset} {info}\\n{code}";
    }
    // replace static parts of the templates
    substituteTemplateFormatStatic(mSettings.templateFormat, !mSettings.outputFile.empty());
    substituteTemplateLocationStatic(mSettings.templateLocation, !mSettings.outputFile.empty());

    if (debug) {
        mSettings.debugnormal = true;
        mSettings.debugvalueflow = true;
        if (mSettings.verbose) {
            mSettings.debugast = true;
            mSettings.debugsymdb = true;
        }
    }

    if (mSettings.jobs > 1 && mSettings.buildDir.empty()) {
        // TODO: bail out instead?
        if (mSettings.checks.isEnabled(Checks::unusedFunction)) {
            mLogger.printMessage("unusedFunction check requires --cppcheck-build-dir to be active with -j.");
            mSettings.checks.disable(Checks::unusedFunction);
        }
    }

    if (!mSettings.checks.isEnabled(Checks::unusedFunction))
        mSettings.unmatchedSuppressionFilters.emplace_back("unusedFunction");
    if (!mSettings.addons.count("misra"))
        mSettings.unmatchedSuppressionFilters.emplace_back("misra-*");
    if (!mSettings.premium)
        mSettings.unmatchedSuppressionFilters.emplace_back("premium-*");

    if (inputAsFilter) {
        mSettings.fileFilters.insert(mSettings.fileFilters.end(), mPathNames.cbegin(), mPathNames.cend());
        mPathNames.clear();
    }

    if (!mPathNames.empty() && projectType != ImportProject::Type::NONE) {
        mLogger.printError("--project cannot be used in conjunction with source files.");
        return Result::Fail;
    }

    // TODO: conflicts with analyzeAllVsConfigs
    if (!vsConfig.empty()) {
        project.ignoreOtherConfigs(vsConfig);
    }

    if (!platform.empty()) {
        std::string errstr;
        if (!mSettings.platform.set(platform, errstr, lookupPaths, mSettings.debuglookup || mSettings.debuglookupPlatform)) {
            mLogger.printError(errstr);
            return Result::Fail;
        }
    }

    if (defaultSign != '\0')
        mSettings.platform.defaultSign = defaultSign;

    if (!mSettings.analyzeAllVsConfigs) {
        if (projectType != ImportProject::Type::VS_SLN &&
            projectType != ImportProject::Type::VS_SLNX &&
            projectType != ImportProject::Type::VS_VCXPROJ) {
            if (mAnalyzeAllVsConfigsSetOnCmdLine) {
                mLogger.printError("--no-analyze-all-vs-configs has no effect - no Visual Studio project provided.");
                return Result::Fail;
            }
        } else {
            project.selectOneVsConfig(mSettings.platform.type);
        }
    }

    if (!mSettings.buildDir.empty() && !Path::isDirectory(mSettings.buildDir)) {
        mLogger.printError("Directory '" + mSettings.buildDir + "' specified by --cppcheck-build-dir argument has to be existent.");
        return Result::Fail;
    }

    // Print error only if we have "real" command and expect files
    if (mPathNames.empty() && project.guiProject.pathNames.empty() && project.fileSettings.empty()) {
        mLogger.printError("no C or C++ source files found.");
        return Result::Fail;
    }

    for (auto& path : mIgnoredPaths) {
        path = Path::removeQuotationMarks(std::move(path));
        path = Path::fromNativeSeparators(std::move(path));
    }

    if (!project.guiProject.pathNames.empty())
        mPathNames = project.guiProject.pathNames;

    if (!project.fileSettings.empty()) {
        project.ignorePaths(mIgnoredPaths, mSettings.debugignore);
        if (project.fileSettings.empty()) {
            mLogger.printError("no C or C++ source files found.");
            mLogger.printMessage("all paths were ignored");
            return Result::Fail;
        }
        mFileSettings = project.fileSettings;
    }

    if (mSettings.debugnormal && mSettings.outputFormat == Settings::OutputFormat::xml && (mPathNames.size() > 1 || mFileSettings.size() > 1)) {
        mLogger.printError("printing debug output in XML format does not support multiple input files.");
        return Result::Fail;
    }

    // Use paths _pathnames if no base paths for relative path output are given
    if (mSettings.basePaths.empty() && mSettings.relativePaths)
        mSettings.basePaths = mPathNames;

    if (mSettings.keepComments && !mSettings.preprocessOnly) {
        mLogger.printError("-CC may only be used on conjunciton with -E");
        return Result::Fail;
    }

    return Result::Success;
}

void CmdLineParser::printHelp() const
{
    std::ostringstream footer;
    footer << "Example usage:\n"
           << "  # Recursively check the current folder. Print the progress on the screen and\n"
           << "  # write errors to a file:\n"
           << "  cppcheck . 2> err.txt\n"
           << "\n"
           << "  # Recursively check ../myproject/ and don't print progress:\n"
           << "  cppcheck --quiet ../myproject/\n"
           << "\n"
           << "  # Check test.cpp, enable all checks:\n"
           << "  cppcheck --enable=all --inconclusive --library=posix test.cpp\n"
           << "\n"
           << "  # Check f.cpp and search include files from inc1/ and inc2/:\n"
           << "  cppcheck -I inc1/ -I inc2/ f.cpp\n"
           << "\n"
           << "For more information:\n"
           << "    " << mSettings.manualUrl << "\n"
           << "\n"
           << "Many thanks to the 3rd party libraries we use:\n"
           << " * tinyxml2 -- loading project/library/ctu files.\n"
           << " * picojson -- loading compile database.\n"
           << " * pcre2 -- rules.\n"
           << " * qt -- used in GUI";

    rfl::cli::UsageOptions opts;
    opts.detailed = true;
    opts.header = "Cppcheck - A tool for static C/C++ code analysis";
    opts.synopsis = "cppcheck [OPTIONS] [files or paths]";
    opts.description =
        "If a directory is given instead of a filename, *.cpp, *.cxx, *.cc, *.c++, *.c, *.ipp,\n"
        "*.ixx, *.tpp, and *.txx files are checked recursively from the given directory.";
    opts.footer = footer.str();
    if (!mSettings.premium) {
        opts.hidden_groups.emplace_back("Premium");
    }

    const auto text = rfl::cli::usage<CppcheckArgs, CppcheckCliProcessors>("cppcheck", opts);
    mLogger.printRaw(text ? *text : text.error().what());
}

std::string CmdLineParser::getVersion() const {
    // TODO: this should not contain the version - it should set the extraVersion
    if (!mSettings.cppcheckCfgProductName.empty())
        return mSettings.cppcheckCfgProductName;
    const char * const extraVersion = CppCheck::extraVersion();
    if (*extraVersion != '\0')
        return std::string("Cppcheck ") + CppCheck::version() + " ("+ extraVersion + ')';
    return std::string("Cppcheck ") + CppCheck::version();
}

bool CmdLineParser::tryLoadLibrary(Library& destination, const std::string& basepath, const char* filename, bool debug)
{
    const Library::Error err = destination.load(basepath.c_str(), filename, debug);

    if (err.errorcode == Library::ErrorCode::UNKNOWN_ELEMENT)
        mLogger.printMessage("Found unknown elements in configuration file '" + std::string(filename) + "': " + err.reason); // TODO: print as errors
    else if (err.errorcode != Library::ErrorCode::OK) {
        std::string msg = "Failed to load library configuration file '" + std::string(filename) + "'. ";
        switch (err.errorcode) {
        case Library::ErrorCode::OK:
            break;
        case Library::ErrorCode::FILE_NOT_FOUND:
            msg += "File not found";
            break;
        case Library::ErrorCode::BAD_XML:
            msg += "Bad XML";
            break;
        case Library::ErrorCode::UNKNOWN_ELEMENT:
            msg += "Unexpected element";
            break;
        case Library::ErrorCode::MISSING_ATTRIBUTE:
            msg +="Missing attribute";
            break;
        case Library::ErrorCode::BAD_ATTRIBUTE_VALUE:
            msg += "Bad attribute value";
            break;
        case Library::ErrorCode::UNSUPPORTED_FORMAT:
            msg += "File is of unsupported format version";
            break;
        case Library::ErrorCode::DUPLICATE_PLATFORM_TYPE:
            msg += "Duplicate platform type";
            break;
        case Library::ErrorCode::PLATFORM_TYPE_REDEFINED:
            msg += "Platform type redefined";
            break;
        case Library::ErrorCode::DUPLICATE_DEFINE:
            msg += "Duplicate define";
            break;
        }
        if (!err.reason.empty())
            msg += " '" + err.reason + "'";
        mLogger.printMessage(msg); // TODO: print as errors
        return false;
    }
    return true;
}

bool CmdLineParser::loadLibraries(Settings& settings)
{
    if (!tryLoadLibrary(settings.library, settings.exename, "std.cfg", settings.debuglookup || settings.debuglookupLibrary)) {
        const std::string msg("Failed to load std.cfg. Your Cppcheck installation is broken, please re-install.");
#ifdef FILESDIR
        const std::string details("The Cppcheck binary was compiled with FILESDIR set to \""
                                  FILESDIR "\" and will therefore search for "
                                  "std.cfg in " FILESDIR "/cfg.");
#else
        const std::string cfgfolder(Path::fromNativeSeparators(Path::getPathFromFilename(settings.exename)) + "cfg");
        const std::string details("The Cppcheck binary was compiled without FILESDIR set. Either the "
                                  "std.cfg should be available in " + cfgfolder + " or the FILESDIR "
                                  "should be configured.");
#endif
        mLogger.printRaw(msg + " " + details); // TODO: do not print as raw?
        return false;
    }

    bool result = true;
    for (const auto& lib : settings.libraries) {
        if (!tryLoadLibrary(settings.library, settings.exename, lib.c_str(), settings.debuglookup || settings.debuglookupLibrary)) {
            result = false;
        }
    }
    return result;
}

bool CmdLineParser::loadAddons(Settings& settings)
{
    bool result = true;
    for (const std::string &addon: settings.addons) {
        AddonInfo addonInfo;
        const std::string failedToGetAddonInfo = addonInfo.getAddonInfo(addon, settings.exename, settings.debuglookup || settings.debuglookupAddon);
        if (!failedToGetAddonInfo.empty()) {
            mLogger.printRaw(failedToGetAddonInfo); // TODO: do not print as raw
            result = false;
            continue;
        }
        settings.addonInfos.emplace_back(std::move(addonInfo));
    }
    return result;
}

bool CmdLineParser::loadCppcheckCfg()
{
    if (!mSettings.settingsFiles.empty())
    {
        // should never happen - programming error
        mLogger.printError("cppcheck.cfg has already been loaded from " + mSettings.settingsFiles[0]);
        return false;
    }
    const std::string cfgErr = Settings::loadCppcheckCfg(mSettings, mSuppressions, mSettings.debuglookup || mSettings.debuglookupConfig);
    if (!cfgErr.empty()) {
        // TODO: log full path
        mLogger.printError("could not load cppcheck.cfg - " + cfgErr);
        return false;
    }
    mSettings.premium = startsWith(mSettings.cppcheckCfgProductName, "Cppcheck Premium");
    return true;
}

std::list<FileWithDetails> CmdLineParser::filterFiles(const std::vector<std::string>& fileFilters,
                                                      const std::list<FileWithDetails>& filesResolved) {
    std::list<FileWithDetails> files;
    PathMatch filtermatcher(fileFilters, Path::getCurrentPath());
    std::copy_if(filesResolved.cbegin(), filesResolved.cend(), std::inserter(files, files.end()), [&](const FileWithDetails& entry) {
        return filtermatcher.match(entry.path());
    });
    return files;
}

void CmdLineParser::outputFormatOptionMixingError() const
{
    mLogger.printError("'--output-format' and '--xml...' may not be used in conjunction.");
}
