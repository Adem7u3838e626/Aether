#include "services/rcloneservice.h"

#include "services/cloudmounts.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QDebug>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

RcloneService::RcloneService(QObject *parent)
    : QObject(parent)
{
    m_rcloneAvailable = checkRcloneAvailable();
    m_mountsBaseDir = cloudMountsBaseDir();
    ensureMountsBaseDirExists();
    if (m_rcloneAvailable)
        refreshRemotes();
}

RcloneService::~RcloneService()
{
    // unmountRemote() drives fusermount through the event loop, and by the
    // time we are destroyed there is no event loop left to run it: the
    // unmount would never happen and ~QProcess would SIGKILL rclone with the
    // FUSE mount still attached, leaving a dead mount point behind. Detach
    // fusermount so it outlives us, then ask rclone to go away.
    const QStringList remotes = m_processes.keys();
    for (const QString &remote : remotes) {
        const QString mountPath = getMountPath(remote);
        if (!QProcess::startDetached(QStringLiteral("fusermount"), {QStringLiteral("-u"), mountPath}))
            QProcess::startDetached(QStringLiteral("fusermount3"), {QStringLiteral("-u"), mountPath});

        QProcess *proc = m_processes.value(remote);
        if (proc && proc->state() != QProcess::NotRunning)
            proc->terminate();
    }
    m_processes.clear();
}

bool RcloneService::rcloneAvailable() const
{
    return m_rcloneAvailable;
}

QStringList RcloneService::activeMounts() const
{
    return m_processes.keys();
}

QStringList RcloneService::remoteNames() const
{
    return m_remoteNames;
}

bool RcloneService::configurationBusy() const
{
    return m_configurationBusy;
}

void RcloneService::refreshRemotes()
{
    if (!m_rcloneAvailable)
        return;

    QProcess *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc](int exitCode, QProcess::ExitStatus status) {
        if (status == QProcess::NormalExit && exitCode == 0) {
            QStringList names;
            const QString output = QString::fromUtf8(proc->readAllStandardOutput());
            for (QString name : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                name = name.trimmed();
                if (name.endsWith(QLatin1Char(':')))
                    name.chop(1);
                if (!name.isEmpty())
                    names.append(name);
            }
            names.removeDuplicates();
            names.sort(Qt::CaseInsensitive);
            if (names != m_remoteNames) {
                m_remoteNames = names;
                emit remoteNamesChanged();
            }
        }
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, proc, &QObject::deleteLater);
    proc->start(QStringLiteral("rclone"), {QStringLiteral("listremotes")});
}

void RcloneService::beginConfiguration(const QString &remoteName, const QString &backend)
{
    static const QStringList supportedBackends = {
        QStringLiteral("drive"), QStringLiteral("dropbox"), QStringLiteral("onedrive"),
        QStringLiteral("webdav"), QStringLiteral("s3")
    };

    const QString name = remoteName.trimmed();
    if (!m_rcloneAvailable) {
        emit configurationFinished(name, false, QStringLiteral("rclone is not installed."));
        return;
    }
    if (m_configurationBusy) {
        emit configurationFinished(name, false, QStringLiteral("Another rclone setup is already running."));
        return;
    }
    if (name.isEmpty() || !QRegularExpression(QStringLiteral("^[A-Za-z0-9_.-]+$")).match(name).hasMatch()) {
        emit configurationFinished(name, false,
                                   QStringLiteral("Use a remote name containing only letters, numbers, dots, dashes, or underscores."));
        return;
    }
    if (m_remoteNames.contains(name, Qt::CaseInsensitive)) {
        emit configurationFinished(name, false, QStringLiteral("A remote with that name already exists."));
        return;
    }
    if (!supportedBackends.contains(backend)) {
        emit configurationFinished(name, false, QStringLiteral("Unsupported cloud provider."));
        return;
    }

    m_configurationRemoteName = name;
    m_configurationBackend = backend;
    m_configurationState.clear();
    runConfigurationCommand({QStringLiteral("config"), QStringLiteral("create"), name, backend,
                             QStringLiteral("--non-interactive")});
}

void RcloneService::continueConfiguration(const QString &state, const QString &result)
{
    if (m_configurationRemoteName.isEmpty() || m_configurationBusy || state.isEmpty())
        return;
    if (state != m_configurationState) {
        emit configurationFinished(m_configurationRemoteName, false,
                                   QStringLiteral("The rclone setup state changed. Start the setup again."));
        return;
    }

    runConfigurationCommand({QStringLiteral("config"), QStringLiteral("create"),
                             m_configurationRemoteName, m_configurationBackend,
                             QStringLiteral("--continue"), QStringLiteral("--state"), state,
                             QStringLiteral("--result"), result, QStringLiteral("--non-interactive")});
}

void RcloneService::cancelConfiguration()
{
    if (m_configurationProcess && m_configurationProcess->state() != QProcess::NotRunning)
        m_configurationProcess->kill();
    m_configurationRemoteName.clear();
    m_configurationBackend.clear();
    m_configurationState.clear();
}

void RcloneService::renameRemote(const QString &remoteName, const QString &newName)
{
    const QString oldName = remoteName.trimmed();
    const QString name = newName.trimmed();
    static const QRegularExpression validName(QStringLiteral("^[A-Za-z0-9_.+@ -]+$"));
    if (!m_rcloneAvailable || !m_remoteNames.contains(oldName)) {
        emit remoteActionFinished(QStringLiteral("rename"), oldName, false,
                                  QStringLiteral("Cloud drive was not found."));
        return;
    }
    if (name.isEmpty() || name.startsWith(QLatin1Char('-')) || name.startsWith(QLatin1Char(' '))
        || name.endsWith(QLatin1Char(' ')) || !validName.match(name).hasMatch()) {
        emit remoteActionFinished(QStringLiteral("rename"), oldName, false,
                                  QStringLiteral("That cloud drive name is not valid."));
        return;
    }
    if (name == oldName) {
        emit remoteActionFinished(QStringLiteral("rename"), oldName, true, QString());
        return;
    }
    if (m_remoteNames.contains(name, Qt::CaseInsensitive)) {
        emit remoteActionFinished(QStringLiteral("rename"), oldName, false,
                                  QStringLiteral("A cloud drive with that name already exists."));
        return;
    }
    if (m_processes.contains(oldName)) {
        auto *connection = new QMetaObject::Connection;
        *connection = connect(this, &RcloneService::unmountFinished, this,
                              [this, oldName, name, connection](const QString &finishedRemote, bool success) {
            if (finishedRemote != oldName)
                return;
            disconnect(*connection);
            delete connection;
            if (!success) {
                emit remoteActionFinished(QStringLiteral("rename"), oldName, false,
                                          QStringLiteral("Could not unmount the cloud drive."));
                return;
            }
            renameRemote(oldName, name);
        });
        unmountRemote(oldName);
        return;
    }

    auto *proc = new QProcess(this);
    connect(proc, &QProcess::started, proc, [proc, oldName, name]() {
        proc->write(QStringLiteral("r\n%1\n%2\nq\n").arg(oldName, name).toUtf8());
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, oldName](int exitCode, QProcess::ExitStatus status) {
        const QString output = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
        const QString errorOutput = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        const bool success = status == QProcess::NormalExit && exitCode == 0
            && !output.contains(QStringLiteral("Error:"), Qt::CaseInsensitive);
        const QString error = !errorOutput.isEmpty() ? errorOutput
            : (success ? QString() : (output.isEmpty() ? QStringLiteral("rclone could not rename this cloud drive.") : output));
        proc->deleteLater();
        if (success)
            refreshRemotes();
        emit remoteActionFinished(QStringLiteral("rename"), oldName, success, error);
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc, oldName](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        const QString message = proc->errorString();
        proc->deleteLater();
        emit remoteActionFinished(QStringLiteral("rename"), oldName, false, message);
    });
    proc->start(QStringLiteral("rclone"), {QStringLiteral("config")});
}

void RcloneService::removeRemote(const QString &remoteName)
{
    if (!m_rcloneAvailable || !m_remoteNames.contains(remoteName)) {
        emit remoteActionFinished(QStringLiteral("remove"), remoteName, false,
                                  QStringLiteral("Cloud drive was not found."));
        return;
    }
    if (m_processes.contains(remoteName)) {
        auto *connection = new QMetaObject::Connection;
        *connection = connect(this, &RcloneService::unmountFinished, this,
                              [this, remoteName, connection](const QString &finishedRemote, bool success) {
            if (finishedRemote != remoteName)
                return;
            disconnect(*connection);
            delete connection;
            if (!success) {
                emit remoteActionFinished(QStringLiteral("remove"), remoteName, false,
                                          QStringLiteral("Could not unmount the cloud drive."));
                return;
            }
            deleteRemoteConfig(remoteName);
        });
        unmountRemote(remoteName);
        return;
    }
    deleteRemoteConfig(remoteName);
}

void RcloneService::deleteRemoteConfig(const QString &remoteName)
{
    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, remoteName](int exitCode, QProcess::ExitStatus status) {
        const QString errorOutput = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        const bool success = status == QProcess::NormalExit && exitCode == 0;
        proc->deleteLater();
        if (success)
            refreshRemotes();
        emit remoteActionFinished(QStringLiteral("remove"), remoteName, success,
                                  success ? QString() : (errorOutput.isEmpty()
                                      ? QStringLiteral("rclone could not remove this cloud drive.") : errorOutput));
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc, remoteName](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        const QString message = proc->errorString();
        proc->deleteLater();
        emit remoteActionFinished(QStringLiteral("remove"), remoteName, false, message);
    });
    proc->start(QStringLiteral("rclone"), {QStringLiteral("config"), QStringLiteral("delete"), remoteName});
}

void RcloneService::runConfigurationCommand(const QStringList &args)
{
    if (m_configurationBusy)
        return;

    m_configurationBusy = true;
    emit configurationBusyChanged();
    QProcess *proc = new QProcess(this);
    m_configurationProcess = proc;
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc](int exitCode, QProcess::ExitStatus status) {
        const QString output = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
        const QString stderrText = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        m_configurationProcess = nullptr;
        m_configurationBusy = false;
        emit configurationBusyChanged();
        proc->deleteLater();

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(output.toUtf8(), &parseError);
        if (status != QProcess::NormalExit || exitCode != 0 || parseError.error != QJsonParseError::NoError
            || !document.isObject()) {
            const QString error = !stderrText.isEmpty() ? stderrText
                : (!output.isEmpty() ? output : QStringLiteral("rclone exited without a response."));
            emit configurationFinished(m_configurationRemoteName, false, error);
            return;
        }

        const QJsonObject response = document.object();
        const QString error = response.value(QStringLiteral("Error")).toString();
        const QString state = response.value(QStringLiteral("State")).toString();
        if (!error.isEmpty() && state.isEmpty()) {
            emit configurationFinished(m_configurationRemoteName, false, error);
            return;
        }
        if (!state.isEmpty()) {
            m_configurationState = state;
            emit configurationQuestion(m_configurationRemoteName, state,
                                       response.value(QStringLiteral("Option")).toObject().toVariantMap(), error);
            return;
        }

        const QString configuredRemote = m_configurationRemoteName;
        m_configurationRemoteName.clear();
        m_configurationBackend.clear();
        m_configurationState.clear();
        refreshRemotes();
        emit configurationFinished(configuredRemote, true, QString());
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        const QString message = proc->errorString();
        m_configurationProcess = nullptr;
        m_configurationBusy = false;
        emit configurationBusyChanged();
        proc->deleteLater();
        emit configurationFinished(m_configurationRemoteName, false, message);
    });
    proc->start(QStringLiteral("rclone"), args);
}

bool RcloneService::checkRcloneAvailable() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("rclone")).isEmpty();
}

void RcloneService::ensureMountsBaseDirExists() const
{
    QDir().mkpath(m_mountsBaseDir);
}

bool RcloneService::isRclonePath(const QString &path) const
{
    return isCloudMountPath(path);
}

bool RcloneService::isMounted(const QString &remoteName) const
{
    return m_processes.contains(remoteName) && m_mountSuccessEmitted.value(remoteName);
}

bool RcloneService::isMounting(const QString &remoteName) const
{
    return m_processes.contains(remoteName) && !m_mountSuccessEmitted.value(remoteName);
}

bool RcloneService::isMountedForPath(const QString &path) const
{
    const QString remote = getRemoteNameFromPath(path);
    if (remote.isEmpty())
        return false;
    return isMounted(remote);
}

QString RcloneService::getRemoteNameFromPath(const QString &path) const
{
    if (!isRclonePath(path))
        return {};

    QString sub = path.mid(m_mountsBaseDir.length());
    if (sub.startsWith(QLatin1Char('/'))) {
        sub = sub.mid(1);
    }
    int slashIdx = sub.indexOf(QLatin1Char('/'));
    if (slashIdx != -1) {
        return sub.left(slashIdx);
    }
    return sub;
}

QString RcloneService::getMountPath(const QString &remoteName) const
{
    return m_mountsBaseDir + QStringLiteral("/") + remoteName;
}

void RcloneService::mountRemote(const QString &remoteName)
{
    if (!m_rcloneAvailable) {
        emit mountFinished(remoteName, false, QStringLiteral("rclone executable not found"));
        return;
    }

    if (isMounted(remoteName)) {
        emit mountFinished(remoteName, true, QString());
        return;
    }

    if (isMounting(remoteName)) {
        return;
    }

    const QString mountPath = getMountPath(remoteName);
    QDir().mkpath(mountPath);

    // Clear any stale mount left behind by an earlier run before starting.
    releaseMountPoint(mountPath, [this, remoteName, mountPath]() {
        startRcloneMountProcess(remoteName, mountPath);
    });
}

// fusermount(1) belongs to libfuse2 and fusermount3(1) to libfuse3; a distro
// ships one, the other, or neither. Try both and continue either way: a
// cleanup that cannot run is no reason to strand the caller, and a missing
// binary emits errorOccurred rather than finished, so a chain hung off
// finished alone would simply stop here and never call anybody back.
void RcloneService::releaseMountPoint(const QString &mountPath, const std::function<void()> &then)
{
    runUnmountTool(QStringLiteral("fusermount"), mountPath, [this, mountPath, then](bool ok) {
        if (ok) {
            then();
            return;
        }
        runUnmountTool(QStringLiteral("fusermount3"), mountPath, [then](bool) { then(); });
    });
}

void RcloneService::runUnmountTool(const QString &tool, const QString &mountPath,
                                   const std::function<void(bool)> &done)
{
    QProcess *proc = new QProcess(this);
    const auto settle = [proc, done](bool ok) {
        proc->disconnect();
        proc->deleteLater();
        done(ok);
    };

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [settle](int exitCode, QProcess::ExitStatus status) {
        settle(status == QProcess::NormalExit && exitCode == 0);
    });
    connect(proc, &QProcess::errorOccurred, this, [settle](QProcess::ProcessError) {
        settle(false);
    });

    proc->start(tool, {QStringLiteral("-u"), mountPath});
}

void RcloneService::startRcloneMountProcess(const QString &remoteName, const QString &mountPath)
{
    QProcess *proc = new QProcess(this);
    const quint64 generation = ++m_mountGeneration[remoteName];
    m_processes.insert(remoteName, proc);
    m_mountSuccessEmitted[remoteName] = false;
    emit activeMountsChanged();

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, remoteName, proc](int exitCode, QProcess::ExitStatus) {
        const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        m_processes.remove(remoteName);
        emit activeMountsChanged();

        if (!m_mountSuccessEmitted.value(remoteName)) {
            emit mountFinished(remoteName, false, err.isEmpty() ? QStringLiteral("rclone mount exited unexpectedly.") : err);
        }
        m_mountSuccessEmitted.remove(remoteName);
        proc->deleteLater();
    });

    connect(proc, &QProcess::errorOccurred, this, [this, remoteName, proc](QProcess::ProcessError) {
        const QString err = proc->errorString();
        m_processes.remove(remoteName);
        emit activeMountsChanged();

        if (!m_mountSuccessEmitted.value(remoteName)) {
            emit mountFinished(remoteName, false, err);
        }
        m_mountSuccessEmitted.remove(remoteName);
        proc->deleteLater();
    });

    proc->start(QStringLiteral("rclone"), {
        QStringLiteral("mount"),
        remoteName + QStringLiteral(":"),
        mountPath,
        QStringLiteral("--vfs-cache-mode"),
        QStringLiteral("writes"),
        QStringLiteral("--vfs-cache-max-age"),
        QStringLiteral("72h"),
        QStringLiteral("--no-checksum"),
        QStringLiteral("--vfs-read-chunk-size"),
        QStringLiteral("1M"),
        QStringLiteral("--vfs-read-chunk-size-limit"),
        QStringLiteral("off"),
        QStringLiteral("--buffer-size"),
        QStringLiteral("32M"),
        QStringLiteral("--poll-interval"),
        QStringLiteral("15s")
    });

    // Poll mountpoint checks every 100ms to verify FUSE mount is active
    QTimer *mountTimer = new QTimer(this);
    connect(mountTimer, &QTimer::timeout, this, [this, remoteName, mountPath, mountTimer]() {
        if (!m_processes.contains(remoteName) || m_processes.value(remoteName)->state() != QProcess::Running) {
            mountTimer->stop();
            mountTimer->deleteLater();
            return;
        }

        struct stat st_dir, st_parent;
        QString parentPath = QDir(mountPath).filePath(QStringLiteral(".."));
        if (stat(mountPath.toLocal8Bit().constData(), &st_dir) == 0 &&
            stat(parentPath.toLocal8Bit().constData(), &st_parent) == 0) {

            if (st_dir.st_dev != st_parent.st_dev) {
                mountTimer->stop();
                mountTimer->deleteLater();

                m_mountSuccessEmitted[remoteName] = true;
                emit mountFinished(remoteName, true, QString());
            }
        }
    });

    // Timeout safety: if it doesn't mount in 10 seconds, abort. The poll timer
    // deletes itself as soon as the process is gone, so hold it weakly, and
    // bail out when a newer attempt has taken over this remote -- otherwise a
    // remount inside the timeout window would tear down the fresh mount.
    QPointer<QTimer> pollTimer(mountTimer);
    QTimer::singleShot(10000, this, [this, remoteName, generation, pollTimer]() {
        if (m_mountGeneration.value(remoteName) != generation)
            return;
        if (!m_processes.contains(remoteName) || m_mountSuccessEmitted.value(remoteName))
            return;

        if (pollTimer) {
            pollTimer->stop();
            pollTimer->deleteLater();
        }

        unmountRemote(remoteName);
        emit mountFinished(remoteName, false, QStringLiteral("Mount operation timed out. Verify your rclone remote or network connection."));
    });

    mountTimer->start(100);
}

void RcloneService::unmountRemote(const QString &remoteName)
{
    if (!m_processes.contains(remoteName)) {
        emit unmountFinished(remoteName, true);
        return;
    }

    QProcess *proc = m_processes.value(remoteName);
    if (proc) {
        disconnect(proc, nullptr, this, nullptr);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), proc, &QObject::deleteLater);
        proc->terminate();
        QTimer::singleShot(2000, proc, [proc]() {
            if (proc->state() != QProcess::NotRunning) {
                proc->kill();
            }
        });
    }

    m_processes.remove(remoteName);
    m_mountSuccessEmitted.remove(remoteName);
    emit activeMountsChanged();

    // Release the FUSE mount point without blocking the UI.
    releaseMountPoint(getMountPath(remoteName), [this, remoteName]() {
        emit unmountFinished(remoteName, true);
    });
}
