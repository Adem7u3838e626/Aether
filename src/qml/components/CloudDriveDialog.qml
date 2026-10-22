import QtQuick
import QtQuick.Layouts
import HyprFM
import Quill as Q

Q.Dialog {
    id: root
    anchors.fill: parent
    z: 1000
    dialogWidth: 500
    title: "Add a Cloud Drive"
    subtitle: "Connect a provider through rclone."
    initialFocusItem: remoteNameField

    property string selectedProvider: "Google Drive"
    property string questionState: ""
    property string questionValue: ""
    property string questionError: ""
    property var questionOption: ({})
    property var questionChoices: []
    property bool configured: false
    property string resultMessage: ""

    function backendFor(provider) {
        if (provider === "Google Drive") return "drive"
        if (provider === "Dropbox") return "dropbox"
        if (provider === "OneDrive") return "onedrive"
        if (provider === "Nextcloud") return "webdav"
        if (provider === "S3-compatible") return "s3"
        return ""
    }

    function resetForm() {
        selectedProvider = "Google Drive"
        remoteNameField.text = ""
        questionState = ""
        questionValue = ""
        questionError = ""
        questionOption = ({})
        questionChoices = []
        configured = false
        resultMessage = ""
        rcloneService.refreshRemotes()
    }

    function receiveConfigurationQuestion(remoteName, state, option, errorString) {
        if (remoteName !== remoteNameField.text.trim())
            return
        questionState = state
        questionOption = option || ({})
        questionError = errorString || ""
        questionChoices = (questionOption.Examples || []).map(example => String(example.Value))
        questionValue = String(questionOption.ValueStr || questionOption.DefaultStr
                               || questionOption.Default || "")
        questionInput.text = questionValue
    }

    function finishConfiguration(remoteName, success, errorString) {
        if (remoteName !== remoteNameField.text.trim())
            return
        if (success) {
            configured = true
            questionState = ""
            questionError = ""
            resultMessage = "“" + remoteName + "” was added to rclone. Select it in Cloud Drives to mount it."
        } else {
            questionError = errorString || "rclone could not configure this remote."
        }
    }

    Text {
        Layout.fillWidth: true
        visible: !rcloneService.rcloneAvailable
        text: "rclone is not installed. Install rclone, then reopen this dialog."
        color: Theme.error
        font.pointSize: Theme.fontNormal
        wrapMode: Text.WordWrap
    }

    Text {
        Layout.fillWidth: true
        visible: rcloneService.rcloneAvailable && !configured && questionState === ""
        text: "Provider"
        color: Theme.subtext
        font.pointSize: Theme.fontSmall
    }

    Q.Dropdown {
        id: providerDropdown
        Layout.fillWidth: true
        visible: rcloneService.rcloneAvailable && !configured && questionState === ""
        enabled: !rcloneService.configurationBusy
        model: ["Google Drive", "Dropbox", "OneDrive", "Nextcloud", "S3-compatible"]
        currentIndex: Math.max(0, model.indexOf(root.selectedProvider))
        onSelected: (_, value) => root.selectedProvider = value
    }

    Text {
        Layout.fillWidth: true
        visible: rcloneService.rcloneAvailable && !configured && questionState === ""
        text: "Remote name"
        color: Theme.subtext
        font.pointSize: Theme.fontSmall
    }

    Q.TextField {
        id: remoteNameField
        Layout.fillWidth: true
        visible: rcloneService.rcloneAvailable && !configured && questionState === ""
        enabled: !rcloneService.configurationBusy
        variant: "filled"
        placeholder: "For example, personal-drive"
        onTextEdited: root.questionError = ""
    }

    Text {
        Layout.fillWidth: true
        visible: questionState !== ""
        text: questionOption.Name || "rclone setup"
        color: Theme.text
        font.pointSize: Theme.fontNormal
        font.bold: true
        wrapMode: Text.WordWrap
    }

    Text {
        Layout.fillWidth: true
        visible: questionState !== "" && (questionOption.Help || "") !== ""
        text: questionOption.Help || ""
        color: Theme.subtext
        font.pointSize: Theme.fontSmall
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
    }

    Q.Dropdown {
        Layout.fillWidth: true
        visible: questionState !== "" && questionChoices.length > 0
        enabled: !rcloneService.configurationBusy
        model: root.questionChoices
        currentIndex: Math.max(0, root.questionChoices.indexOf(root.questionValue))
        onSelected: (_, value) => root.questionValue = value
    }

    Q.TextField {
        id: questionInput
        Layout.fillWidth: true
        visible: questionState !== "" && questionChoices.length === 0
        enabled: !rcloneService.configurationBusy
        variant: "filled"
        placeholder: questionOption.Required ? "Required" : "Optional"
        echoMode: questionOption.IsPassword ? TextInput.Password : TextInput.Normal
        onTextEdited: root.questionValue = text
    }

    Text {
        Layout.fillWidth: true
        visible: rcloneService.configurationBusy
        text: "Waiting for rclone. If your browser opens, finish signing in there."
        color: Theme.subtext
        font.pointSize: Theme.fontSmall
        wrapMode: Text.WordWrap
    }

    Rectangle {
        Layout.fillWidth: true
        visible: root.configured
        implicitHeight: resultText.implicitHeight + 24
        radius: Theme.radius
        color: Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.12)
        border.color: Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.3)
        border.width: 1

        Text {
            id: resultText
            anchors.fill: parent
            anchors.margins: 12
            text: root.resultMessage
            color: Theme.text
            font.pointSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.questionError !== ""
        text: root.questionError
        color: Theme.error
        font.pointSize: Theme.fontSmall
        wrapMode: Text.WordWrap
    }

    Q.Separator { }

    RowLayout {
        Layout.alignment: Qt.AlignRight
        spacing: 12

        Q.Button {
            text: "Cancel"
            variant: "ghost"
            size: "small"
            onClicked: {
                rcloneService.cancelConfiguration()
                root.reject()
            }
        }

        Q.Button {
            text: root.configured ? "Done"
                : rcloneService.configurationBusy ? "Waiting…"
                : root.questionState !== "" ? "Continue"
                : "Connect with rclone"
            variant: "primary"
            size: "small"
            enabled: root.configured || (!rcloneService.configurationBusy
                && rcloneService.rcloneAvailable
                && (root.questionState === ""
                    ? remoteNameField.text.trim() !== ""
                    : (!questionOption.Required || root.questionValue.trim() !== "")))
            onClicked: {
                if (root.configured) {
                    root.accept()
                } else if (root.questionState === "") {
                    rcloneService.beginConfiguration(remoteNameField.text, root.backendFor(root.selectedProvider))
                } else {
                    rcloneService.continueConfiguration(root.questionState, root.questionValue)
                }
            }
        }
    }
}
