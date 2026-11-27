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

#include "projectfile.h"

#include "common.h"
#include "config.h"
#include "importproject.h"
#include "schemas.h"
#include "settings.h"
#include "utils.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLatin1String>
#include <QRegularExpression>

ProjectFile *ProjectFile::mActiveProject;

namespace {
    void appendNamedAttrs(QStringList &dst, const std::vector<cppcheck::schemas::NamedAttrSchema> &items)
    {
        for (const auto &item : items) {
            if (item.name.value().has_value() && !item.name.value()->empty())
                dst << QString::fromStdString(*item.name.value());
        }
    }

    void appendStrings(QStringList &dst, const std::vector<std::string> &items, bool skipEmpty = true)
    {
        for (const std::string &item : items) {
            if (!skipEmpty || !item.empty())
                dst << QString::fromStdString(item);
        }
    }

    std::vector<cppcheck::schemas::NamedAttrSchema> toNamedAttrs(const QStringList &src)
    {
        std::vector<cppcheck::schemas::NamedAttrSchema> out;
        out.reserve(static_cast<std::size_t>(src.size()));
        for (const QString &s : src) {
            cppcheck::schemas::NamedAttrSchema item;
            item.name = s.toStdString();
            out.push_back(std::move(item));
        }
        return out;
    }

    std::vector<std::string> toStdStrings(const QStringList &src)
    {
        std::vector<std::string> out;
        out.reserve(static_cast<std::size_t>(src.size()));
        for (const QString &s : src)
            out.push_back(s.toStdString());
        return out;
    }
}

ProjectFile::ProjectFile(QObject *parent) :
    QObject(parent)
{
    clear();
}

ProjectFile::ProjectFile(QString filename, QObject *parent) :
    QObject(parent),
    mFilename(std::move(filename))
{
    clear();
    read();
}

void ProjectFile::clear()
{
    const Settings settings;
    clangParser = false;
    mCheckLevel = CheckLevel::normal;
    mRootPath.clear();
    mBuildDir.clear();
    mImportProject.clear();
    mIncludeDirs.clear();
    mDefines.clear();
    mUndefines.clear();
    mUserInclude.clear();
    mPaths.clear();
    mExcludedPaths.clear();
    mLibraries.clear();
    mPlatform.clear();
    mProjectName.clear();
    mSuppressions.clear();
    mAddons.clear();
    mClangAnalyzer = mClangTidy = false;
    mAnalyzeAllVsConfigs = false; // TODO: defaults to true if loading a GUI project via CLI
    mCheckHeaders = true;
    mCheckUnusedTemplates = true;
    mInlineSuppression = true;
    mMaxCtuDepth = settings.maxCtuDepth;
    mMaxTemplateRecursion = settings.maxTemplateRecursion;
    mCheckUnknownFunctionReturn.clear();
    safeChecks.clear();
    mVsConfigurations.clear();
    mTags.clear();
    mWarningTags.clear();

    // Premium
    mBughunting = false;
    mCertIntPrecision = 0;
    mCodingStandards.clear();
}

bool ProjectFile::read(const QString &filename)
{
    if (!filename.isEmpty())
        mFilename = filename;

    QFile file(mFilename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    const QByteArray bytes = file.readAll();
    file.close();

    clear();

    const std::string content(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    std::optional<cppcheck::schemas::ProjectFileSchema> parsed;
    if (mFilename.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)) {
        auto res = cppcheck::schemas::parseProjectFileJson(content);
        if (!res)
            return false;
        parsed = std::move(*res);
    } else {
        std::string err;
        parsed = cppcheck::schemas::parseProjectFileXml(content, /*strictUnknownElements=*/false, err);
        if (!parsed)
            return false;
    }

    const auto &s = *parsed;
    if (s.root.has_value() && s.root->name.value().has_value() && !s.root->name.value()->empty())
        mRootPath = QString::fromStdString(*s.root->name.value());
    if (s.builddir.has_value())
        mBuildDir = QString::fromStdString(*s.builddir);
    if (s.platform.has_value())
        mPlatform = QString::fromStdString(*s.platform);
    if (s.importproject.has_value())
        mImportProject = QString::fromStdString(*s.importproject);
    if (s.analyze_all_vs_configs.value().has_value())
        mAnalyzeAllVsConfigs = *s.analyze_all_vs_configs.value();
    if (s.parser.has_value())
        clangParser = true;
    if (s.check_headers.value().has_value())
        mCheckHeaders = *s.check_headers.value();
    if (s.check_unused_templates.value().has_value())
        mCheckUnusedTemplates = *s.check_unused_templates.value();
    if (s.inline_suppression.value().has_value())
        mInlineSuppression = *s.inline_suppression.value();
    if (s.check_level_exhaustive.value().has_value())
        mCheckLevel = CheckLevel::exhaustive;
    if (s.check_level_normal.value().has_value())
        mCheckLevel = CheckLevel::normal;
    if (s.check_level_reduced.value().has_value())
        mCheckLevel = CheckLevel::reduced;
    if (s.includedir.has_value())
        appendNamedAttrs(mIncludeDirs, s.includedir->dir);
    if (s.defines.has_value())
        appendNamedAttrs(mDefines, s.defines->define);
    if (s.undefines.has_value())
        appendStrings(mUndefines, s.undefines->undefine);
    if (s.user_include.value().has_value())
        mUserInclude = QString::fromStdString(*s.user_include.value());
    if (s.paths.has_value())
        appendNamedAttrs(mPaths, s.paths->dir);
    if (s.exclude.has_value())
        appendNamedAttrs(mExcludedPaths, s.exclude->path);
    if (s.ignore.has_value())
        appendNamedAttrs(mExcludedPaths, s.ignore->path);
    if (s.libraries.has_value())
        appendStrings(mLibraries, s.libraries->library);
    if (s.suppressions.has_value()) {
        for (const auto &entry : s.suppressions->suppression) {
            SuppressionList::Suppression suppression;
            if (entry.fileName.value().has_value())
                suppression.fileName = *entry.fileName.value();
            if (entry.lineNumber.value().has_value())
                suppression.lineNumber = *entry.lineNumber.value();
            if (entry.symbolName.value().has_value())
                suppression.symbolName = *entry.symbolName.value();
            if (entry.hash.value().has_value())
                suppression.hash = *entry.hash.value();
            if (entry.errorId.value().has_value())
                suppression.errorId = *entry.errorId.value();
            mSuppressions << suppression;
        }
    }
    if (s.check_unknown_function_return_values.value().has_value())
        appendStrings(mCheckUnknownFunctionReturn, s.check_unknown_function_return_values.value()->name);
    if (s.safe_checks.value().has_value()) {
        const auto &sc = *s.safe_checks.value();
        safeChecks.classes = sc.class_public.value().has_value();
        safeChecks.externalFunctions = sc.external_functions.value().has_value();
        safeChecks.internalFunctions = sc.internal_functions.value().has_value();
        safeChecks.externalVariables = sc.external_variables.value().has_value();
    }
    if (s.addons.has_value())
        appendStrings(mAddons, s.addons->addon);
    if (s.tools.has_value()) {
        QStringList tools;
        appendStrings(tools, s.tools->tool);
        mClangAnalyzer = tools.contains(CLANG_ANALYZER);
        mClangTidy = tools.contains(CLANG_TIDY);
    }
    if (s.tags.has_value())
        appendStrings(mTags, s.tags->tag);
    if (s.tag_warnings.value().has_value()) {
        for (const auto &tw : *s.tag_warnings.value()) {
            const QString tag = tw.tag.value().has_value() ? QString::fromStdString(*tw.tag.value()) : QString();
            for (const auto &w : tw.warning) {
                if (w.hash.value().has_value())
                    mWarningTags[*w.hash.value()] = tag;
            }
        }
    }
    if (s.max_ctu_depth.value().has_value())
        mMaxCtuDepth = *s.max_ctu_depth.value();
    if (s.max_template_recursion.value().has_value())
        mMaxTemplateRecursion = *s.max_template_recursion.value();
    if (s.vs_configurations.value().has_value())
        appendStrings(mVsConfigurations, s.vs_configurations.value()->config, /*skipEmpty=*/false);
    if (s.bug_hunting.value().has_value())
        mBughunting = true;
    if (s.coding_standards.value().has_value())
        appendStrings(mCodingStandards, s.coding_standards.value()->coding_standard.value());
    if (s.cert_c_int_precision.value().has_value())
        mCertIntPrecision = *s.cert_c_int_precision.value();
    if (s.project_name.value().has_value())
        mProjectName = QString::fromStdString(*s.project_name.value());

    return true;
}

void ProjectFile::setIncludes(const QStringList &includes)
{
    mIncludeDirs = includes;
}

void ProjectFile::setDefines(const QStringList &defines)
{
    mDefines = defines;
}

void ProjectFile::setUndefines(const QStringList &undefines)
{
    mUndefines = undefines;
}

void ProjectFile::setCheckPaths(const QStringList &paths)
{
    mPaths = paths;
}

void ProjectFile::setExcludedPaths(const QStringList &paths)
{
    mExcludedPaths = paths;
}

void ProjectFile::setLibraries(const QStringList &libraries)
{
    mLibraries = libraries;
}

void ProjectFile::setPlatform(const QString &platform)
{
    mPlatform = platform;
}

QList<SuppressionList::Suppression> ProjectFile::getCheckingSuppressions() const
{
    const QRegularExpression re1("^[a-zA-Z0-9_\\-]+/.*");
    const QRegularExpression re2("^[^/]+$");
    QList<SuppressionList::Suppression> result;
    for (SuppressionList::Suppression suppression : mSuppressions) {
        if (re1.match(suppression.fileName.c_str()).hasMatch() || re2.match(suppression.fileName.c_str()).hasMatch()) {
            if (suppression.fileName[0] != '*')
                suppression.fileName = QFileInfo(mFilename).absolutePath().toStdString() + "/" + suppression.fileName;
        }
        result << suppression;
    }
    return result;
}

void ProjectFile::setSuppressions(const QList<SuppressionList::Suppression> &suppressions)
{
    mSuppressions = suppressions;
}

void ProjectFile::addSuppression(const SuppressionList::Suppression &suppression)
{
    mSuppressions.append(suppression);
}

void ProjectFile::setAddons(const QStringList &addons)
{
    mAddons = addons;
}

void ProjectFile::setVSConfigurations(const QStringList &vsConfigs)
{
    mVsConfigurations = vsConfigs;
}

void ProjectFile::setCheckLevel(ProjectFile::CheckLevel checkLevel)
{
    mCheckLevel = checkLevel;
}

void ProjectFile::setWarningTags(std::size_t hash, const QString& tags)
{
    if (tags.isEmpty())
        mWarningTags.erase(hash);
    else if (hash > 0)
        mWarningTags[hash] = tags;
}

QString ProjectFile::getWarningTags(std::size_t hash) const
{
    auto it = mWarningTags.find(hash);
    return (it != mWarningTags.end()) ? it->second : QString();
}

bool ProjectFile::write(const QString &filename)
{
    if (!filename.isEmpty())
        mFilename = filename;

    QFile file(mFilename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    cppcheck::schemas::ProjectFileSchema s;
    s.version = std::string(CppcheckXml::ProjectFileVersion);

    if (!mRootPath.isEmpty()) {
        cppcheck::schemas::NamedAttrSchema r;
        r.name = mRootPath.toStdString();
        s.root = std::move(r);
    }
    if (!mBuildDir.isEmpty())
        s.builddir = mBuildDir.toStdString();
    if (!mPlatform.isEmpty())
        s.platform = mPlatform.toStdString();
    if (!mImportProject.isEmpty())
        s.importproject = mImportProject.toStdString();

    s.analyze_all_vs_configs = mAnalyzeAllVsConfigs;
    if (clangParser)
        s.parser = std::string("clang");
    s.check_headers = mCheckHeaders;
    s.check_unused_templates = mCheckUnusedTemplates;
    s.inline_suppression = mInlineSuppression;
    s.max_ctu_depth = mMaxCtuDepth;
    s.max_template_recursion = mMaxTemplateRecursion;

    if (!mIncludeDirs.isEmpty())
        s.includedir = cppcheck::schemas::IncludeDirListSchema{toNamedAttrs(mIncludeDirs)};
    if (!mDefines.isEmpty())
        s.defines = cppcheck::schemas::DefinesListSchema{toNamedAttrs(mDefines)};
    if (!mUserInclude.isEmpty())
        s.user_include = mUserInclude.toStdString();
    if (!mVsConfigurations.isEmpty())
        s.vs_configurations = cppcheck::schemas::VsConfigsSchema{toStdStrings(mVsConfigurations)};
    if (!mUndefines.isEmpty())
        s.undefines = cppcheck::schemas::UndefinesListSchema{toStdStrings(mUndefines)};
    if (!mPaths.isEmpty())
        s.paths = cppcheck::schemas::PathsListSchema{toNamedAttrs(mPaths)};
    if (!mExcludedPaths.isEmpty())
        s.exclude = cppcheck::schemas::ExcludeListSchema{toNamedAttrs(mExcludedPaths)};
    if (!mLibraries.isEmpty())
        s.libraries = cppcheck::schemas::LibrariesListSchema{toStdStrings(mLibraries)};

    if (!mSuppressions.isEmpty()) {
        cppcheck::schemas::ProjectSuppressionsSchema supprs;
        for (const SuppressionList::Suppression &suppression : utils::as_const(mSuppressions)) {
            cppcheck::schemas::ProjectSuppressionSchema entry;
            if (!suppression.fileName.empty())
                entry.fileName = suppression.fileName;
            if (suppression.lineNumber > 0)
                entry.lineNumber = suppression.lineNumber;
            if (!suppression.symbolName.empty())
                entry.symbolName = suppression.symbolName;
            if (suppression.hash > 0)
                entry.hash = suppression.hash;
            if (!suppression.errorId.empty())
                entry.errorId = suppression.errorId;
            supprs.suppression.push_back(std::move(entry));
        }
        s.suppressions = std::move(supprs);
    }

    if (!mCheckUnknownFunctionReturn.isEmpty())
        s.check_unknown_function_return_values = cppcheck::schemas::CheckUnknownFunctionReturnSchema{toStdStrings(mCheckUnknownFunctionReturn)};

    if (safeChecks.classes || safeChecks.externalFunctions || safeChecks.internalFunctions || safeChecks.externalVariables) {
        cppcheck::schemas::SafeChecksSchema sc;
        if (safeChecks.classes)
            sc.class_public = cppcheck::schemas::EmptyElement{};
        if (safeChecks.externalFunctions)
            sc.external_functions = cppcheck::schemas::EmptyElement{};
        if (safeChecks.internalFunctions)
            sc.internal_functions = cppcheck::schemas::EmptyElement{};
        if (safeChecks.externalVariables)
            sc.external_variables = cppcheck::schemas::EmptyElement{};
        s.safe_checks = std::move(sc);
    }

    if (!mAddons.isEmpty())
        s.addons = cppcheck::schemas::AddonsListSchema{toStdStrings(mAddons)};

    QStringList tools;
    if (mClangAnalyzer)
        tools << CLANG_ANALYZER;
    if (mClangTidy)
        tools << CLANG_TIDY;
    if (!tools.isEmpty())
        s.tools = cppcheck::schemas::ToolsListSchema{toStdStrings(tools)};

    if (!mTags.isEmpty())
        s.tags = cppcheck::schemas::TagsListSchema{toStdStrings(mTags)};

    if (!mWarningTags.empty()) {
        QStringList tags;
        for (const auto &wt : mWarningTags) {
            if (!tags.contains(wt.second))
                tags.append(wt.second);
        }
        std::vector<cppcheck::schemas::TagWarningsSchema> twList;
        for (const QString &tag : utils::as_const(tags)) {
            cppcheck::schemas::TagWarningsSchema tw;
            tw.tag = tag.toStdString();
            for (const auto &wt : mWarningTags) {
                if (wt.second == tag) {
                    cppcheck::schemas::TagWarningEntrySchema w;
                    w.hash = wt.first;
                    tw.warning.push_back(std::move(w));
                }
            }
            twList.push_back(std::move(tw));
        }
        s.tag_warnings = std::move(twList);
    }

    switch (mCheckLevel) {
    case CheckLevel::reduced:
        s.check_level_reduced = cppcheck::schemas::EmptyElement{};
        break;
    case CheckLevel::normal:
        s.check_level_normal = cppcheck::schemas::EmptyElement{};
        break;
    case CheckLevel::exhaustive:
        s.check_level_exhaustive = cppcheck::schemas::EmptyElement{};
        break;
    }

    if (mBughunting)
        s.bug_hunting = cppcheck::schemas::EmptyElement{};
    if (!mCodingStandards.isEmpty())
        s.coding_standards = cppcheck::schemas::CodingStandardsListSchema{toStdStrings(mCodingStandards)};
    if (mCertIntPrecision > 0)
        s.cert_c_int_precision = mCertIntPrecision;
    if (!mProjectName.isEmpty())
        s.project_name = mProjectName.toStdString();

    const std::string output = mFilename.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)
                                   ? cppcheck::schemas::writeProjectFileJson(s)
                                   : cppcheck::schemas::writeProjectFileXml(s);
    file.write(output.data(), static_cast<qint64>(output.size()));
    file.close();
    return true;
}

void ProjectFile::setSettingsUserIncludes(Settings &settings) const
{
    if (!mUserInclude.isEmpty())
        settings.userIncludes.push_back(mUserInclude.toStdString());
}

QStringList ProjectFile::fromNativeSeparators(const QStringList &paths)
{
    QStringList ret;
    for (const QString &path : paths)
        ret << QDir::fromNativeSeparators(path);
    return ret;
}

QStringList ProjectFile::getAddonsAndTools() const
{
    QStringList ret(mAddons);
    if (mClangAnalyzer)
        ret << CLANG_ANALYZER;
    if (mClangTidy)
        ret << CLANG_TIDY;
    return ret;
}

QString ProjectFile::getAddonFilePath(QString filesDir, const QString &addon)
{
    if (QFile(addon).exists())
        return addon;

    if (!filesDir.endsWith("/"))
        filesDir += "/";

    QStringList searchPaths;
    searchPaths << filesDir << (filesDir + "addons/") << (filesDir + "../addons/")
#ifdef FILESDIR
        << (QLatin1String(FILESDIR) + "/addons/")
#endif
    ;

    for (const QString& path : utils::as_const(searchPaths)) {
        QString f = path + addon + ".py";
        if (QFile(f).exists())
            return f;
    }

    return QString();
}

QStringList ProjectFile::getSearchPaths(const QString& projectPath, const QString& appPath, const QString& datadir, const QString& dir) {
    QStringList ret;
    ret << appPath << (appPath + "/" + dir) << projectPath;
#ifdef FILESDIR
    if (FILESDIR[0])
        ret << FILESDIR << (FILESDIR "/" + dir);
#endif
    if (!datadir.isEmpty())
        ret << datadir << (datadir + "/" + dir);
    return ret;
}

QString ProjectFile::getRelativePath(const QString &absolutePath) const
{
    const QDir dir(QFileInfo(mFilename).absolutePath());
    const QString relativePath(dir.relativeFilePath(absolutePath));
    if (relativePath.startsWith("../../..") || absolutePath.length() < relativePath.length())
        return absolutePath;
    return relativePath;
}

QStringList ProjectFile::getSearchPaths(const QString& dir) const {
    const QFileInfo inf(mFilename);
    const QString applicationFilePath = QCoreApplication::applicationFilePath();
    const QString appPath = QFileInfo(applicationFilePath).canonicalPath();
    const QString datadir = getDataDir();
    return getSearchPaths(inf.canonicalPath(), appPath, datadir, dir);
}
