import QtQuick
import QtQuick.Layouts
import Aether

Rectangle {
    id: root
    Accessible.role: Accessible.StatusBar
    Accessible.name: "File operations"

    property bool _showBar: false
    property bool _expanded: false

    function compactTitle() {
        if (transfersModel.count === 0)
            return fileOps.busy ? fileOps.statusText : "Done"
        if (transfersModel.count > 1)
            return fileOps.statusText || (transfersModel.count + " transfers")

        if (fileOps.paused)
            return "Paused · " + (fileOps.currentFile || fileOps.statusText)
        return fileOps.currentFile || fileOps.statusText || "Working…"
    }

    function compactProgress() {
        return fileOps.progress
    }

    function transferIndexById(transferId) {
        for (var i = 0; i < transfersModel.count; ++i) {
            if (transfersModel.get(i).transferId === transferId)
                return i
        }
        return -1
    }

    function syncTransfers() {
        var snapshot = fileOps.activeTransfers
        var seen = ({})

        for (var i = 0; i < snapshot.length; ++i) {
            var transfer = snapshot[i]
            var transferId = transfer.id
            var key = String(transferId)
            var existingIndex = transferIndexById(transferId)
            var progress = transfer.progress

            seen[key] = true

            if (existingIndex >= 0) {
                var existing = transfersModel.get(existingIndex)
                if (progress >= 0 && existing.progress >= 0 && progress < existing.progress)
                    progress = existing.progress
            }

            if (existingIndex < 0) {
                transfersModel.insert(i, {
                    transferId: transferId,
                    statusText: transfer.statusText,
                    progress: progress,
                    speed: transfer.speed,
                    eta: transfer.eta,
                    currentFile: transfer.currentFile,
                    paused: transfer.paused
                })
                continue
            }

            if (existingIndex !== i) {
                transfersModel.move(existingIndex, i, 1)
                existingIndex = i
            }

            transfersModel.setProperty(existingIndex, "statusText", transfer.statusText)
            transfersModel.setProperty(existingIndex, "progress", progress)
            transfersModel.setProperty(existingIndex, "speed", transfer.speed)
            transfersModel.setProperty(existingIndex, "eta", transfer.eta)
            transfersModel.setProperty(existingIndex, "currentFile", transfer.currentFile)
            transfersModel.setProperty(existingIndex, "paused", transfer.paused)
        }

        for (var j = transfersModel.count - 1; j >= 0; --j) {
            if (!seen[String(transfersModel.get(j).transferId)])
                transfersModel.remove(j)
        }
    }

    visible: _showBar || opacity > 0.01
    opacity: _showBar ? 1 : 0
    implicitHeight: _showBar ? contentCol.implicitHeight + 2 * Theme.spacing : 0
    radius: Math.min(Theme.radiusSmall, 5)
    color: Theme.containerColor(Theme.mantle, 0.96)
    border.width: 1
    border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.09)

    Behavior on opacity {
        NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
    }

    ListModel {
        id: transfersModel
    }

    Behavior on implicitHeight {
        NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
    }

    Timer {
        id: hideTimer
        interval: 1500
        onTriggered: root._showBar = false
    }

    Connections {
        target: fileOps
        function onBusyChanged() {
            if (fileOps.busy) {
                hideTimer.stop()
                root._showBar = true
                root._expanded = config.expandTransfersByDefault
            } else {
                hideTimer.restart()
            }
            root.syncTransfers()
        }

        function onActiveTransfersChanged() {
            root.syncTransfers()
        }
    }

    Component.onCompleted: syncTransfers()

    ColumnLayout {
        id: contentCol
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacing
        spacing: 8

        RowLayout {
            id: transferHeader
            Layout.fillWidth: true
            spacing: 6

            Text {
                Layout.fillWidth: true
                text: root._expanded ? "FILE TRANSFERS" : root.compactTitle()
                color: root._expanded ? Theme.muted : Theme.text
                font.pointSize: Theme.fontSmall
                font.bold: !root._expanded
                font.letterSpacing: root._expanded ? 0.7 : 0
                elide: Text.ElideMiddle
            }

            Rectangle {
                implicitWidth: transferCount.implicitWidth + 12
                implicitHeight: 20
                radius: 10
                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.15)
                visible: root._expanded

                Text {
                    id: transferCount
                    anchors.centerIn: parent
                    text: transfersModel.count
                    color: Theme.accent
                    font.pointSize: Theme.fontSmall
                    font.bold: true
                }
            }

            TapHandler {
                onTapped: root._expanded = !root._expanded
            }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
        }

        Rectangle {
            visible: !root._expanded
            Layout.fillWidth: true
            Layout.leftMargin: 2
            Layout.rightMargin: 2
            implicitHeight: 4
            radius: 2
            color: Theme.surface
            clip: true

            Rectangle {
                id: compactProgressFill
                x: root.compactProgress() < 0 ? -width : 0
                width: root.compactProgress() < 0 ? parent.width * 0.3
                    : parent.width * Math.max(0, Math.min(1, root.compactProgress()))
                height: parent.height
                radius: parent.radius
                color: Theme.accent

                Behavior on width {
                    NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
                }

                SequentialAnimation on x {
                    running: !root._expanded && root.compactProgress() < 0
                    loops: Animation.Infinite
                    NumberAnimation {
                        from: -compactProgressFill.width
                        to: compactProgressFill.parent.width
                        duration: 1200
                        easing.type: Easing.InOutCubic
                    }
                }
            }

            TapHandler {
                onTapped: root._expanded = true
            }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
        }

        Repeater {
            model: transfersModel

            delegate: ColumnLayout {
                visible: root._expanded
                Layout.fillWidth: true
                Layout.leftMargin: 2
                Layout.rightMargin: 2
                Layout.topMargin: 1
                Layout.bottomMargin: 1
                spacing: 4

                Rectangle {
                    z: -1
                    anchors.fill: parent
                    anchors.margins: -6
                    radius: Math.min(Theme.radiusSmall, 4)
                    color: Theme.containerColor(Theme.surface, 0.55)
                    border.width: 1
                    border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06)
                }

                required property int transferId
                required property string statusText
                required property real progress
                required property string speed
                required property string eta
                required property string currentFile
                required property bool paused
                required property int index

                // Row 1: status + speed
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        Layout.fillWidth: true
                        text: paused ? "Paused" : statusText
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                        elide: Text.ElideMiddle
                    }

                    Text {
                        visible: !paused && speed !== ""
                        text: speed
                        color: Theme.accent
                        font.pointSize: Theme.fontSmall
                    }
                }

                // Row 2: progress bar + percentage
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 6
                        radius: 3
                        color: Theme.surface
                        clip: true

                        Rectangle {
                            id: progressFill
                            x: progress < 0 ? -width : 0
                            width: progress < 0 ? parent.width * 0.3
                                                : parent.width * Math.max(0, Math.min(1, progress))
                            height: parent.height
                            radius: parent.radius
                            color: Theme.accent

                            Behavior on width {
                                NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
                            }

                            SequentialAnimation on x {
                                running: progress < 0
                                loops: Animation.Infinite
                                NumberAnimation {
                                    from: -progressFill.width
                                    to: progressFill.parent.width
                                    duration: 1200
                                    easing.type: Easing.InOutCubic
                                }
                            }
                        }
                    }

                    Text {
                        visible: progress >= 0
                        text: Math.round(progress * 100) + "%"
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                    }
                }

                // Row 3: current file + ETA
                RowLayout {
                    visible: currentFile !== "" || eta !== ""
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        visible: currentFile !== ""
                        Layout.fillWidth: true
                        text: currentFile
                        color: Theme.muted
                        font.pointSize: Theme.fontSmall
                        elide: Text.ElideMiddle
                    }

                    Text {
                        visible: !paused && eta !== ""
                        text: "~" + eta
                        color: Theme.muted
                        font.pointSize: Theme.fontSmall
                    }
                }

                // Row 4: pause/cancel buttons
                Row {
                    Layout.alignment: Qt.AlignRight
                    spacing: 6

                    HoverRect {
                        width: 24; height: 24
                        onClicked: paused
                            ? fileOps.resumeTransfer(transferId)
                            : fileOps.pauseTransfer(transferId)

                        Loader {
                            anchors.centerIn: parent
                            sourceComponent: paused ? playIcon : pauseIcon
                        }
                    }

                    HoverRect {
                        width: 24; height: 24
                        onClicked: fileOps.cancelTransfer(transferId)

                        IconX {
                            anchors.centerIn: parent
                            size: 14
                            color: Theme.error
                        }
                    }
                }

                // Separator between transfers
                Rectangle {
                    visible: index < transfersModel.count - 1
                    Layout.fillWidth: true
                    height: 1
                    color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                }
            }
        }

        // "Done" state when no active transfers but bar still lingering
        Text {
            visible: !fileOps.busy && root._expanded
            text: "Done"
            color: Theme.subtext
            font.pointSize: Theme.fontSmall
        }
    }

    Component { id: pauseIcon; IconPause { size: 14; color: Theme.subtext } }
    Component { id: playIcon; IconPlay { size: 14; color: Theme.accent } }
}
