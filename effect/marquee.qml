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
    // The drawer a squeezed edge tile would become on release: its strip of the edge and which edge (0 left, 1 right, 2 top, 3 bottom).
    property real hX: 0
    property real hY: 0
    property real hW: 0
    property real hH: 0
    property int hEdge: 0
    property real hOpacity: 0
    property string hLabel: ""

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

    Rectangle {
        x: hX
        y: hY
        width: hW
        height: hH
        opacity: hOpacity
        radius: 8
        color: Qt.rgba(0.30, 0.45, 0.95, 0.30)
        border.width: 2
        border.color: Qt.rgba(0.45, 0.62, 1.0, 0.95)
    }

    Rectangle {
        opacity: hOpacity
        width: hText.implicitWidth + 24
        height: hText.implicitHeight + 12
        radius: height / 2
        color: Qt.rgba(0.10, 0.12, 0.18, 0.88)
        x: hEdge === 0 ? hX + hW + 10 : hEdge === 1 ? hX - width - 10 : hX + (hW - width) / 2
        y: hEdge === 2 ? hY + hH + 10 : hEdge === 3 ? hY - height - 10 : hY + (hH - height) / 2
        Text {
            id: hText
            anchors.centerIn: parent
            text: hLabel
            color: "white"
            font.pixelSize: 13
        }
    }
}
