#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QJsonObject>
#include <QVector>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>

struct lua_State;
class FileOperations;

class PluginManager : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString pluginDirectory READ pluginDirectory CONSTANT)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QVariantList toolbarActions READ toolbarActions NOTIFY actionsChanged)
    Q_PROPERTY(QVariantList shortcuts READ shortcuts NOTIFY actionsChanged)

public:
    struct ContextAction { QString label; QString icon; int callbackRef = -2; int route = -1; };
    struct ContextMenuContribution {
        QString label;
        QString icon;
        QStringList extensions;
        QStringList mimeTypes;
        QStringList contexts;
        QVector<ContextAction> actions;
    };
    struct SettingsSection { QString title; QString description; QVariantList controls; };
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, VersionRole, DescriptionRole,
                 AuthorRole, EntryRole, EnabledRole, ErrorRole, ActionsRole, SettingsRole, PermissionsRole };
    Q_ENUM(Roles)

    explicit PluginManager(FileOperations *fileOperations, QObject *parent = nullptr);
    ~PluginManager() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString pluginDirectory() const;
    Q_INVOKABLE bool setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void triggerAction(const QString &id, int actionIndex);
    Q_INVOKABLE void dispatchEvent(const QString &eventName, const QVariantMap &payload = {});
    Q_INVOKABLE QVariantList toolbarActions() const;
    Q_INVOKABLE QVariantList shortcuts() const;
    Q_INVOKABLE QVariant setting(const QString &id, const QString &key, const QVariant &fallback = {}) const;
    Q_INVOKABLE bool setSetting(const QString &id, const QString &key, const QVariant &value);
    Q_INVOKABLE bool setPermission(const QString &id, const QString &permission, bool enabled);
    Q_INVOKABLE QVariantList contextMenuItems(const QString &contextType, const QString &path,
                                               bool isDirectory, const QVariantList &paths,
                                               const QVariantMap &contextData = {});
    Q_INVOKABLE void triggerContextAction(int route, const QVariantMap &context);
    Q_INVOKABLE QVariantMap fileInfo(const QString &path) const;
    Q_INVOKABLE QVariantList listDirectory(const QString &path) const;
    void registerAction(const QString &id, const QString &label, int callbackRef);
    void registerSettingsSection(const QString &id, SettingsSection section);
    void registerToolbarButton(const QString &id, const QString &label, const QString &icon, int callbackRef);
    void registerShortcut(const QString &id, const QString &name, const QString &sequence, int callbackRef);
    void registerEventHandler(const QString &id, const QString &eventName, int callbackRef);
    void registerContextMenu(const QString &id, ContextMenuContribution contribution);
    bool runProgram(const QString &program, const QStringList &arguments) const;
    bool hasPermission(const QString &id, const QString &permission) const;
    void openPath(const QString &path) const;
    bool renamePath(const QString &path, const QString &newName) const;
    int copyPaths(const QStringList &paths, const QString &destination) const;
    int movePaths(const QStringList &paths, const QString &destination) const;
    int trashPaths(const QStringList &paths) const;

signals:
    void countChanged();
    void actionsChanged();

private:
    struct Plugin { QString id, name, version, description, author, entry, error; QStringList permissions; int apiVersion = 1; bool enabled = false; QVariantList actions; QVariantList settings; QVariantList toolbarActions; QVariantList shortcuts; QVector<ContextMenuContribution> contextMenus; };
    struct Runtime { lua_State *state = nullptr; QVector<int> callbacks; QHash<QString, QVector<int>> eventHandlers; };
    struct ContextRoute { QString pluginId; int menuIndex = -1; int actionIndex = -1; };
    QList<Plugin> m_plugins;
    QHash<QString, Runtime> m_runtimes;
    QHash<int, ContextRoute> m_contextRoutes;
    int m_nextContextRoute = 1;
    QString m_directory;
    QString m_stateFile;
    QString m_settingsFile;
    QString m_permissionsFile;
    QJsonObject m_settingValues;
    QJsonObject m_permissionValues;
    FileOperations *m_fileOperations = nullptr;
    bool startPlugin(int row);
    void stopPlugin(const QString &id);
};
