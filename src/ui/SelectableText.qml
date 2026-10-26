import Callie.Ui
import QtQuick

/// Read-only text that can be selected and copied, as Text cannot: drag or double-click to
/// select, then Ctrl+C, or right-click to copy the selection, or all of it when nothing is
/// selected. Escape clears a selection before it can close what holds the text.
TextEdit {
    id: root

    readOnly: true
    selectByMouse: true
    activeFocusOnTab: false
    textFormat: TextEdit.PlainText
    wrapMode: TextEdit.Wrap
    color: Theme.text
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    font.family: Theme.fontFamily

    // Claimed before a popup's own Escape shortcut sees it, but only while there is a selection.
    Keys.onShortcutOverride: event => event.accepted = event.key === Qt.Key_Escape
                                      && root.selectedText !== ""
    Keys.onEscapePressed: event => {
        if (root.selectedText === "") {
            event.accepted = false
            return
        }
        root.deselect()
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: menu.popup()
    }
    MenuCard {
        id: menu
        minimumWidth: 120

        MenuEntry {
            text: qsTr("Copy")
            onTriggered: Support.copyText(root.selectedText !== "" ? root.selectedText : root.text)
        }
    }
}
