#include "desktopintegration.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QUuid>
#include <QUrl>

namespace {
const QString desktopId = QStringLiteral("io.github.adem7u3838e626.Aether.desktop");
bool writeUserPortalMetadata()
{
    if (!qEnvironmentVariableIsEmpty("FLATPAK_ID")) return false;
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString portalDir = data + QStringLiteral("/xdg-desktop-portal/portals");
    const QString serviceDir = data + QStringLiteral("/dbus-1/services");
    if (!QDir().mkpath(portalDir) || !QDir().mkpath(serviceDir)) return false;
    QFile portal(portalDir + QStringLiteral("/aether.portal"));
    if (!portal.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return false;
    QTextStream p(&portal);
    p << "[portal]\nDBusName=org.freedesktop.impl.portal.desktop.aether\n"
         "Interfaces=org.freedesktop.impl.portal.FileChooser;\n"
         "UseIn=Hyprland;wlroots;sway;wayfire;river;\n";
    portal.close();
    QFile service(serviceDir + QStringLiteral("/org.freedesktop.impl.portal.desktop.aether.service"));
    if (!service.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return false;
    QTextStream s(&service);
    const QString executable = qEnvironmentVariable("APPIMAGE", QCoreApplication::applicationFilePath());
    s << "[D-BUS Service]\nName=org.freedesktop.impl.portal.desktop.aether\nExec=\""
      << executable << "\" --portal\n";
    return service.error() == QFileDevice::NoError;
}

QString mimeDefault(const QString &mime)
{
    QProcess process;
    process.start(QStringLiteral("xdg-mime"), {QStringLiteral("query"), QStringLiteral("default"), mime});
    if (!process.waitForFinished(3000) || process.exitCode() != 0) return {};
    return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
}

QString portalConfigPath()
{
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower().split(':').value(0);
    const QString config = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    const QStringList candidates = {config + "/xdg-desktop-portal/" + desktop + "-portals.conf",
                                    config + "/xdg-desktop-portal/portals.conf"};
    for (const QString &path : candidates) if (QFile::exists(path)) return path;
    return candidates.first();
}

bool portalConfigSelectsAether()
{
    QFile f(portalConfigPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QString content = QString::fromUtf8(f.readAll());
    return content.contains(QStringLiteral("org.freedesktop.impl.portal.FileChooser=aether"));
}
}

DesktopIntegration::DesktopIntegration(QObject *parent) : QObject(parent)
{
    refresh();
    auto bus = QDBusConnection::sessionBus();
    m_portalRegistered = bus.registerService(QStringLiteral("org.freedesktop.impl.portal.desktop.aether"))
        && bus.registerObject(QStringLiteral("/org/freedesktop/portal/desktop"), this,
                              QDBusConnection::ExportAllSlots);
    if (m_portalRegistered) {
        m_statusMessage = QStringLiteral("Aether FileChooser portal is running.");
    } else {
        m_statusMessage = QStringLiteral("Could not register Aether's FileChooser portal on D-Bus.");
    }
}

void DesktopIntegration::refresh()
{
    m_defaultFileManager = mimeDefault(QStringLiteral("inode/directory")) == desktopId;
    m_defaultFilePicker = portalConfigSelectsAether() && m_portalRegistered;
    m_statusMessage = m_defaultFilePicker
        ? QStringLiteral("Aether is selected for Open/Save dialogs. Restart the desktop portal if this was just changed.")
        : QStringLiteral("Aether provides Open/Save dialogs when selected as the XDG FileChooser portal.");
    emit statusChanged();
}

bool DesktopIntegration::makeDefaultFileManager()
{
    QProcess p;
    p.start(QStringLiteral("xdg-mime"), {QStringLiteral("default"), desktopId, QStringLiteral("inode/directory")});
    const bool ok = p.waitForFinished(5000) && p.exitCode() == 0;
    refresh();
    if (!ok) m_statusMessage = QStringLiteral("Could not update the folder-opening default.");
    emit statusChanged();
    return ok && m_defaultFileManager;
}

bool DesktopIntegration::makeDefaultFilePicker()
{
    if (!m_portalRegistered) {
        m_statusMessage = QStringLiteral("Aether could not register its FileChooser D-Bus service.");
        emit statusChanged();
        return false;
    }
    if (!writeUserPortalMetadata()) {
        m_statusMessage = qEnvironmentVariableIsEmpty("FLATPAK_ID")
            ? QStringLiteral("Could not install Aether's per-user portal registration files.")
            : QStringLiteral("Aether's Flatpak build cannot register itself as a host XDG portal backend.");
        emit statusChanged();
        return false;
    }
    const QString path = portalConfigPath();
    QFile f(path);
    QString content;
    if (f.exists()) {
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_statusMessage = QStringLiteral("Could not read portal config %1: %2").arg(path, f.errorString());
            emit statusChanged();
            return false;
        }
        content = QString::fromUtf8(f.readAll());
        f.close();
    }
    QStringList lines = content.split('\n');
    bool inPreferred = false, foundPreferred = false, replaced = false;
    QStringList out;
    for (QString line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith('[')) {
            if (inPreferred && !replaced) {
                out << "org.freedesktop.impl.portal.FileChooser=aether;gtk";
                replaced = true;
            }
            inPreferred = trimmed == QStringLiteral("[preferred]");
            foundPreferred = foundPreferred || inPreferred;
        }
        if (inPreferred && trimmed.startsWith(QStringLiteral("org.freedesktop.impl.portal.FileChooser="))) {
            line = QStringLiteral("org.freedesktop.impl.portal.FileChooser=aether;gtk");
            replaced = true;
        }
        out << line;
    }
    if (inPreferred && !replaced) out << "org.freedesktop.impl.portal.FileChooser=aether;gtk";
    if (!foundPreferred) out << "\n[preferred]\norg.freedesktop.impl.portal.FileChooser=aether;gtk";
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        m_statusMessage = QStringLiteral("Could not create the portal config directory for %1.").arg(path);
        emit statusChanged();
        return false;
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_statusMessage = QStringLiteral("Could not write portal config %1: %2").arg(path, output.errorString());
        emit statusChanged();
        return false;
    }
    QTextStream stream(&output);
    stream << out.join('\n');
    stream.flush();
    if (stream.status() != QTextStream::Ok || !output.commit()) {
        m_statusMessage = QStringLiteral("Could not save portal config %1: %2").arg(path, output.errorString());
        emit statusChanged();
        return false;
    }
    refresh();
    QProcess restart;
    restart.start(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("restart"),
                                                QStringLiteral("xdg-desktop-portal.service")});
    if (restart.waitForFinished(5000) && restart.exitCode() == 0) {
        m_statusMessage = QStringLiteral("Aether is selected as the file picker. The desktop portal has been restarted.");
    } else {
        m_statusMessage = QStringLiteral("Aether is selected as the file picker. Restart xdg-desktop-portal or log out and back in to apply it.");
    }
    emit statusChanged();
    return true;
}

void DesktopIntegration::beginPicker(const QString &title, const QVariantMap &options, const QString &mode)
{
    QVariantMap normalizedOptions = options;
    const QByteArray currentFolder = options.value(QStringLiteral("current_folder")).toByteArray();
    if (!currentFolder.isEmpty())
        normalizedOptions.insert(QStringLiteral("initial_folder"), QString::fromLocal8Bit(currentFolder.constData()));
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (calledFromDBus()) {
        const QDBusMessage msg = message();
        setDelayedReply(true);
        m_pending.insert(id, msg);
    }
    m_requestOptions.insert(id, normalizedOptions);
    m_requestModes.insert(id, mode);
    emit pickerRequested(id, title, normalizedOptions, mode);
}

uint DesktopIntegration::OpenFile(const QDBusObjectPath &, const QString &, const QString &,
                                  const QString &title, const QVariantMap &options, QVariantMap &)
{ beginPicker(title, options, options.value(QStringLiteral("directory")).toBool() ? QStringLiteral("directory") : QStringLiteral("open")); return 1; }

uint DesktopIntegration::SaveFile(const QDBusObjectPath &, const QString &, const QString &,
                                  const QString &title, const QVariantMap &options, QVariantMap &)
{ beginPicker(title, options, QStringLiteral("save")); return 1; }

uint DesktopIntegration::SaveFiles(const QDBusObjectPath &, const QString &, const QString &,
                                   const QString &title, const QVariantMap &options, QVariantMap &)
{ beginPicker(title, options, QStringLiteral("save-files")); return 1; }

void DesktopIntegration::completePicker(const QString &requestId, const QStringList &uris, bool accepted)
{
    QStringList normalized;
    for (const QString &item : uris) {
        const QUrl url(item);
        normalized << (url.isValid() && !url.scheme().isEmpty() ? url.toString()
                                                                  : QUrl::fromLocalFile(item).toString());
    }
    finishPicker(requestId, normalized, accepted);
    m_claimed.remove(requestId);
}

bool DesktopIntegration::claimPicker(const QString &requestId)
{
    if (m_claimed.contains(requestId)) return false;
    m_claimed.insert(requestId);
    return true;
}

void DesktopIntegration::finishPicker(const QString &requestId, const QStringList &uris, bool accepted)
{
    auto it = m_pending.find(requestId);
    if (it == m_pending.end()) return;
    const QDBusMessage call = it.value();
    m_pending.erase(it);
    QVariantMap results;
    if (accepted) {
        QStringList resultUris = uris;
        if (m_requestModes.value(requestId) == QStringLiteral("save-files") && !uris.isEmpty()) {
            const QString folder = QUrl(uris.first()).toLocalFile();
            const QVariantList names = m_requestOptions.value(requestId).value(QStringLiteral("files")).toList();
            resultUris.clear();
            for (const QVariant &nameValue : names) {
                QByteArray bytes = nameValue.toByteArray();
                QString name = bytes.isEmpty() ? nameValue.toString() : QString::fromLocal8Bit(bytes.constData());
                while (name.endsWith(QChar::Null)) name.chop(1);
                if (name.isEmpty()) continue;
                QString path = QDir(folder).filePath(name);
                const QFileInfo original(path);
                for (int suffix = 1; QFileInfo::exists(path); ++suffix) {
                    const QString candidate = original.suffix().isEmpty()
                        ? original.completeBaseName() + QStringLiteral(" (%1)").arg(suffix)
                        : original.completeBaseName() + QStringLiteral(" (%1).").arg(suffix) + original.suffix();
                    path = QDir(folder).filePath(candidate);
                }
                resultUris << QUrl::fromLocalFile(path).toString();
            }
        }
        results.insert(QStringLiteral("uris"), resultUris);
    }
    m_requestOptions.remove(requestId);
    m_requestModes.remove(requestId);
    QDBusMessage reply = call.createReply({QVariant::fromValue<uint>(accepted ? 0u : 1u), QVariant::fromValue(results)});
    QDBusConnection::sessionBus().send(reply);
}

