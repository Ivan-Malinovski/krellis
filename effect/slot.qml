import QtQuick

// The drop slot: where the dragged window will land. The scene covers the work area and the
// effect drives the rectangle every frame, so the texture never has to resize mid-animation.
Item {
    property real slotX: 0
    property real slotY: 0
    property real slotW: 0
    property real slotH: 0
    property real slotOpacity: 0

    Rectangle {
        x: slotX
        y: slotY
        width: slotW
        height: slotH
        opacity: slotOpacity
        radius: 10
        color: Qt.rgba(0.30, 0.45, 0.95, 0.20)
        border.width: 2
        border.color: Qt.rgba(0.45, 0.62, 1.0, 0.85)
    }
}
