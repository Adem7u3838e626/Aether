import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Aether
import Quill as Q

Q.Dialog {
    id: root
    anchors.fill: parent
    z: 1000
    dialogWidth: 430
    title: "Archive password"
    subtitle: root.fileName
    initialFocusItem: passwordField

    property string filePath: ""
    property string fileName: {
        var parts = String(root.filePath).split("/")
        return parts[parts.length - 1] || root.filePath
    }
    property string errorText: ""
    // A password has been handed over and we are waiting to hear whether it
    // worked. The dialog stays up throughout: closing and reopening it on a
    // wrong password reads as a flicker and loses what you typed.
    property bool checking: false

    signal confirmed(string password)

    function openFor(path) {
        root.filePath = path
        root.errorText = ""
        root.checking = false
        passwordField.text = ""
        root.open()
    }

    // The password was refused. Stay open, say so, and offer the field back
    // with the failed attempt selected so typing replaces it.
    function failed() {
        root.checking = false
        root.errorText = "Wrong password. Try again."
        passwordField.inputItem.forceActiveFocus()
        passwordField.inputItem.selectAll()
    }

    // The password worked; nothing left to ask.
    function succeeded() {
        root.checking = false
        root.accept()
    }

    function submit() {
        if (root.checking)
            return
        root.errorText = ""
        var pass = passwordField.text
        if (pass === "") {
            root.errorText = "Enter the archive password."
            return
        }
        root.checking = true
        root.confirmed(pass)
    }

    onOpened: Qt.callLater(function() { passwordField.inputItem.forceActiveFocus() })

    Q.TextField {
        id: passwordField
        objectName: "archivePasswordField"
        Layout.fillWidth: true
        variant: "filled"
        placeholder: "Password"
        echoMode: TextInput.Password
        enabled: !root.checking
        inputItem.Keys.onReturnPressed: root.submit()
        onTextChanged: root.errorText = ""
    }

    Text {
        Layout.fillWidth: true
        visible: root.errorText !== ""
        text: root.errorText
        color: Theme.error
        font.pointSize: Theme.fontSmall
        wrapMode: Text.WordWrap
    }

    RowLayout {
        Layout.alignment: Qt.AlignRight
        spacing: 12

        Q.Button {
            text: "Cancel"
            variant: "ghost"
            size: "small"
            onClicked: root.reject()
        }

        Q.Button {
            text: root.checking ? "Checking\u2026" : "Unlock"
            variant: "primary"
            size: "small"
            enabled: !root.checking
            onClicked: root.submit()
        }
    }
}
