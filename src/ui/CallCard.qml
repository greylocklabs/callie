pragma ComponentBehavior: Bound
import Callie.Ui
import QtQuick

/// The card in the window's corner that offers to join the current or next call, as the event
/// popup's Join would. Its X hides it for that call only.
Rectangle {
    id: root

    required property CallOffer offer

    readonly property string callName: ({
                                            "zoom": qsTr("Join Zoom call"),
                                            "meet": qsTr("Join Google Meet"),
                                            "teams": qsTr("Join Teams call")
                                        })[offer.service] || qsTr("Join call")

    width: 300
    height: column.implicitHeight + 2 * Theme.space5
    radius: Theme.radiusXl
    color: Theme.surface
    border.color: Theme.border
    visible: offer.active
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Call: %1").arg(offer.title)

    SoftShadow {
        anchors.fill: parent
        radius: parent.radius
    }

    Column {
        id: column
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: Theme.space5
        }
        spacing: Theme.space3

        Item {
            width: parent.width
            height: Math.max(title.implicitHeight + when.implicitHeight, close.height)

            Text {
                id: title
                anchors {
                    left: parent.left
                    right: close.left
                    rightMargin: Theme.space3
                }
                text: root.offer.title
                elide: Text.ElideRight
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textMd
                font.weight: Theme.weightBold
            }
            Text {
                id: when
                anchors {
                    left: parent.left
                    right: close.left
                    rightMargin: Theme.space3
                    top: title.bottom
                }
                text: root.offer.when
                elide: Text.ElideRight
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textSm
            }
            CloseButton {
                id: close
                objectName: "dismissCall"
                anchors.right: parent.right
                width: 28
                height: 28
                tip: qsTr("Hide for this call")
                Accessible.name: qsTr("Hide for this call")
                onClicked: root.offer.dismiss()
            }
        }

        StickerButton {
            objectName: "joinCall"
            width: parent.width
            height: 40
            accent: true
            glyph: "video"
            glyphStroke: Theme.fineGlyphStroke
            text: root.callName
            onClicked: root.offer.join()
        }
    }
}
