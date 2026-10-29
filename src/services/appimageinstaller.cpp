#include "services/appimageinstaller.h"

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QUrl>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <utility>

namespace {

QString normalizedPath(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}

QString safeFileStem(const QString &name)
{
    QString stem = name.trimmed();
    stem.replace(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|\\x00-\\x1f]+")), QStringLiteral("-"));
    while (stem.startsWith(QLatin1Char('.')))
        stem.remove(0, 1);
    while (stem.endsWith(QLatin1Char('-')))
        stem.chop(1);
    return stem.isEmpty() ? QStringLiteral("AppImage") : stem;
}

QString desktopValue(QString value)
{
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    value.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    value.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    return value;
}

QString desktopExecValue(QString value)
{
    value.replace(QLatin1Char('%'), QStringLiteral("%%"));
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    value.replace(QLatin1Char('$'), QStringLiteral("\\$"));
    value.replace(QLatin1Char('`'), QStringLiteral("\\`"));
    return QStringLiteral("\"%1\"").arg(value);
}

bool isFlatpak()
{
    return QFileInfo::exists(QStringLiteral("/.flatpak-info"));
}

bool hasAppImageHeader(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile()
        || !info.fileName().endsWith(QStringLiteral(".AppImage"), Qt::CaseInsensitive))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray header = file.read(11);
    return header.size() == 11 && header.startsWith(QByteArrayLiteral("\x7f" "ELF"))
        && (header.mid(8, 3) == QByteArrayLiteral("AI\x01")
            || header.mid(8, 3) == QByteArrayLiteral("AI\x02"));
}

bool runUnsquashfs(const QStringList &arguments, QByteArray *standardOutput,
                   QString *error, int timeoutMs = 15000)
{
    QString program;
    QStringList processArguments;
    if (isFlatpak()) {
        program = QStandardPaths::findExecutable(QStringLiteral("flatpak-spawn"));
        if (program.isEmpty()) {
            if (error)
                *error = QStringLiteral("Host tool access is unavailable in this Flatpak.");
            return false;
        }
        processArguments << QStringLiteral("--host") << QStringLiteral("unsquashfs") << arguments;
    } else {
        program = QStandardPaths::findExecutable(QStringLiteral("unsquashfs"));
        if (program.isEmpty()) {
            if (error)
                *error = QStringLiteral("unsquashfs is not installed.");
            return false;
        }
        processArguments = arguments;
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(program, processArguments);
    if (!process.waitForStarted(3000) || !process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        if (error)
            *error = QStringLiteral("Timed out while reading the AppImage contents.");
        return false;
    }
    if (standardOutput)
        *standardOutput = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error)
            *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        return false;
    }
    return true;
}

QList<qint64> squashfsCandidates(const QString &path)
{
    QList<qint64> candidates;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return candidates;

    constexpr qint64 chunkSize = 1024 * 1024;
    QByteArray carry;
    qint64 position = 0;
    while (!file.atEnd() && candidates.size() < 64) {
        QByteArray chunk = file.read(chunkSize);
        if (chunk.isEmpty())
            break;
        QByteArray data = carry + chunk;
        int found = 0;
        while ((found = data.indexOf("hsqs", found)) >= 0) {
            candidates.append(position - carry.size() + found);
            found += 4;
            if (candidates.size() >= 64)
                break;
        }
        position += chunk.size();
        carry = data.right(3);
    }
    return candidates;
}

bool locateSquashfs(const QString &path, qint64 *offset, QString *error)
{
    const QList<qint64> candidates = squashfsCandidates(path);
    for (qint64 candidate : candidates) {
        QByteArray output;
        if (runUnsquashfs({QStringLiteral("-no-progress"), QStringLiteral("-s"),
                           QStringLiteral("-offset"), QString::number(candidate), path},
                          &output, nullptr, 8000)) {
            *offset = candidate;
            return true;
        }
    }
    if (error)
        *error = candidates.isEmpty()
            ? QStringLiteral("No embedded SquashFS image was found.")
            : QStringLiteral("The embedded SquashFS image could not be read.");
    return false;
}

QByteArray readSquashFile(const QString &imagePath, qint64 offset, const QString &filePath)
{
    QByteArray contents;
    runUnsquashfs({QStringLiteral("-no-progress"), QStringLiteral("-offset"),
                   QString::number(offset), QStringLiteral("-cat"), imagePath, filePath},
                  &contents, nullptr);
    return contents;
}

QStringList listSquashFiles(const QString &imagePath, qint64 offset)
{
    QByteArray output;
    const QStringList common{QStringLiteral("-no-progress"), QStringLiteral("-offset"),
                             QString::number(offset)};
    QStringList arguments = common;
    arguments << QStringLiteral("-max-depth") << QStringLiteral("10")
              << QStringLiteral("-ls") << imagePath;
    if (!runUnsquashfs(arguments, &output, nullptr)) {
        arguments = common;
        arguments << QStringLiteral("-ls") << imagePath;
        if (!runUnsquashfs(arguments, &output, nullptr))
            return {};
    }

    QStringList files;
    const QStringList lines = QString::fromUtf8(output).split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.startsWith(QStringLiteral("squashfs-root/")))
            files.append(line.mid(14));
    }
    return files;
}

QVariantMap inspectAppImage(const QString &sourcePath)
{
    const QFileInfo source(sourcePath);
    QString baseName = source.completeBaseName();
    if (baseName.isEmpty())
        baseName = source.fileName();

    QVariantMap result{
        {QStringLiteral("name"), baseName},
        {QStringLiteral("comment"), QString()},
        {QStringLiteral("iconPath"), QString()},
        {QStringLiteral("iconName"), QStringLiteral("application-x-executable")},
        {QStringLiteral("metadataAvailable"), false}
    };

    QFile file(sourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.insert(QStringLiteral("error"), QStringLiteral("Could not read this AppImage."));
        return result;
    }
    const QByteArray header = file.read(11);
    if (header.size() < 11 || !header.startsWith(QByteArrayLiteral("\x7f" "ELF"))
        || header.mid(8, 3) != QByteArrayLiteral("AI\x02"))
        return result;

    qint64 offset = -1;
    if (!locateSquashfs(sourcePath, &offset, nullptr))
        return result;

    QStringList paths = listSquashFiles(sourcePath, offset);
    QStringList desktopFiles;
    for (const QString &path : std::as_const(paths)) {
        if (path.endsWith(QStringLiteral(".desktop"), Qt::CaseInsensitive))
            desktopFiles.append(path);
    }
    std::sort(desktopFiles.begin(), desktopFiles.end(), [](const QString &a, const QString &b) {
        const int aDepth = a.count(QLatin1Char('/'));
        const int bDepth = b.count(QLatin1Char('/'));
        return aDepth == bDepth ? a < b : aDepth < bDepth;
    });

    QString iconName;
    for (const QString &desktopPath : std::as_const(desktopFiles)) {
        const QByteArray desktopBytes = readSquashFile(sourcePath, offset, desktopPath);
        if (desktopBytes.isEmpty())
            continue;
        QTemporaryFile temporary;
        if (!temporary.open())
            continue;
        temporary.write(desktopBytes);
        temporary.flush();
        QSettings desktop(temporary.fileName(), QSettings::IniFormat);
        desktop.beginGroup(QStringLiteral("Desktop Entry"));
        const QString name = desktop.value(QStringLiteral("Name")).toString().trimmed();
        const QString comment = desktop.value(QStringLiteral("Comment")).toString().trimmed();
        const QString icon = desktop.value(QStringLiteral("Icon")).toString().trimmed();
        desktop.endGroup();
        if (!name.isEmpty()) {
            result[QStringLiteral("name")] = name;
            result[QStringLiteral("comment")] = comment;
            result[QStringLiteral("metadataAvailable")] = true;
            iconName = icon;
            break;
        }
    }

    if (iconName.isEmpty())
        return result;
    const QString wantedBase = QFileInfo(iconName).fileName();
    QString iconPath;
    for (const QString &path : std::as_const(paths)) {
        if (QFileInfo(path).fileName() == wantedBase
            || QFileInfo(path).completeBaseName() == wantedBase) {
            iconPath = path;
            break;
        }
        const QString stem = QFileInfo(path).completeBaseName();
        if (stem == wantedBase && (path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)
                                   || path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)
                                   || path.endsWith(QStringLiteral(".xpm"), Qt::CaseInsensitive))) {
            iconPath = path;
            break;
        }
    }
    if (iconPath.isEmpty()) {
        for (const QString &path : std::as_const(paths)) {
            const QString stem = QFileInfo(path).completeBaseName();
            if ((stem == wantedBase || stem == QFileInfo(wantedBase).completeBaseName())
                && (path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)
                    || path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)
                    || path.endsWith(QStringLiteral(".xpm"), Qt::CaseInsensitive))) {
                iconPath = path;
                break;
            }
        }
    }
    if (iconPath.isEmpty())
        return result;

    const QByteArray iconData = readSquashFile(sourcePath, offset, iconPath);
    if (iconData.isEmpty() || iconData.size() > 16 * 1024 * 1024)
        return result;
    QString extension = QFileInfo(iconPath).suffix().toLower();
    if (extension != QLatin1String("png") && extension != QLatin1String("svg")
        && extension != QLatin1String("xpm"))
        return result;
    const QString cacheLocation = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheLocation.isEmpty())
        return result;
    const QString cacheRoot = cacheLocation + QStringLiteral("/appimage-icons");
    if (!QDir().mkpath(cacheRoot))
        return result;
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(iconData, QCryptographicHash::Sha256).toHex());
    const QString cachedIcon = QDir(cacheRoot).filePath(digest + QLatin1Char('.') + extension);
    QFile output(cachedIcon);
    if (!output.open(QIODevice::WriteOnly) || output.write(iconData) != iconData.size())
        return result;
    result[QStringLiteral("iconPath")] = cachedIcon;
    result[QStringLiteral("iconName")] = cachedIcon;
    return result;
}

QString installAppImage(const QString &sourcePath, const QString &destinationDirectory,
                        const QString &name, const QString &comment, const QString &iconPath,
                        bool replaceExisting, QString *desktopPathOut,
                        bool *sourceRemoveFailed)
{
    if (sourceRemoveFailed)
        *sourceRemoveFailed = false;
    const QString source = normalizedPath(sourcePath);
    const QString destination = normalizedPath(destinationDirectory);
    const QString iconSource = normalizedPath(iconPath);
    const QFileInfo sourceInfo(source);
    if (!hasAppImageHeader(source))
        return QStringLiteral("The selected file is not a valid AppImage.");
    const QString sourceCanonicalPath = sourceInfo.canonicalFilePath();
    if (name.trimmed().isEmpty())
        return QStringLiteral("Enter an application name.");
    const QString stem = safeFileStem(name);
    const QString installDirectory = QDir::cleanPath(destination);
    if (installDirectory.isEmpty() || !QDir().mkpath(installDirectory))
        return QStringLiteral("Could not create the selected install folder.");

    const QString installedImage = QDir(installDirectory).filePath(stem + QStringLiteral(".AppImage"));
    const QString applicationsDirectory = QDir(QDir::homePath())
        .filePath(QStringLiteral(".local/share/applications"));
    if (applicationsDirectory.isEmpty() || !QDir().mkpath(applicationsDirectory))
        return QStringLiteral("Could not create the applications menu folder.");
    const QString desktopFile = QDir(applicationsDirectory).filePath(stem + QStringLiteral(".desktop"));
    const QString iconExtension = QFileInfo(iconSource).suffix().toLower();
    const bool customIcon = QFileInfo(iconSource).isFile()
        && (iconExtension == QLatin1String("png") || iconExtension == QLatin1String("svg")
            || iconExtension == QLatin1String("xpm"));
    const QString iconDirectory = QDir(installDirectory).filePath(QStringLiteral("icons"));
    const QString installedIcon = customIcon
        ? QDir(iconDirectory).filePath(stem + QLatin1Char('.') + iconExtension) : QString();
    const bool conflict = QFileInfo::exists(installedImage) || QFileInfo::exists(desktopFile)
        || (!installedIcon.isEmpty() && QFileInfo::exists(installedIcon));
    if (conflict && !replaceExisting)
        return QStringLiteral("An app with this name is already installed.");

    QTemporaryDir staging(QDir(installDirectory).filePath(QStringLiteral(".aether-install-XXXXXX")));
    if (!staging.isValid())
        return QStringLiteral("Could not create a temporary install folder.");
    const QString stagedImage = QDir(staging.path()).filePath(QStringLiteral("application.AppImage"));
    if (!QFile::copy(source, stagedImage))
        return QStringLiteral("Could not copy the AppImage into the selected folder.");
    QFile::setPermissions(stagedImage, sourceInfo.permissions() | QFile::ExeOwner);

    QString stagedIcon;
    if (customIcon) {
        if (!QDir().mkpath(iconDirectory))
            return QStringLiteral("Could not create the app icon folder.");
        stagedIcon = QDir(staging.path()).filePath(QStringLiteral("application-icon.") + iconExtension);
        if (!QFile::copy(iconSource, stagedIcon))
            return QStringLiteral("Could not copy the selected icon.");
    }

    QString desktopContents = QStringLiteral(
        "[Desktop Entry]\nType=Application\nVersion=1.0\nName=%1\nExec=%2\nIcon=%3\nTerminal=false\n")
        .arg(desktopValue(name.trimmed()), desktopExecValue(installedImage),
             customIcon ? installedIcon : QStringLiteral("application-x-executable"));
    if (!comment.trimmed().isEmpty())
        desktopContents += QStringLiteral("Comment=%1\n").arg(desktopValue(comment.trimmed()));
    desktopContents += QStringLiteral("Categories=Utility;\n");
    const QString backupSuffix = QStringLiteral(".aether-backup-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QStringList backedUp;
    auto backupExisting = [&](const QString &path) {
        if (!QFileInfo::exists(path))
            return true;
        const QString backup = QDir(staging.path()).filePath(QFileInfo(path).fileName() + backupSuffix);
        if (!QFile::rename(path, backup))
            return false;
        backedUp << backup << path;
        return true;
    };
    auto restoreBackups = [&]() {
        for (int i = backedUp.size() - 2; i >= 0; i -= 2)
            QFile::rename(backedUp.at(i), backedUp.at(i + 1));
    };
    if (conflict) {
        if (!backupExisting(installedImage)
            || (!installedIcon.isEmpty() && !backupExisting(installedIcon))) {
            restoreBackups();
            return QStringLiteral("Could not safely replace the existing installation.");
        }
    }

    if (!QFile::rename(stagedImage, installedImage)) {
        restoreBackups();
        return QStringLiteral("Could not place the AppImage in the selected folder.");
    }
    if (customIcon && !QFile::rename(stagedIcon, installedIcon)) {
        QFile::remove(installedImage);
        restoreBackups();
        return QStringLiteral("Could not install the selected app icon.");
    }

    QSaveFile desktopOutput(desktopFile);
    const QByteArray desktopBytes = desktopContents.toUtf8();
    if (!desktopOutput.open(QIODevice::WriteOnly)
        || desktopOutput.write(desktopBytes) != desktopBytes.size()
        || !desktopOutput.commit()) {
        QFile::remove(installedImage);
        if (customIcon)
            QFile::remove(installedIcon);
        restoreBackups();
        return QStringLiteral("Could not write the application menu entry.");
    }

    const QString installedCanonical = QFileInfo(installedImage).canonicalFilePath();
    if (sourceCanonicalPath != installedCanonical && !QFile::remove(source) && sourceRemoveFailed)
        *sourceRemoveFailed = true;
    if (desktopPathOut)
        *desktopPathOut = desktopFile;
    return {};
}

QVariantMap installAppImageResult(const QString &source, const QString &directory,
                                  const QString &name, const QString &comment,
                                  const QString &iconPath, bool replaceExisting)
{
    QString desktopPath;
    bool sourceRemoveFailed = false;
    const QString error = installAppImage(source, directory, name, comment, iconPath,
                                          replaceExisting, &desktopPath, &sourceRemoveFailed);
    return {{QStringLiteral("success"), error.isEmpty()},
            {QStringLiteral("message"), error.isEmpty()
                 ? (sourceRemoveFailed
                        ? QStringLiteral("Installed successfully, but the original file could not be removed.")
                        : QStringLiteral("Installed successfully."))
                 : error},
            {QStringLiteral("desktopEntryPath"), desktopPath}};
}

} // namespace

AppImageInstaller::AppImageInstaller(QObject *parent)
    : QObject(parent)
{
    connect(&m_inspectionWatcher, &QFutureWatcher<QVariantMap>::finished, this, [this]() {
        const QVariantMap metadata = m_inspectionWatcher.result();
        const QString sourcePath = m_inspectionWatcher.property("sourcePath").toString();
        m_inspectionWatcher.setProperty("sourcePath", QString());
        setBusy(false);
        emit inspectionFinished(sourcePath, metadata);
    });
    connect(&m_installationWatcher, &QFutureWatcher<QVariantMap>::finished, this, [this]() {
        const QVariantMap result = m_installationWatcher.result();
        setBusy(false);
        emit installationFinished(result.value(QStringLiteral("success")).toBool(),
                                  result.value(QStringLiteral("message")).toString(),
                                  result.value(QStringLiteral("desktopEntryPath")).toString());
    });
}

QString AppImageInstaller::defaultInstallDirectory() const
{
    return QDir(QDir::homePath()).filePath(QStringLiteral(".local/share/Aether-apps"));
}

bool AppImageInstaller::busy() const
{
    return m_busy;
}

void AppImageInstaller::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

bool AppImageInstaller::isAppImage(const QString &path) const
{
    return hasAppImageHeader(path);
}

QUrl AppImageInstaller::fileUrl(const QString &path) const
{
    return QUrl::fromLocalFile(path);
}

QString AppImageInstaller::pathFromUrl(const QString &pathOrUrl) const
{
    return normalizedPath(pathOrUrl);
}

void AppImageInstaller::inspect(const QString &path)
{
    if (m_busy)
        return;
    setBusy(true);
    m_inspectionWatcher.setProperty("sourcePath", path);
    m_inspectionWatcher.setFuture(QtConcurrent::run([path]() {
        return inspectAppImage(path);
    }));
}

bool AppImageInstaller::hasConflict(const QString &destinationDirectory, const QString &name) const
{
    const QString stem = safeFileStem(name);
    const QDir installDirectory(normalizedPath(destinationDirectory));
    const QString installFile = installDirectory
        .filePath(stem + QStringLiteral(".AppImage"));
    const QString desktopFile = QDir(QDir::homePath())
        .filePath(QStringLiteral(".local/share/applications/") + stem + QStringLiteral(".desktop"));
    return QFileInfo::exists(installFile) || QFileInfo::exists(desktopFile)
        || QFileInfo::exists(installDirectory.filePath(QStringLiteral("icons/") + stem + QStringLiteral(".png")))
        || QFileInfo::exists(installDirectory.filePath(QStringLiteral("icons/") + stem + QStringLiteral(".svg")))
        || QFileInfo::exists(installDirectory.filePath(QStringLiteral("icons/") + stem + QStringLiteral(".xpm")));
}

bool AppImageInstaller::install(const QString &sourcePath, const QString &destinationDirectory,
                                const QString &name, const QString &comment,
                                const QString &iconPath, bool replaceExisting)
{
    if (m_busy || !isAppImage(normalizedPath(sourcePath)))
        return false;
    setBusy(true);
    m_installationWatcher.setFuture(QtConcurrent::run(
        [sourcePath, destinationDirectory, name, comment, iconPath, replaceExisting]() {
            return installAppImageResult(sourcePath, destinationDirectory, name,
                                         comment, iconPath, replaceExisting);
        }));
    return true;
}
