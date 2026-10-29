#include "services/pluginmanager.h"
#include "services/fileoperations.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QUrl>
#include <QDebug>
#include <QVector>
#include <QMimeDatabase>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>
#include <utility>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace {
QString safeIconName(const QString &name)
{
    static const QRegularExpression validName(QStringLiteral("^[A-Za-z][A-Za-z0-9]*$"));
    return validName.match(name).hasMatch() ? name : QString();
}

bool copyDirectoryContents(const QString &sourcePath, const QString &destinationPath)
{
    QDir source(sourcePath);
    if (!source.exists())
        return false;
    if (QDir::cleanPath(source.absolutePath())
        == QDir::cleanPath(QFileInfo(destinationPath).absoluteFilePath()))
        return true;
    if (!QDir().mkpath(destinationPath))
        return false;
    bool success = true;
    const QFileInfoList entries = source.entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
    for (const QFileInfo &entry : entries) {
        const QString destination = QDir(destinationPath).filePath(entry.fileName());
        if (entry.isDir() && !entry.isSymLink()) {
            if (!copyDirectoryContents(entry.absoluteFilePath(), destination))
                success = false;
        } else if (!QFileInfo::exists(destination)
                   && !QFile::copy(entry.absoluteFilePath(), destination)) {
            success = false;
        }
    }
    return success;
}

void copyLegacyFile(const QString &sourcePath, const QString &destinationPath)
{
    if (!QFileInfo::exists(destinationPath) && QFileInfo::exists(sourcePath)) {
        QDir().mkpath(QFileInfo(destinationPath).absolutePath());
        QFile::copy(sourcePath, destinationPath);
    }
}

QString pluginIdFromLua(lua_State *state) { return QString::fromUtf8(lua_tostring(state, lua_upvalueindex(2))); }
PluginManager *managerFromLua(lua_State *state) { return static_cast<PluginManager *>(lua_touserdata(state, lua_upvalueindex(1))); }
int pluginLuaLog(lua_State *state)
{
    qInfo().noquote() << "Aether plugin:" << pluginIdFromLua(state) << QString::fromUtf8(luaL_checkstring(state, 1));
    return 0;
}
int pluginLuaAddAction(lua_State *state)
{
    const QString label = QString::fromUtf8(luaL_checkstring(state, 1));
    luaL_checktype(state, 2, LUA_TFUNCTION);
    lua_pushvalue(state, 2);
    const int callback = luaL_ref(state, LUA_REGISTRYINDEX);
    managerFromLua(state)->registerAction(pluginIdFromLua(state), label, callback);
    return 0;
}
int pluginLuaAddContextMenu(lua_State *state)
{
    luaL_checktype(state, 1, LUA_TTABLE);
    PluginManager::ContextMenuContribution menu;
    lua_getfield(state, 1, "label");
    menu.label = QString::fromUtf8(luaL_checkstring(state, -1));
    lua_pop(state, 1);
    lua_getfield(state, 1, "icon");
    if (lua_isstring(state, -1)) menu.icon = safeIconName(QString::fromUtf8(lua_tostring(state, -1)));
    lua_pop(state, 1);
    auto readStringArray = [state](const char *field) {
        QStringList values;
        lua_getfield(state, 1, field);
        if (lua_istable(state, -1)) {
            const size_t count = lua_rawlen(state, -1);
            for (size_t i = 1; i <= count; ++i) {
                lua_rawgeti(state, -1, static_cast<lua_Integer>(i));
                if (lua_isstring(state, -1)) values.append(QString::fromUtf8(lua_tostring(state, -1)));
                lua_pop(state, 1);
            }
        }
        lua_pop(state, 1);
        return values;
    };
    menu.extensions = readStringArray("extensions");
    menu.mimeTypes = readStringArray("mimeTypes");
    menu.contexts = readStringArray("contexts");
    if (menu.contexts.isEmpty()) menu.contexts.append(QStringLiteral("file"));
    lua_getfield(state, 1, "actions");
    if (!lua_istable(state, -1)) return luaL_error(state, "addContextMenu requires an actions list");
    const size_t count = lua_rawlen(state, -1);
    for (size_t i = 1; i <= count; ++i) {
        lua_rawgeti(state, -1, static_cast<lua_Integer>(i));
        lua_getfield(state, -1, "label");
        const QString label = QString::fromUtf8(luaL_checkstring(state, -1));
        lua_pop(state, 1);
        lua_getfield(state, -1, "icon");
        const QString icon = lua_isstring(state, -1) ? safeIconName(QString::fromUtf8(lua_tostring(state, -1))) : QString();
        lua_pop(state, 1);
        lua_getfield(state, -1, "run");
        luaL_checktype(state, -1, LUA_TFUNCTION);
        lua_pushvalue(state, -1);
        const int ref = luaL_ref(state, LUA_REGISTRYINDEX);
        lua_pop(state, 1);
        menu.actions.append({label, icon, ref});
        lua_pop(state, 1);
    }
    lua_pop(state, 1);
    static const QStringList validContexts = {QStringLiteral("file"), QStringLiteral("directory"),
        QStringLiteral("empty"), QStringLiteral("sidebar")};
    for (const QString &context : menu.contexts)
        if (!validContexts.contains(context)) return luaL_error(state, "unknown context type");
    if (menu.label.isEmpty() || menu.actions.isEmpty()
        || (menu.contexts.contains(QStringLiteral("file")) && menu.extensions.isEmpty() && menu.mimeTypes.isEmpty()))
        return luaL_error(state, "context menu needs a label, actions, and file type filters when targeting files");
    managerFromLua(state)->registerContextMenu(pluginIdFromLua(state), std::move(menu));
    return 0;
}
int pluginLuaRun(lua_State *state)
{
    if (!managerFromLua(state)->hasPermission(pluginIdFromLua(state), QStringLiteral("process"))) {
        lua_pushboolean(state, false);
        return 1;
    }
    const QString program = QString::fromUtf8(luaL_checkstring(state, 1));
    luaL_checktype(state, 2, LUA_TTABLE);
    QStringList args;
    const size_t count = lua_rawlen(state, 2);
    for (size_t i = 1; i <= count; ++i) {
        lua_rawgeti(state, 2, static_cast<lua_Integer>(i));
        args.append(QString::fromUtf8(luaL_checkstring(state, -1)));
        lua_pop(state, 1);
    }
    lua_pushboolean(state, managerFromLua(state)->runProgram(program, args));
    return 1;
}
void pushVariant(lua_State *state, const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const auto map = value.toMap();
        lua_createtable(state, 0, map.size());
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            pushVariant(state, it.value());
            lua_setfield(state, -2, it.key().toUtf8().constData());
        }
    } else if (value.metaType().id() == QMetaType::QVariantList) {
        const auto list = value.toList();
        lua_createtable(state, list.size(), 0);
        for (int i = 0; i < list.size(); ++i) {
            pushVariant(state, list.at(i));
            lua_rawseti(state, -2, i + 1);
        }
    } else if (value.metaType().id() == QMetaType::Bool) {
        lua_pushboolean(state, value.toBool());
    } else if (value.metaType().id() == QMetaType::Int || value.metaType().id() == QMetaType::UInt
               || value.metaType().id() == QMetaType::LongLong || value.metaType().id() == QMetaType::ULongLong) {
        lua_pushinteger(state, value.toLongLong());
    } else {
        const QByteArray bytes = value.toString().toUtf8();
        lua_pushlstring(state, bytes.constData(), bytes.size());
    }
}
int pluginLuaFileInfo(lua_State *state)
{
    if (!managerFromLua(state)->hasPermission(pluginIdFromLua(state), QStringLiteral("filesystem.read"))) { lua_pushnil(state); return 1; }
    const QString path = QString::fromUtf8(luaL_checkstring(state, 1));
    pushVariant(state, managerFromLua(state)->fileInfo(path));
    return 1;
}
int pluginLuaListDirectory(lua_State *state)
{
    if (!managerFromLua(state)->hasPermission(pluginIdFromLua(state), QStringLiteral("filesystem.read"))) { lua_pushnil(state); return 1; }
    const QString path = QString::fromUtf8(luaL_checkstring(state, 1));
    pushVariant(state, managerFromLua(state)->listDirectory(path));
    return 1;
}
QStringList luaStringArray(lua_State *state, int index)
{
    QStringList values;
    if (lua_isstring(state, index)) {
        values.append(QString::fromUtf8(lua_tostring(state, index)));
        return values;
    }
    luaL_checktype(state, index, LUA_TTABLE);
    const size_t count = lua_rawlen(state, index);
    for (size_t i = 1; i <= count; ++i) {
        lua_rawgeti(state, index, static_cast<lua_Integer>(i));
        values.append(QString::fromUtf8(luaL_checkstring(state, -1)));
        lua_pop(state, 1);
    }
    return values;
}
int pluginLuaOpen(lua_State *state)
{
    if (!managerFromLua(state)->hasPermission(pluginIdFromLua(state), QStringLiteral("filesystem.read"))) return 0;
    managerFromLua(state)->openPath(QString::fromUtf8(luaL_checkstring(state, 1)));
    return 0;
}
int pluginLuaRename(lua_State *state)
{
    if (!managerFromLua(state)->hasPermission(pluginIdFromLua(state), QStringLiteral("filesystem.write"))) { lua_pushboolean(state, false); return 1; }
    const QString path = QString::fromUtf8(luaL_checkstring(state, 1));
    const QString name = QString::fromUtf8(luaL_checkstring(state, 2));
    lua_pushboolean(state, managerFromLua(state)->renamePath(path, name));
    return 1;
}
int pluginLuaCopy(lua_State *state)
{
    if (!managerFromLua(state)->hasPermission(pluginIdFromLua(state), QStringLiteral("filesystem.write"))) { lua_pushinteger(state, -1); return 1; }
    const QStringList paths = luaStringArray(state, 1);
    const QString destination = QString::fromUtf8(luaL_checkstring(state, 2));
    lua_pushinteger(state, managerFromLua(state)->copyPaths(paths, destination));
    return 1;
}
int pluginLuaMove(lua_State *state)
{
    if (!managerFromLua(state)->hasPermission(pluginIdFromLua(state), QStringLiteral("filesystem.write"))) { lua_pushinteger(state, -1); return 1; }
    const QStringList paths = luaStringArray(state, 1);
    const QString destination = QString::fromUtf8(luaL_checkstring(state, 2));
    lua_pushinteger(state, managerFromLua(state)->movePaths(paths, destination));
    return 1;
}
int pluginLuaTrash(lua_State *state)
{
    if (!managerFromLua(state)->hasPermission(pluginIdFromLua(state), QStringLiteral("filesystem.write"))) { lua_pushinteger(state, -1); return 1; }
    lua_pushinteger(state, managerFromLua(state)->trashPaths(luaStringArray(state, 1)));
    return 1;
}
int pluginLuaGetSetting(lua_State *state)
{
    const QString id = pluginIdFromLua(state);
    const QString key = QString::fromUtf8(luaL_checkstring(state, 1));
    QVariant fallback;
    if (lua_gettop(state) >= 2) {
        if (lua_isboolean(state, 2)) fallback = bool(lua_toboolean(state, 2));
        else if (lua_isnumber(state, 2)) fallback = lua_tonumber(state, 2);
        else if (lua_isstring(state, 2)) fallback = QString::fromUtf8(lua_tostring(state, 2));
    }
    pushVariant(state, managerFromLua(state)->setting(id, key, fallback));
    return 1;
}
int pluginLuaSetSetting(lua_State *state)
{
    const QString key = QString::fromUtf8(luaL_checkstring(state, 1));
    QVariant value;
    if (lua_isboolean(state, 2)) value = bool(lua_toboolean(state, 2));
    else if (lua_isnumber(state, 2)) value = lua_tonumber(state, 2);
    else if (lua_isstring(state, 2)) value = QString::fromUtf8(lua_tostring(state, 2));
    else return luaL_error(state, "setting values must be strings, numbers, or booleans");
    lua_pushboolean(state, managerFromLua(state)->setSetting(pluginIdFromLua(state), key, value));
    return 1;
}
int pluginLuaAddSettingsSection(lua_State *state)
{
    luaL_checktype(state, 1, LUA_TTABLE);
    PluginManager::SettingsSection section;
    lua_getfield(state, 1, "title");
    section.title = QString::fromUtf8(luaL_checkstring(state, -1));
    lua_pop(state, 1);
    lua_getfield(state, 1, "description");
    if (lua_isstring(state, -1)) section.description = QString::fromUtf8(lua_tostring(state, -1));
    lua_pop(state, 1);
    lua_getfield(state, 1, "controls");
    if (!lua_istable(state, -1)) return luaL_error(state, "addSettingsSection requires controls");
    const size_t count = lua_rawlen(state, -1);
    for (size_t i = 1; i <= count; ++i) {
        lua_rawgeti(state, -1, static_cast<lua_Integer>(i));
        QVariantMap control;
        for (const char *field : {"type", "key", "label", "description", "placeholder"}) {
            lua_getfield(state, -1, field);
            if (lua_isstring(state, -1)) control.insert(QString::fromLatin1(field), QString::fromUtf8(lua_tostring(state, -1)));
            lua_pop(state, 1);
        }
        lua_getfield(state, -1, "options");
        if (lua_istable(state, -1)) {
            QVariantList options;
            const size_t n = lua_rawlen(state, -1);
            for (size_t j = 1; j <= n; ++j) {
                lua_rawgeti(state, -1, static_cast<lua_Integer>(j));
                options.append(QString::fromUtf8(luaL_checkstring(state, -1)));
                lua_pop(state, 1);
            }
            control.insert(QStringLiteral("options"), options);
        }
        lua_pop(state, 1);
        lua_getfield(state, -1, "default");
        if (lua_isboolean(state, -1)) control.insert(QStringLiteral("default"), bool(lua_toboolean(state, -1)));
        else if (lua_isnumber(state, -1)) control.insert(QStringLiteral("default"), lua_tonumber(state, -1));
        else if (lua_isstring(state, -1)) control.insert(QStringLiteral("default"), QString::fromUtf8(lua_tostring(state, -1)));
        lua_pop(state, 1);
        control.insert(QStringLiteral("value"), managerFromLua(state)->setting(pluginIdFromLua(state),
            control.value(QStringLiteral("key")).toString(), control.value(QStringLiteral("default"))));
        section.controls.append(control);
        lua_pop(state, 1);
    }
    lua_pop(state, 1);
    if (section.title.isEmpty() || section.controls.isEmpty()) return luaL_error(state, "settings section needs a title and controls");
    managerFromLua(state)->registerSettingsSection(pluginIdFromLua(state), std::move(section));
    return 0;
}
int pluginLuaAddToolbarButton(lua_State *state)
{
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_getfield(state, 1, "label");
    const QString label = QString::fromUtf8(luaL_checkstring(state, -1)); lua_pop(state, 1);
    lua_getfield(state, 1, "icon");
    const QString icon = lua_isstring(state, -1) ? safeIconName(QString::fromUtf8(lua_tostring(state, -1))) : QString(); lua_pop(state, 1);
    lua_getfield(state, 1, "run"); luaL_checktype(state, -1, LUA_TFUNCTION);
    lua_pushvalue(state, -1); const int ref = luaL_ref(state, LUA_REGISTRYINDEX); lua_pop(state, 1);
    managerFromLua(state)->registerToolbarButton(pluginIdFromLua(state), label, icon, ref);
    return 0;
}
int pluginLuaAddShortcut(lua_State *state)
{
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_getfield(state, 1, "name"); const QString name = QString::fromUtf8(luaL_checkstring(state, -1)); lua_pop(state, 1);
    lua_getfield(state, 1, "shortcut"); const QString sequence = QString::fromUtf8(luaL_checkstring(state, -1)); lua_pop(state, 1);
    lua_getfield(state, 1, "run"); luaL_checktype(state, -1, LUA_TFUNCTION);
    lua_pushvalue(state, -1); const int ref = luaL_ref(state, LUA_REGISTRYINDEX); lua_pop(state, 1);
    managerFromLua(state)->registerShortcut(pluginIdFromLua(state), name, sequence, ref);
    return 0;
}
int pluginLuaOn(lua_State *state)
{
    const QString eventName = QString::fromUtf8(luaL_checkstring(state, 1));
    luaL_checktype(state, 2, LUA_TFUNCTION);
    lua_pushvalue(state, 2); const int ref = luaL_ref(state, LUA_REGISTRYINDEX);
    managerFromLua(state)->registerEventHandler(pluginIdFromLua(state), eventName, ref);
    return 0;
}
}

PluginManager::PluginManager(FileOperations *fileOperations, const QString &configDirectory,
                             QObject *parent)
    : QAbstractListModel(parent), m_fileOperations(fileOperations)
{
    const QString legacyConfigDirectory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    m_directory = QDir(configDirectory).filePath(QStringLiteral("plugins"));
    m_stateFile = QDir(configDirectory).filePath(QStringLiteral("plugins-enabled.json"));
    m_settingsFile = QDir(configDirectory).filePath(QStringLiteral("plugin-settings.json"));
    m_permissionsFile = QDir(configDirectory).filePath(QStringLiteral("plugin-permissions.json"));

    // Move the user's existing plugin setup forward from Qt's previous
    // per-application config location without overwriting anything already
    // present in the new Aether config directory.
    copyDirectoryContents(QDir(legacyConfigDirectory).filePath(QStringLiteral("plugins")), m_directory);
    copyLegacyFile(QDir(legacyConfigDirectory).filePath(QStringLiteral("plugins-enabled.json")), m_stateFile);
    copyLegacyFile(QDir(legacyConfigDirectory).filePath(QStringLiteral("plugin-settings.json")), m_settingsFile);
    copyLegacyFile(QDir(legacyConfigDirectory).filePath(QStringLiteral("plugin-permissions.json")), m_permissionsFile);
    QFile settings(m_settingsFile);
    if (settings.open(QIODevice::ReadOnly)) m_settingValues = QJsonDocument::fromJson(settings.readAll()).object();
    QFile permissions(m_permissionsFile);
    if (permissions.open(QIODevice::ReadOnly)) m_permissionValues = QJsonDocument::fromJson(permissions.readAll()).object();
    QDir().mkpath(m_directory);
    refresh();
}

PluginManager::~PluginManager()
{
    if (m_repositoryProcess && m_repositoryProcess->state() != QProcess::NotRunning) {
        m_repositoryProcess->kill();
        m_repositoryProcess->waitForFinished(1000);
    }
    if (!m_repositoryStagingPath.isEmpty())
        QDir(m_repositoryStagingPath).removeRecursively();
    const auto ids = m_runtimes.keys();
    for (const auto &id : ids) stopPlugin(id);
}

int PluginManager::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_plugins.size(); }

QVariant PluginManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_plugins.size()) return {};
    const auto &p = m_plugins.at(index.row());
    switch (role) {
    case IdRole: return p.id; case NameRole: return p.name; case VersionRole: return p.version;
    case DescriptionRole: return p.description; case AuthorRole: return p.author;
    case EntryRole: return QUrl::fromLocalFile(p.entry); case EnabledRole: return p.enabled;
    case ErrorRole: return p.error; case ActionsRole: return p.actions; case SettingsRole: return p.settings;
    case PermissionsRole: {
        QVariantList requested;
        const QJsonObject allowed = m_permissionValues.value(p.id).toObject();
        for (const QString &permission : p.permissions)
            requested.append(QVariantMap{{QStringLiteral("name"), permission},
                {QStringLiteral("enabled"), allowed.value(permission).toBool(false)}});
        return requested;
    }
    default: return {};
    }
}

QHash<int, QByteArray> PluginManager::roleNames() const
{
    return {{IdRole,"pluginId"},{NameRole,"name"},{VersionRole,"version"},
            {DescriptionRole,"description"},{AuthorRole,"author"},{EntryRole,"entry"},
            {EnabledRole,"enabled"},{ErrorRole,"error"},{ActionsRole,"actions"},{SettingsRole,"settings"},
            {PermissionsRole,"permissions"}};
}

QString PluginManager::pluginDirectory() const { return m_directory; }
bool PluginManager::installingRepository() const { return m_repositoryProcess != nullptr; }
QString PluginManager::repositoryInstallStatus() const { return m_repositoryInstallStatus; }

void PluginManager::finishRepositoryInstall(bool success, const QString &message)
{
    const QString stagingPath = std::exchange(m_repositoryStagingPath, QString());
    if (!success && !stagingPath.isEmpty())
        QDir(stagingPath).removeRecursively();
    m_repositoryInstallStatus = message;
    emit repositoryInstallChanged();
}

bool PluginManager::installFromRepository(const QString &rawUrl)
{
    if (m_repositoryProcess)
        return false;

    const QUrl url(rawUrl.trimmed());
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("ssh"))) {
        m_repositoryInstallStatus = QStringLiteral("Enter a valid HTTPS or SSH Git repository URL.");
        emit repositoryInstallChanged();
        return false;
    }

    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty()) {
        m_repositoryInstallStatus = QStringLiteral("Git is required to install a plugin repository.");
        emit repositoryInstallChanged();
        return false;
    }

    QDir().mkpath(m_directory);
    m_repositoryStagingPath = QDir(m_directory).filePath(
        QStringLiteral(".install-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    auto *process = new QProcess(this);
    m_repositoryProcess = process;
    m_repositoryInstallStatus = QStringLiteral("Cloning plugin repository…");
    emit repositoryInstallChanged();

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    process->setProcessEnvironment(environment);
    process->setProgram(git);
    process->setArguments({QStringLiteral("clone"), QStringLiteral("--depth"), QStringLiteral("1"),
                           QStringLiteral("--"), url.toString(), m_repositoryStagingPath});
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString errorOutput = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
        m_repositoryProcess = nullptr;
        process->deleteLater();

        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            finishRepositoryInstall(false, errorOutput.isEmpty()
                ? QStringLiteral("Git could not clone this repository.") : errorOutput);
            return;
        }

        const QDir staging(m_repositoryStagingPath);
        QFile manifest(staging.filePath(QStringLiteral("plugin.json")));
        if (!manifest.open(QIODevice::ReadOnly)) {
            finishRepositoryInstall(false, QStringLiteral("Repository has no plugin.json at its root."));
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll(), &parseError);
        const QJsonObject object = document.object();
        const QString id = object.value(QStringLiteral("id")).toString();
        static const QRegularExpression validPluginId(QStringLiteral("^[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}$"));
        if (parseError.error != QJsonParseError::NoError || !document.isObject()
            || !validPluginId.match(id).hasMatch()) {
            finishRepositoryInstall(false, QStringLiteral("Repository has an invalid plugin.json id."));
            return;
        }

        const QString entryName = object.value(QStringLiteral("entry")).toString(QStringLiteral("main.lua"));
        const QFileInfo entryInfo(staging.filePath(entryName));
        const QString stagingCanonical = QFileInfo(m_repositoryStagingPath).canonicalFilePath();
        const QString entryCanonical = entryInfo.canonicalFilePath();
        if (stagingCanonical.isEmpty() || !entryInfo.isFile() || entryCanonical.isEmpty()
            || !entryCanonical.startsWith(stagingCanonical + QDir::separator())) {
            finishRepositoryInstall(false, QStringLiteral("Repository entry file is missing or outside the plugin folder."));
            return;
        }

        const QString destination = QDir(m_directory).filePath(id);
        if (QFileInfo::exists(destination)) {
            finishRepositoryInstall(false, QStringLiteral("A plugin named “%1” is already installed.").arg(id));
            return;
        }
        if (!QDir(m_directory).rename(QFileInfo(m_repositoryStagingPath).fileName(), id)) {
            finishRepositoryInstall(false, QStringLiteral("Could not move the plugin into the plugin folder."));
            return;
        }

        m_repositoryStagingPath.clear();
        refresh();
        m_repositoryInstallStatus = QStringLiteral("Installed “%1”. Review its permissions, then enable it.").arg(id);
        emit repositoryInstallChanged();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_repositoryProcess != process)
            return;
        m_repositoryProcess = nullptr;
        process->deleteLater();
        finishRepositoryInstall(false, QStringLiteral("Could not start Git to clone this repository."));
    });
    process->start();
    return true;
}

void PluginManager::refresh()
{
    QJsonObject states;
    QFile state(m_stateFile);
    if (state.open(QIODevice::ReadOnly)) states = QJsonDocument::fromJson(state.readAll()).object();
    QList<Plugin> found;
    const auto dirs = QDir(m_directory).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto &dir : dirs) {
        Plugin p;
        p.id = dir.fileName();
        // Keep every plugin folder visible in Settings, even when its
        // manifest is absent or malformed. The folder name is the fallback.
        p.name = p.id;
        const QDir pluginDir(dir.absoluteFilePath());
        QFile manifest(pluginDir.filePath(QStringLiteral("plugin.json")));
        if (!manifest.open(QIODevice::ReadOnly)) { p.error = QStringLiteral("Missing plugin.json"); found.append(p); continue; }
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(manifest.readAll(), &parse);
        const auto obj = doc.object();
        p.name = obj.value("name").toString(p.id);
        p.version = obj.value("version").toString();
        p.description = obj.value("description").toString();
        p.author = obj.value("author").toString();
        p.apiVersion = obj.value("apiVersion").toInt(1);
        for (const auto &permission : obj.value("permissions").toArray())
            if (permission.isString()) p.permissions.append(permission.toString());
        const QString entryName = obj.value("entry").toString(QStringLiteral("main.lua"));
        const QFileInfo entryInfo(pluginDir.filePath(entryName));
        p.entry = entryInfo.canonicalFilePath();
        if (parse.error != QJsonParseError::NoError || !doc.isObject()) p.error = QStringLiteral("Invalid plugin.json");
        else if (obj.value("id").toString() != p.id) p.error = QStringLiteral("Manifest id must match its folder name");
        else if (p.apiVersion != 1) p.error = QStringLiteral("Plugin requires an unsupported Aether plugin API version");
        else if (!entryInfo.isFile() || p.entry.isEmpty() || !p.entry.startsWith(pluginDir.canonicalPath() + QDir::separator())) p.error = QStringLiteral("Entry Lua file is missing or outside the plugin folder");
        p.enabled = p.error.isEmpty() && states.value(p.id).toBool(false);
        found.append(p);
    }
    const auto runningIds = m_runtimes.keys();
    for (const auto &id : runningIds) stopPlugin(id);
    m_contextRoutes.clear();
    beginResetModel(); m_plugins = found; endResetModel();
    for (int i = 0; i < m_plugins.size(); ++i)
        if (m_plugins[i].enabled && !startPlugin(i)) m_plugins[i].enabled = false;
    emit countChanged();
    emit actionsChanged();
}

bool PluginManager::setEnabled(const QString &id, bool enabled)
{
    int row = -1;
    for (int i=0;i<m_plugins.size();++i) if (m_plugins[i].id == id) { row=i; break; }
    if (row < 0 || !m_plugins[row].error.isEmpty()) return false;
    QJsonObject states;
    QFile old(m_stateFile); if (old.open(QIODevice::ReadOnly)) states = QJsonDocument::fromJson(old.readAll()).object();
    states.insert(id, enabled);
    QDir().mkpath(QFileInfo(m_stateFile).absolutePath());
    QSaveFile file(m_stateFile);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(states).toJson()) < 0 || !file.commit()) return false;
    if (enabled && !startPlugin(row)) {
        emit dataChanged(index(row), index(row), {ErrorRole, ActionsRole});
        return false;
    }
    if (!enabled) stopPlugin(id);
    m_plugins[row].enabled = enabled;
    emit dataChanged(index(row), index(row), {EnabledRole, ErrorRole, ActionsRole});
    emit actionsChanged();
    return true;
}

bool PluginManager::startPlugin(int row)
{
    auto &plugin = m_plugins[row];
    if (!plugin.error.isEmpty()) return false;
    lua_State *state = luaL_newstate();
    if (!state) { plugin.error = QStringLiteral("Could not create Lua state"); return false; }
    luaL_requiref(state, LUA_GNAME, luaopen_base, 1); lua_pop(state, 1);
    luaL_requiref(state, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(state, 1);
    luaL_requiref(state, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(state, 1);
    luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(state, 1);
    luaL_requiref(state, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(state, 1);
    lua_newtable(state);
    lua_pushlightuserdata(state, this); lua_pushstring(state, plugin.id.toUtf8().constData());
    lua_pushcclosure(state, pluginLuaLog, 2); lua_setfield(state, -2, "log");
    lua_pushlightuserdata(state, this); lua_pushstring(state, plugin.id.toUtf8().constData());
    lua_pushcclosure(state, pluginLuaAddAction, 2); lua_setfield(state, -2, "addAction");
    lua_pushlightuserdata(state, this); lua_pushstring(state, plugin.id.toUtf8().constData());
    lua_pushcclosure(state, pluginLuaAddContextMenu, 2); lua_setfield(state, -2, "addContextMenu");
    lua_pushlightuserdata(state, this); lua_pushstring(state, plugin.id.toUtf8().constData());
    lua_pushcclosure(state, pluginLuaAddSettingsSection, 2); lua_setfield(state, -2, "addSettingsSection");
    lua_pushlightuserdata(state, this); lua_pushstring(state, plugin.id.toUtf8().constData());
    lua_pushcclosure(state, pluginLuaAddToolbarButton, 2); lua_setfield(state, -2, "addToolbarButton");
    lua_pushlightuserdata(state, this); lua_pushstring(state, plugin.id.toUtf8().constData());
    lua_pushcclosure(state, pluginLuaAddShortcut, 2); lua_setfield(state, -2, "addShortcut");
    lua_pushlightuserdata(state, this); lua_pushstring(state, plugin.id.toUtf8().constData());
    lua_pushcclosure(state, pluginLuaOn, 2); lua_setfield(state, -2, "on");
    lua_pushlightuserdata(state, this); lua_pushstring(state, plugin.id.toUtf8().constData());
    lua_pushcclosure(state, pluginLuaGetSetting, 2); lua_setfield(state, -2, "getSetting");
    lua_pushlightuserdata(state, this); lua_pushstring(state, plugin.id.toUtf8().constData());
    lua_pushcclosure(state, pluginLuaSetSetting, 2); lua_setfield(state, -2, "setSetting");
    for (const auto &api : QVector<QPair<const char *, lua_CFunction>>{
             {"run", pluginLuaRun}, {"fileInfo", pluginLuaFileInfo}, {"listDirectory", pluginLuaListDirectory},
             {"open", pluginLuaOpen}, {"rename", pluginLuaRename}, {"copy", pluginLuaCopy},
             {"move", pluginLuaMove}, {"trash", pluginLuaTrash}}) {
        lua_pushlightuserdata(state, this); lua_pushstring(state, plugin.id.toUtf8().constData());
        lua_pushcclosure(state, api.second, 2); lua_setfield(state, -2, api.first);
    }
    lua_setglobal(state, "aether");
    plugin.actions.clear();
    plugin.settings.clear();
    plugin.toolbarActions.clear();
    plugin.shortcuts.clear();
    plugin.contextMenus.clear();
    m_runtimes.insert(plugin.id, Runtime{state, {}});
    if (luaL_loadfile(state, QFile::encodeName(plugin.entry).constData()) != LUA_OK
        || lua_pcall(state, 0, 0, 0) != LUA_OK) {
        plugin.error = QString::fromUtf8(lua_tostring(state, -1));
        stopPlugin(plugin.id);
        return false;
    }
    return true;
}

void PluginManager::stopPlugin(const QString &id)
{
    auto it = m_runtimes.find(id);
    if (it == m_runtimes.end()) return;
    if (it->state) lua_close(it->state);
    m_runtimes.erase(it);
}

void PluginManager::registerAction(const QString &id, const QString &label, int callbackRef)
{
    const auto it = m_runtimes.find(id);
    if (it == m_runtimes.end()) return;
    it->callbacks.append(callbackRef);
    for (auto &plugin : m_plugins) if (plugin.id == id) plugin.actions.append(label);
}

void PluginManager::registerSettingsSection(const QString &id, SettingsSection section)
{
    for (int row = 0; row < m_plugins.size(); ++row) {
        auto &plugin = m_plugins[row];
        if (plugin.id != id) continue;
        QVariantMap stored = m_settingValues.value(id).toObject().toVariantMap();
        for (QVariant &entry : section.controls) {
            auto control = entry.toMap();
            const QString key = control.value(QStringLiteral("key")).toString();
            if (!key.isEmpty() && stored.contains(key)) control.insert(QStringLiteral("value"), stored.value(key));
            entry = control;
        }
        QVariantMap sectionMap{{QStringLiteral("title"), section.title},
                               {QStringLiteral("description"), section.description},
                               {QStringLiteral("controls"), section.controls}};
        plugin.settings.append(sectionMap);
        emit dataChanged(index(row), index(row), {SettingsRole});
        return;
    }
}

void PluginManager::registerToolbarButton(const QString &id, const QString &label,
                                         const QString &icon, int callbackRef)
{
    auto runtime = m_runtimes.find(id);
    if (runtime == m_runtimes.end()) return;
    const int actionIndex = runtime->callbacks.size();
    runtime->callbacks.append(callbackRef);
    for (int row = 0; row < m_plugins.size(); ++row) if (m_plugins[row].id == id) {
        QVariantMap button{{QStringLiteral("pluginId"), id}, {QStringLiteral("label"), label},
                           {QStringLiteral("actionIndex"), actionIndex}};
        if (!icon.isEmpty()) button.insert(QStringLiteral("icon"), icon);
        m_plugins[row].toolbarActions.append(button);
        emit dataChanged(index(row), index(row), {ActionsRole});
        emit actionsChanged();
        return;
    }
}

void PluginManager::registerShortcut(const QString &id, const QString &name,
                                    const QString &sequence, int callbackRef)
{
    auto runtime = m_runtimes.find(id);
    if (runtime == m_runtimes.end()) return;
    const int actionIndex = runtime->callbacks.size();
    runtime->callbacks.append(callbackRef);
    for (int row = 0; row < m_plugins.size(); ++row) if (m_plugins[row].id == id) {
        m_plugins[row].shortcuts.append(QVariantMap{{QStringLiteral("pluginId"), id},
            {QStringLiteral("name"), name}, {QStringLiteral("shortcut"), sequence},
            {QStringLiteral("actionIndex"), actionIndex}});
        emit dataChanged(index(row), index(row), {ActionsRole});
        emit actionsChanged();
        return;
    }
}

void PluginManager::registerEventHandler(const QString &id, const QString &eventName, int callbackRef)
{
    static const QStringList events = {QStringLiteral("app.started"), QStringLiteral("folder.opened"),
        QStringLiteral("files.selected")};
    auto runtime = m_runtimes.find(id);
    if (runtime == m_runtimes.end() || !events.contains(eventName)) return;
    runtime->eventHandlers[eventName].append(callbackRef);
    runtime->callbacks.append(callbackRef);
}

void PluginManager::dispatchEvent(const QString &eventName, const QVariantMap &payload)
{
    for (auto runtime = m_runtimes.begin(); runtime != m_runtimes.end(); ++runtime) {
        const auto refs = runtime->eventHandlers.value(eventName);
        for (const int ref : refs) {
            lua_rawgeti(runtime->state, LUA_REGISTRYINDEX, ref);
            pushVariant(runtime->state, payload);
            if (lua_pcall(runtime->state, 1, 0, 0) != LUA_OK) {
                qWarning().noquote() << "Aether plugin event handler failed:" << runtime.key() << lua_tostring(runtime->state, -1);
                lua_pop(runtime->state, 1);
            }
        }
    }
}

QVariantList PluginManager::toolbarActions() const
{
    QVariantList result;
    for (const auto &plugin : m_plugins) if (plugin.enabled && m_runtimes.contains(plugin.id))
        result.append(plugin.toolbarActions);
    return result;
}

QVariantList PluginManager::shortcuts() const
{
    QVariantList result;
    for (const auto &plugin : m_plugins) if (plugin.enabled && m_runtimes.contains(plugin.id))
        result.append(plugin.shortcuts);
    return result;
}

QVariant PluginManager::setting(const QString &id, const QString &key, const QVariant &fallback) const
{
    const QJsonObject values = m_settingValues.value(id).toObject();
    const QJsonValue value = values.value(key);
    return value.isUndefined() ? fallback : value.toVariant();
}

bool PluginManager::setSetting(const QString &id, const QString &key, const QVariant &value)
{
    if (key.isEmpty()) return false;
    QJsonObject values = m_settingValues.value(id).toObject();
    values.insert(key, QJsonValue::fromVariant(value));
    m_settingValues.insert(id, values);
    QDir().mkpath(QFileInfo(m_settingsFile).absolutePath());
    QSaveFile file(m_settingsFile);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(m_settingValues).toJson()) < 0 || !file.commit()) return false;
    for (int row = 0; row < m_plugins.size(); ++row) {
        auto &plugin = m_plugins[row];
        if (plugin.id != id) continue;
        for (QVariant &sectionValue : plugin.settings) {
            auto section = sectionValue.toMap();
            QVariantList controls = section.value(QStringLiteral("controls")).toList();
            for (QVariant &controlValue : controls) {
                auto control = controlValue.toMap();
                if (control.value(QStringLiteral("key")).toString() == key) {
                    control.insert(QStringLiteral("value"), value);
                    controlValue = control;
                }
            }
            section.insert(QStringLiteral("controls"), controls);
            sectionValue = section;
        }
        emit dataChanged(index(row), index(row), {SettingsRole});
        break;
    }
    return true;
}

bool PluginManager::setPermission(const QString &id, const QString &permission, bool enabled)
{
    auto plugin = std::find_if(m_plugins.begin(), m_plugins.end(), [&](const Plugin &candidate) { return candidate.id == id; });
    if (plugin == m_plugins.end() || !plugin->permissions.contains(permission)) return false;
    QJsonObject allowed = m_permissionValues.value(id).toObject();
    allowed.insert(permission, enabled);
    m_permissionValues.insert(id, allowed);
    QDir().mkpath(QFileInfo(m_permissionsFile).absolutePath());
    QSaveFile file(m_permissionsFile);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(m_permissionValues).toJson()) < 0 || !file.commit()) return false;
    const int row = int(std::distance(m_plugins.begin(), plugin));
    emit dataChanged(index(row), index(row), {PermissionsRole});
    return true;
}

bool PluginManager::hasPermission(const QString &id, const QString &permission) const
{
    const auto plugin = std::find_if(m_plugins.cbegin(), m_plugins.cend(), [&](const Plugin &candidate) { return candidate.id == id; });
    if (plugin == m_plugins.cend()) return false;
    // Manifests predating permission declarations retain their existing behavior.
    if (plugin->permissions.isEmpty()) return true;
    if (!plugin->permissions.contains(permission)) return false;
    return m_permissionValues.value(id).toObject().value(permission).toBool(false);
}

void PluginManager::registerContextMenu(const QString &id, ContextMenuContribution contribution)
{
    for (auto &plugin : m_plugins) {
        if (plugin.id == id) {
            const int menuIndex = plugin.contextMenus.size();
            for (int i = 0; i < contribution.actions.size(); ++i) {
                contribution.actions[i].route = m_nextContextRoute++;
                m_contextRoutes.insert(contribution.actions[i].route, {id, menuIndex, i});
            }
            plugin.contextMenus.append(std::move(contribution));
            return;
        }
    }
}

QVariantList PluginManager::contextMenuItems(const QString &contextType, const QString &path,
                                              bool isDirectory, const QVariantList &paths,
                                              const QVariantMap &contextData)
{
    QVariantList result;
    QMimeDatabase mimeDb;
    QStringList checkedPaths;
    for (const auto &value : paths) checkedPaths.append(value.toString());
    if (checkedPaths.isEmpty()) checkedPaths.append(path);
    for (const auto &plugin : m_plugins) {
        if (!plugin.enabled || !m_runtimes.contains(plugin.id)) continue;
        for (int menuIndex = 0; menuIndex < plugin.contextMenus.size(); ++menuIndex) {
            const auto &menu = plugin.contextMenus[menuIndex];
            if (!menu.contexts.contains(contextType)) continue;
            bool matches = true;
            if (contextType == QStringLiteral("file") && checkedPaths.isEmpty()) matches = false;
            if (contextType == QStringLiteral("file")) for (const auto &candidate : checkedPaths) {
                const QFileInfo info(candidate);
                const QString ext = info.suffix().toLower();
                const QString candidateMime = mimeDb.mimeTypeForFile(candidate, QMimeDatabase::MatchExtension).name();
                bool typeMatch = false;
                for (const auto &allowed : menu.extensions) {
                    const QString normalized = allowed.startsWith('.') ? allowed.mid(1).toLower() : allowed.toLower();
                    if (normalized == ext) typeMatch = true;
                }
                for (const auto &allowed : menu.mimeTypes) {
                    if (allowed.endsWith("/*") ? candidateMime.startsWith(allowed.left(allowed.size() - 1)) : candidateMime == allowed)
                        typeMatch = true;
                }
                if (!typeMatch) { matches = false; break; }
            }
            if (!matches) continue;
            QVariantList submenu;
            for (int actionIndex = 0; actionIndex < menu.actions.size(); ++actionIndex) {
                const int route = menu.actions[actionIndex].route;
                QVariantMap item{{"text", menu.actions[actionIndex].label},
                                 {"action", QStringLiteral("pluginctx:%1").arg(route)}};
                if (!menu.actions[actionIndex].icon.isEmpty()) item.insert(QStringLiteral("icon"), menu.actions[actionIndex].icon);
                submenu.append(item);
            }
            QVariantMap parent{{"text", menu.label}, {"action", QStringLiteral("pluginctx-submenu:%1:%2:%3")
                               .arg(contextType, path).arg(contextData.value(QStringLiteral("kind")).toString())},
                               {"isSubmenu", true}, {"submenuItems", submenu}};
            if (!menu.icon.isEmpty()) parent.insert(QStringLiteral("icon"), menu.icon);
            result.append(parent);
        }
    }
    return result;
}

void PluginManager::triggerContextAction(int route, const QVariantMap &context)
{
    const auto routeIt = m_contextRoutes.constFind(route);
    if (routeIt == m_contextRoutes.cend()) return;
    const ContextRoute target = routeIt.value();
    auto runtime = m_runtimes.find(target.pluginId);
    if (runtime == m_runtimes.end()) return;
    const auto pluginIt = std::find_if(m_plugins.cbegin(), m_plugins.cend(), [&](const Plugin &p) { return p.id == target.pluginId; });
    if (pluginIt == m_plugins.cend() || target.menuIndex < 0 || target.menuIndex >= pluginIt->contextMenus.size()) return;
    const auto &menu = pluginIt->contextMenus[target.menuIndex];
    if (target.actionIndex < 0 || target.actionIndex >= menu.actions.size()) return;
    QVariantMap pluginContext = context;
    const QString path = pluginContext.value(QStringLiteral("path")).toString();
    const QVariantMap targetInfo = fileInfo(path);
    for (auto it = targetInfo.cbegin(); it != targetInfo.cend(); ++it)
        pluginContext.insert(it.key(), it.value());
    const QVariantList paths = pluginContext.value(QStringLiteral("paths")).toList();
    QVariantList filePaths, folderPaths, selectedInfo;
    for (const QVariant &pathValue : paths) {
        const QString selectedPath = pathValue.toString();
        const QVariantMap info = fileInfo(selectedPath);
        selectedInfo.append(info);
        (info.value(QStringLiteral("isDirectory")).toBool() ? folderPaths : filePaths).append(selectedPath);
    }
    pluginContext.insert(QStringLiteral("files"), filePaths);
    pluginContext.insert(QStringLiteral("folders"), folderPaths);
    pluginContext.insert(QStringLiteral("selected"), selectedInfo);
    lua_rawgeti(runtime->state, LUA_REGISTRYINDEX, menu.actions[target.actionIndex].callbackRef);
    pushVariant(runtime->state, pluginContext);
    if (lua_pcall(runtime->state, 1, 0, 0) != LUA_OK)
        qWarning().noquote() << "Aether plugin context action failed:" << target.pluginId << lua_tostring(runtime->state, -1);
}

QVariantMap PluginManager::fileInfo(const QString &path) const
{
    const QFileInfo info(path);
    QMimeDatabase db;
    const auto mime = db.mimeTypeForFile(path, QMimeDatabase::MatchExtension);
    return {{QStringLiteral("path"), info.absoluteFilePath()},
            {QStringLiteral("fileName"), info.fileName()},
            {QStringLiteral("baseName"), info.completeBaseName()},
            {QStringLiteral("extension"), info.suffix().toLower()},
            {QStringLiteral("parentPath"), info.absolutePath()},
            {QStringLiteral("isDirectory"), info.isDir()},
            {QStringLiteral("isFile"), info.isFile()},
            {QStringLiteral("mimeType"), mime.name()},
            {QStringLiteral("fileType"), mime.comment()},
            {QStringLiteral("size"), info.size()}};
}

QVariantList PluginManager::listDirectory(const QString &path) const
{
    QVariantList entries;
    const QDir directory(path);
    if (!directory.exists()) return entries;
    const auto children = directory.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Readable,
                                                  QDir::Name | QDir::DirsFirst);
    for (const auto &child : children) entries.append(fileInfo(child.absoluteFilePath()));
    return entries;
}

bool PluginManager::runProgram(const QString &program, const QStringList &arguments) const
{
    const QString executable = QStandardPaths::findExecutable(program);
    return !executable.isEmpty() && QProcess::startDetached(executable, arguments);
}

void PluginManager::openPath(const QString &path) const
{
    if (m_fileOperations) m_fileOperations->openFile(path);
}

bool PluginManager::renamePath(const QString &path, const QString &newName) const
{
    return m_fileOperations && m_fileOperations->rename(path, newName);
}

int PluginManager::copyPaths(const QStringList &paths, const QString &destination) const
{
    return m_fileOperations ? m_fileOperations->copyFiles(paths, destination) : -1;
}

int PluginManager::movePaths(const QStringList &paths, const QString &destination) const
{
    return m_fileOperations ? m_fileOperations->moveFiles(paths, destination) : -1;
}

int PluginManager::trashPaths(const QStringList &paths) const
{
    return m_fileOperations ? m_fileOperations->trashFiles(paths) : -1;
}

void PluginManager::triggerAction(const QString &id, int actionIndex)
{
    auto it = m_runtimes.find(id);
    if (it == m_runtimes.end() || actionIndex < 0 || actionIndex >= it->callbacks.size()) return;
    lua_rawgeti(it->state, LUA_REGISTRYINDEX, it->callbacks[actionIndex]);
    if (lua_pcall(it->state, 0, 0, 0) != LUA_OK) {
        for (int row = 0; row < m_plugins.size(); ++row) {
            if (m_plugins[row].id == id) {
                m_plugins[row].error = QString::fromUtf8(lua_tostring(it->state, -1));
                emit dataChanged(index(row), index(row), {ErrorRole});
                break;
            }
        }
    }
}
