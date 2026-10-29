#pragma once

#include <QObject>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

class DesktopIntegration : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.FileChooser")
    Q_PROPERTY(bool defaultFileManager READ defaultFileManager NOTIFY statusChanged)
    Q_PROPERTY(bool defaultFilePicker READ defaultFilePicker NOTIFY statusChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
public:
    explicit DesktopIntegration(QObject *parent = nullptr);
    bool defaultFileManager() const { return m_defaultFileManager; }
    bool defaultFilePicker() const { return m_defaultFilePicker; }
    QString statusMessage() const { return m_statusMessage; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool makeDefaultFileManager();
    Q_INVOKABLE bool makeDefaultFilePicker();
    Q_INVOKABLE void completePicker(const QString &requestId, const QStringList &uris, bool accepted);
    Q_INVOKABLE bool claimPicker(const QString &requestId);
public slots:
    uint OpenFile(const QDBusObjectPath &handle, const QString &appId, const QString &parentWindow,
                  const QString &title, const QVariantMap &options, QVariantMap &results);
    uint SaveFile(const QDBusObjectPath &handle, const QString &appId, const QString &parentWindow,
                  const QString &title, const QVariantMap &options, QVariantMap &results);
    uint SaveFiles(const QDBusObjectPath &handle, const QString &appId, const QString &parentWindow,
                   const QString &title, const QVariantMap &options, QVariantMap &results);
signals:
    void statusChanged();
    void pickerRequested(const QString &requestId, const QString &title, const QVariantMap &options,
                         const QString &mode);
private:
    void beginPicker(const QString &title, const QVariantMap &options, const QString &mode);
    void finishPicker(const QString &requestId, const QStringList &uris, bool accepted);
    QHash<QString, QDBusMessage> m_pending;
    QHash<QString, QVariantMap> m_requestOptions;
    QHash<QString, QString> m_requestModes;
    QSet<QString> m_claimed;
    bool m_portalRegistered = false;
    bool m_defaultFileManager = false;
    bool m_defaultFilePicker = false;
    QString m_statusMessage;
};
