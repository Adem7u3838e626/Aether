#pragma once

#include <QObject>
#include <QFutureWatcher>
#include <QUrl>
#include <QVariantMap>

class AppImageInstaller : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString defaultInstallDirectory READ defaultInstallDirectory CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit AppImageInstaller(QObject *parent = nullptr);

    QString defaultInstallDirectory() const;
    bool busy() const;

    Q_INVOKABLE bool isAppImage(const QString &path) const;
    Q_INVOKABLE QUrl fileUrl(const QString &path) const;
    Q_INVOKABLE QString pathFromUrl(const QString &pathOrUrl) const;
    Q_INVOKABLE void inspect(const QString &path);
    Q_INVOKABLE bool hasConflict(const QString &destinationDirectory,
                                 const QString &name) const;
    Q_INVOKABLE bool install(const QString &sourcePath,
                             const QString &destinationDirectory,
                             const QString &name,
                             const QString &comment,
                             const QString &iconPath,
                             bool replaceExisting);

signals:
    void busyChanged();
    void inspectionFinished(const QString &sourcePath, const QVariantMap &metadata);
    void installationFinished(bool success, const QString &message,
                              const QString &desktopEntryPath);

private:
    void setBusy(bool busy);

    bool m_busy = false;
    QFutureWatcher<QVariantMap> m_inspectionWatcher;
    QFutureWatcher<QVariantMap> m_installationWatcher;
};
