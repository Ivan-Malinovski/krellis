import QtQuick

// A short confirmation ("Bar on", "Undid: move tile") at the bottom centre of the work area. The scene is a
// band along the bottom edge; the effect drives the text, opacity and rise from its frame clock and the item
// takes no input.
Item {
    required property QtObject theme
    property string text: ""
    property real toastOpacity: 0
    property real rise: 0       // px still to rise; 0 once settled (and always 0 under reduced motion)

    Rectangle {
        id: card
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - height - 18 + rise
        width: label.implicitWidth + 32
        height: label.implicitHeight + 18
        radius: height / 2
        opacity: toastOpacity
        color: Qt.alpha(theme.surface, 0.95)
        border.width: 1
        border.color: Qt.alpha(theme.wash, 0.2)
        Text {
            id: label
            anchors.centerIn: parent
            text: parent.parent.text
            color: theme.text
            font.pixelSize: 13
        }
    }
}
