import QtQuick

// The rectangle-select gesture: the rubber band, and the framing releasing it would choose.
// The scene covers the work area; the effect drives every property each frame.
Item {
    property real mX: 0
    property real mY: 0
    property real mW: 0
    property real mH: 0
    property real mOpacity: 0
    property real cX: 0
    property real cY: 0
    property real cW: 0
    property real cH: 0
    property real cOpacity: 0
    property string cLabel: ""

    Rectangle {
        x: mX
        y: mY
        width: mW
        height: mH
        opacity: mOpacity
        radius: 6
        color: Qt.rgba(0.45, 0.62, 1.0, 0.10)
        border.width: 1
        border.color: Qt.rgba(0.55, 0.70, 1.0, 0.9)
    }

    Rectangle {
        x: cX
        y: cY
        width: cW
        height: cH
        opacity: cOpacity
        radius: 10
        color: Qt.rgba(0.30, 0.45, 0.95, 0.14)
        border.width: 2
        border.color: Qt.rgba(0.45, 0.62, 1.0, 0.85)

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 12
            width: label.implicitWidth + 24
            height: label.implicitHeight + 12
            radius: height / 2
            color: Qt.rgba(0.10, 0.12, 0.18, 0.88)
            Text {
                id: label
                anchors.centerIn: parent
                text: cLabel
                color: "white"
                font.pixelSize: 13
            }
        }
    }
}
