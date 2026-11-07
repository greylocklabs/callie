pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick
import QtQuick.Controls

/// The connected Google and CalDAV accounts: each with how its sync is going,
/// a way to sign in again, and removal; then connecting another.
Column {
    id: root

    /// The source's syncReport, for each account's status line.
    property var report: []

    /// One account's line in the report: Google and Microsoft accounts can
    /// share an address, so the provider counts too.
    function entryFor(provider, id) {
        for (const entry of report)
            if (entry.account === id && (entry.provider || provider) === provider)
                return entry
        return null
    }

    /// What the account is known by in settings, apart from another provider's
    /// with the same address.
    function keyFor(account) {
        return account.provider === "microsoft" ? "microsoft:" + account.id : account.id
    }

    /// Signs in to `account` again: mends a broken sign-in, and grants what Callie has asked for
    /// since, such as reading contacts. CalDAV's opens its form, filled in.
    function reconnect(account) {
        if (account.provider !== "caldav") {
            Accounts.reconnect(account.id)
            return
        }
        root.calDavOpen = true
        calDav.prefill(account)
    }

    /// The provider, as the person knows it.
    function kindOf(provider) {
        return provider === "microsoft" ? qsTr("Microsoft") : provider === "caldav" ? qsTr(
                                                                                          "CalDAV") :
                                                                                      qsTr("Google")
    }

    width: parent ? parent.width : 0
    spacing: Theme.space4

    Text {
        visible: Accounts.accounts.length === 0
        width: parent.width
        wrapMode: Text.Wrap
        text: qsTr(
                  "No accounts yet. Connect a Google or Microsoft account, or a CalDAV one such as Fastmail, iCloud or Nextcloud, to see its calendars here.")
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textMd
    }

    Repeater {
        model: Accounts.accounts

        Column {
            id: account

            required property var modelData
            /// A CalDAV account's calendars, to choose which sync, are shown.
            property bool showCalendars: false

            width: root.width
            spacing: Theme.space3

            Item {
                id: row

                readonly property var modelData: account.modelData
                readonly property bool caldav: modelData.provider === "caldav"
                readonly property bool microsoft: modelData.provider === "microsoft"
                /// The account can sign in again or be removed here.
                readonly property bool manageable: caldav ? Accounts.calDavReady : microsoft
                                                            ? Accounts.microsoftReady :
                                                              Accounts.unavailable === ""
                readonly property var entry: root.entryFor(modelData.provider, modelData.id)
                /// Callie asks for more than this account granted when it signed in.
                readonly property bool missing: !!entry && (entry.missingScopes || []).length > 0
                readonly property url photo: Settings.accountPhotos[root.keyFor(modelData)] || ""
                readonly property string problem: entry ? (entry.error || (entry.problems.length > 0
                                                                           ? entry.problems[0] :
                                                                             "")) : ""
                /// Something about the account wants seeing to: a failed sync or a sign-in short
                /// of what Callie asks for.
                readonly property bool attention: problem !== "" || missing
                /// When it last synced, as its tooltip says it.
                readonly property string syncedText: !entry || isNaN(entry.lastSynced.getTime())
                                                     ? qsTr("Not synced yet") : qsTr(
                                                           "Synced %1").arg(Settings.times.time(
                                                                                entry.lastSynced))
                /// Remove was chosen from the menu and waits for Remove or Keep.
                property bool confirming: false

                width: root.width
                height: Math.max(avatar.height, details.implicitHeight, actions.implicitHeight)

                Rectangle {
                    id: avatar
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28
                    height: 28
                    radius: width / 2
                    color: Theme.tint(Theme.accent, 0.25)

                    // The account's photo, round, over its letter until it loads.
                    RoundPhoto {
                        id: photo
                        anchors.fill: parent
                        source: row.photo
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: !photo.ready
                        text: row.modelData.id.charAt(0).toUpperCase()
                        color: Theme.accent
                        font.family: Theme.displayFontFamily
                        font.pixelSize: Theme.textMd
                        font.weight: Theme.weightBold
                    }
                }

                // The address and provider on one line; a second only when something is wrong.
                Column {
                    id: details
                    anchors {
                        left: avatar.right
                        leftMargin: Theme.space3
                        right: actions.left
                        rightMargin: Theme.space3
                        verticalCenter: parent.verticalCenter
                    }

                    // The address, then the provider, quieter.
                    Row {
                        width: parent.width
                        spacing: Theme.space2

                        Text {
                            id: name
                            width: Math.min(implicitWidth, parent.width - (kind.visible ? kind.width
                                                                                          + parent.spacing :
                                                                                          0))
                            text: row.confirming ? qsTr("Remove %1?").arg(row.modelData.id) :
                                                   row.modelData.id

                            textFormat: Text.PlainText
                            elide: Text.ElideMiddle
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textMd
                            font.weight: Theme.weightStrong

                            HoverHandler {
                                id: nameHover
                            }
                            // When it last synced, on resting on the address.
                            Tip {
                                visible: nameHover.hovered && !row.confirming
                                delay: Theme.durSlow * 2
                                text: row.syncedText
                            }
                        }
                        Text {
                            id: kind
                            visible: !row.confirming
                            text: root.kindOf(row.modelData.provider)
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.textMd
                        }
                    }
                    Text {
                        visible: row.attention && !row.confirming
                        width: parent.width
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        text: row.problem !== "" ? row.problem : qsTr(
                                                       "Reconnect to grant new permissions")
                        color: row.problem !== "" ? Theme.danger : Theme.accent
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.textSm
                    }
                }

                Row {
                    id: actions
                    anchors {
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: Theme.space2

                    // Out in the row only when the account needs it; otherwise in the menu.
                    StickerButton {
                        visible: row.manageable && row.attention && !row.confirming
                        enabled: !Accounts.busy
                        accent: true
                        text: qsTr("Reconnect")
                        onClicked: root.reconnect(row.modelData)
                    }
                    StickerButton {
                        visible: row.confirming
                        enabled: !Accounts.busy
                        destructive: true
                        armed: true
                        text: qsTr("Remove")
                        onClicked: {
                            row.confirming = false
                            Accounts.remove(row.modelData.provider, row.modelData.id)
                        }
                    }
                    StickerButton {
                        visible: row.confirming
                        text: qsTr("Keep")
                        onClicked: row.confirming = false
                    }
                    StickerButton {
                        id: more
                        visible: !row.confirming && (row.manageable || row.caldav)
                        glyph: "more"
                        glyphStroke: Theme.smallGlyphStroke
                        Accessible.name: qsTr("More for %1").arg(row.modelData.id)
                        onClicked: moreMenu.open()

                        MenuCard {
                            id: moreMenu
                            y: parent.height + Theme.space1
                            x: parent.width - width

                            MenuEntry {
                                visible: row.manageable
                                height: visible ? implicitHeight : 0
                                enabled: !Accounts.busy
                                text: qsTr("Reconnect")
                                onTriggered: root.reconnect(row.modelData)
                            }
                            MenuEntry {
                                visible: row.caldav && Accounts.calDavReady
                                height: visible ? implicitHeight : 0
                                text: account.showCalendars ? qsTr("Hide calendars") : qsTr(
                                                                  "Choose calendars")
                                onTriggered: account.showCalendars = !account.showCalendars
                            }
                            // Removing asks first, since the account's sign-in goes too.
                            MenuEntry {
                                visible: row.microsoft ? Accounts.canRemove : row.manageable
                                height: visible ? implicitHeight : 0
                                enabled: !Accounts.busy
                                text: qsTr("Remove...")
                                onTriggered: row.confirming = true
                            }
                        }
                    }
                }
            }

            // Under the account, lined up with its name, once asked for.
            CalDavCalendarList {
                visible: row.caldav && Accounts.calDavReady && account.showCalendars
                x: avatar.width + Theme.space3
                width: parent.width - x
                accountId: account.modelData.id
            }
        }
    }

    /// The CalDAV form is open.
    property bool calDavOpen: false

    /// The form's Connect is being checked with the server.
    property bool calDavChecking: false

    Connections {
        target: Accounts
        // Done checking without an error: connected, so the form has done its work.
        function onStateChanged() {
            if (!root.calDavChecking || Accounts.busy)
                return
            root.calDavChecking = false
            if (Accounts.error === "") {
                root.calDavOpen = false
                calDav.clear()
            }
        }
    }

    CalDavForm {
        id: calDav
        visible: root.calDavOpen
        width: parent.width
        onSubmitted: root.calDavChecking = true
        onCancelled: {
            root.calDavOpen = false
            clear()
        }
    }

    Flow {
        visible: !root.calDavOpen
        width: parent.width
        spacing: Theme.space3

        StickerButton {
            objectName: "connectAccount"
            enabled: !Accounts.busy
            glyph: "plus"
            text: qsTr("Connect an account")
            onClicked: connectMenu.open()

            MenuCard {
                id: connectMenu
                y: parent.height + Theme.space1

                MenuEntry {
                    enabled: Accounts.unavailable === ""
                    text: qsTr("Google")
                    onTriggered: Accounts.connectGoogle()
                }
                MenuEntry {
                    visible: Accounts.microsoftReady
                    height: visible ? implicitHeight : 0
                    text: qsTr("Microsoft")
                    onTriggered: Accounts.connectMicrosoft()
                }
                MenuEntry {
                    enabled: Accounts.calDavReady
                    text: qsTr("CalDAV: Fastmail, iCloud, Nextcloud and others")
                    onTriggered: root.calDavOpen = true
                }
            }
        }
        StickerButton {
            visible: Accounts.signingIn
            text: qsTr("Cancel")
            onClicked: Accounts.cancel()
        }
    }

    Text {
        visible: text !== ""
        width: parent.width
        wrapMode: Text.Wrap
        // What is happening now, ahead of why Google cannot be connected here.
        text: Accounts.error || Accounts.status || Accounts.unavailable
        color: Accounts.error !== "" ? Theme.danger : Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.textSm
    }
}
