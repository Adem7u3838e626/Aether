#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QProcess>
#include <QVariantMap>

#include <functional>

class RcloneService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool rcloneAvailable READ rcloneAvailable NOTIFY rcloneAvailableChanged)
    Q_PROPERTY(QStringList activeMounts READ activeMounts NOTIFY activeMountsChanged)
    Q_PROPERTY(QStringList remoteNames READ remoteNames NOTIFY remoteNamesChanged)
    Q_PROPERTY(bool configurationBusy READ configurationBusy NOTIFY configurationBusyChanged)

public:
    explicit RcloneService(QObject *parent = nullptr);
    ~RcloneService();

    bool rcloneAvailable() const;
    QStringList activeMounts() const;
    QStringList remoteNames() const;
    bool configurationBusy() const;

    Q_INVOKABLE bool isRclonePath(const QString &path) const;
    Q_INVOKABLE bool isMounted(const QString &remoteName) const;
    Q_INVOKABLE bool isMounting(const QString &remoteName) const;
    Q_INVOKABLE bool isMountedForPath(const QString &path) const;
    Q_INVOKABLE void mountRemote(const QString &remoteName);
    Q_INVOKABLE void unmountRemote(const QString &remoteName);
    Q_INVOKABLE QString getMountPath(const QString &remoteName) const;
    Q_INVOKABLE QString getRemoteNameFromPath(const QString &path) const;
    Q_INVOKABLE void refreshRemotes();
    Q_INVOKABLE void beginConfiguration(const QString &remoteName, const QString &backend);
    Q_INVOKABLE void continueConfiguration(const QString &state, const QString &result);
    Q_INVOKABLE void cancelConfiguration();
    Q_INVOKABLE void renameRemote(const QString &remoteName, const QString &newName);
    Q_INVOKABLE void removeRemote(const QString &remoteName);

signals:
    void rcloneAvailableChanged();
    void activeMountsChanged();
    void remoteNamesChanged();
    void configurationBusyChanged();
    void configurationQuestion(const QString &remoteName, const QString &state,
                               const QVariantMap &option, const QString &errorString);
    void configurationFinished(const QString &remoteName, bool success, const QString &errorString);
    void mountFinished(const QString &remoteName, bool success, const QString &errorString);
    void unmountFinished(const QString &remoteName, bool success);
    void remoteActionFinished(const QString &action, const QString &remoteName,
                              bool success, const QString &errorString);

private:
    bool checkRcloneAvailable() const;
    void ensureMountsBaseDirExists() const;
    void startRcloneMountProcess(const QString &remoteName, const QString &mountPath);
    void releaseMountPoint(const QString &mountPath, const std::function<void()> &then);
    void runUnmountTool(const QString &tool, const QString &mountPath,
                        const std::function<void(bool)> &done);
    void runConfigurationCommand(const QStringList &args);
    void deleteRemoteConfig(const QString &remoteName);

    bool m_rcloneAvailable;
    QString m_mountsBaseDir;
    QStringList m_remoteNames;
    QString m_configurationRemoteName;
    QString m_configurationBackend;
    QString m_configurationState;
    QProcess *m_configurationProcess = nullptr;
    bool m_configurationBusy = false;
    QHash<QString, QProcess*> m_processes;
    QHash<QString, bool> m_mountSuccessEmitted;
    // Bumped per mount attempt so a timeout fired for an abandoned attempt
    // cannot tear down the mount that replaced it.
    QHash<QString, quint64> m_mountGeneration;
};
