import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import Aether
import Quill as Q

Q.Dialog {
    id: root
    anchors.fill: parent
    z: 1000
    dialogWidth: 560
    title: "Install AppImage"
    subtitle: "Add this application to your app launcher."
    initialFocusItem: nameField
    closeOnOverlayPress: !appImageInstaller.busy
    closeOnEscape: !appImageInstaller.busy

    property string sourcePath: ""
    property string destinationPath: ""
    property string appName: ""
    property string appComment: ""
    property string iconPath: ""
    property string errorMessage: ""
    property bool metadataLoaded: false
    property bool inspecting: false
    property bool confirmingReplace: false
    signal installCompleted(bool success, string message)

    function openFor(path) {
        sourcePath = path
        destinationPath = appImageInstaller.defaultInstallDirectory
        appName = path.substring(path.lastIndexOf("/") + 1).replace(/\.AppImage$/i, "")
        appComment = ""
        iconPath = ""
        errorMessage = ""
        metadataLoaded = false
        inspecting = true
        confirmingReplace = false
        appImageInstaller.inspect(path)
        root.open()
    }

    function requestInstall(replaceExisting) {
        errorMessage = ""
        if (appName.trim() === "") {
            errorMessage = "Enter an application name."
            return
        }
        if (destinationPath.trim() === "") {
            errorMessage = "Choose an install folder."
            return
        }
        if (!replaceExisting && appImageInstaller.hasConflict(destinationPath, appName)) {
            confirmingReplace = true
            return
        }
        var started = appImageInstaller.install(sourcePath, destinationPath,
                                                appName, appComment, iconPath,
                                                replaceExisting)
        if (!started)
            errorMessage = "Could not start installation. Check that the file is a valid AppImage."
        else
            inspecting = false
    }

    Connections {
        target: appImageInstaller

        function onInspectionFinished(path, metadata) {
            if (!root.visible || path !== root.sourcePath)
                return
            root.metadataLoaded = true
            root.inspecting = false
            if (metadata.name)
                root.appName = metadata.name
            root.appComment = metadata.comment || ""
            root.iconPath = metadata.iconPath || ""
        }

        function onInstallationFinished(success, message, desktopEntryPath) {
            if (!root.visible)
                return
            if (success) {
                root.installCompleted(true, "Installed “" + root.appName + "” successfully.")
                root.accept()
            } else if (message === "An app with this name is already installed.") {
                root.confirmingReplace = true
            } else {
                root.errorMessage = message || "The AppImage could not be installed."
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 14

        Rectangle {
            Layout.preferredWidth: 64
            Layout.preferredHeight: 64
            radius: Theme.radiusMedium
            color: Theme.surface
            border.color: Theme.surface1
            border.width: 1

            Image {
                anchors.fill: parent
                anchors.margins: 8
                source: root.iconPath !== ""
                    ? appImageInstaller.fileUrl(root.iconPath)
                    : "image://icon/application-x-executable?theme=" + config.iconTheme
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                Layout.fillWidth: true
                text: root.appName || "AppImage application"
                color: Theme.text
                font.pointSize: Theme.fontNormal
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: root.sourcePath
                color: Theme.muted
                font.pointSize: Theme.fontSmall
                elide: Text.ElideMiddle
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 5
        Text { text: "Application name"; color: Theme.subtext; font.pointSize: Theme.fontSmall }
        Q.TextField {
            id: nameField
            Layout.fillWidth: true
            variant: "filled"
            placeholder: "Application name"
            text: root.appName
            enabled: !appImageInstaller.busy
            onTextEdited: {
                root.appName = text
                root.confirmingReplace = false
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 5
        Text { text: "Comment (optional)"; color: Theme.subtext; font.pointSize: Theme.fontSmall }
        Q.TextField {
            Layout.fillWidth: true
            variant: "filled"
            placeholder: "Short description"
            text: root.appComment
            enabled: !appImageInstaller.busy
            onTextEdited: root.appComment = text
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 5
        Text { text: "Install folder"; color: Theme.subtext; font.pointSize: Theme.fontSmall }
        RowLayout {
            Layout.fillWidth: true
            Q.TextField {
                Layout.fillWidth: true
                variant: "filled"
                text: root.destinationPath
                enabled: !appImageInstaller.busy
                onTextEdited: {
                    root.destinationPath = text
                    root.confirmingReplace = false
                }
            }
            Q.Button {
                text: "Choose…"
                variant: "ghost"
                enabled: !appImageInstaller.busy
                onClicked: folderPicker.open()
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text {
            Layout.fillWidth: true
            text: root.iconPath === "" ? "Using the default application icon." : "Application icon"
            color: Theme.subtext
            font.pointSize: Theme.fontSmall
            elide: Text.ElideMiddle
        }
        Q.Button {
            text: "Choose icon…"
            variant: "ghost"
            enabled: !appImageInstaller.busy
            onClicked: iconPicker.open()
        }
        Q.Button {
            text: "Reset"
            variant: "ghost"
            enabled: !appImageInstaller.busy && root.sourcePath !== ""
            onClicked: {
                root.inspecting = true
                root.errorMessage = ""
                appImageInstaller.inspect(root.sourcePath)
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        visible: root.confirmingReplace
        implicitHeight: replaceMessage.implicitHeight + 18
        radius: Theme.radiusSmall
        color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.12)
        border.color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.35)
        border.width: 1
        Text {
            id: replaceMessage
            anchors.fill: parent
            anchors.margins: 9
            text: "An app with this name is already installed. Replace its AppImage and launcher entry?"
            color: Theme.text
            font.pointSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.errorMessage !== ""
        text: root.errorMessage
        color: Theme.error
        font.pointSize: Theme.fontSmall
        wrapMode: Text.WordWrap
    }

    Text {
        Layout.fillWidth: true
        visible: appImageInstaller.busy
        text: root.inspecting ? "Reading AppImage metadata…" : "Installing AppImage…"
        color: Theme.subtext
        font.pointSize: Theme.fontSmall
    }

    Q.Separator { }

    RowLayout {
        Layout.alignment: Qt.AlignRight
        spacing: 12
        Q.Button {
            text: "Cancel"
            variant: "ghost"
            size: "small"
            enabled: !appImageInstaller.busy
            onClicked: root.reject()
        }
        Q.Button {
            text: root.confirmingReplace ? "Replace and Install" : "Install"
            variant: "primary"
            size: "small"
            enabled: root.metadataLoaded && !appImageInstaller.busy
                && root.appName.trim() !== "" && root.destinationPath.trim() !== ""
            onClicked: root.requestInstall(root.confirmingReplace)
        }
    }

    Item {
        Layout.preferredWidth: 0
        Layout.preferredHeight: 0
        width: 0
        height: 0

        FileDialog {
            id: iconPicker
            title: "Choose an application icon"
            fileMode: FileDialog.OpenFile
            currentFolder: appImageInstaller.fileUrl(root.destinationPath)
            nameFilters: ["Image files (*.png *.svg *.xpm)"]
            onAccepted: {
                root.iconPath = appImageInstaller.pathFromUrl(selectedFile.toString())
                root.confirmingReplace = false
            }
        }

        FolderDialog {
            id: folderPicker
            title: "Choose the install folder"
            currentFolder: appImageInstaller.fileUrl(root.destinationPath)
            onAccepted: {
                root.destinationPath = appImageInstaller.pathFromUrl(selectedFolder.toString())
                root.confirmingReplace = false
            }
        }
    }
}
