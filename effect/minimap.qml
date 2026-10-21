import QtQuick

// The workspace minimap: every panel as a tile, the live camera as a frame. The effect feeds
// `tiles` when the layout changes and the camera every frame; clicks are routed through hit().
Item {
    id: root
    property var tiles: []           // [{id, x, y, w, h, focused, floating, tint}] in 0..1 units
    property real camX: 0
    property real camY: 0
    property real camW: 1
    property real camH: 1
    property real aspect: 0.6
    property bool shown: false
    property string stateText: "Overview"
    property string hoverKey: ""
    readonly property real mapW: 232
    readonly property real mapH: Math.round(mapW * aspect)
    readonly property bool settled: shown ? card.opacity > 0.99 : card.opacity < 0.01
    implicitWidth: mapW + 24
    implicitHeight: mapH + 24 + 26

    function hit(px, py, item) {
        item = item || root
        var kids = item.children
        for (var i = kids.length - 1; i >= 0; --i) {
            var c = kids[i]
            if (!c.visible || c.width <= 0 || c.height <= 0) continue
            var p = item.mapToItem(c, px, py)
            if (p.x < 0 || p.y < 0 || p.x > c.width || p.y > c.height) continue
            var r = hit(p.x, p.y, c)
            if (r) return r
            if (c.hitKey) return c.hitKey
        }
        return ""
    }

    Rectangle {
        id: card
        anchors.fill: parent
        radius: 14
        color: "#e61a1d26"
        border.width: 1
        border.color: "#26ffffff"
        opacity: root.shown ? 1 : 0
        transform: Translate { y: root.shown ? 0 : 14; Behavior on y { NumberAnimation { duration: 300; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.2, 0.75, 0.2, 1, 1, 1] } } }
        Behavior on opacity { NumberAnimation { duration: 300; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.2, 0.75, 0.2, 1, 1, 1] } }

        Row {
            x: 12; y: 8
            spacing: 6
            Text { text: "Workspace"; color: "#8c93a8"; font.pixelSize: 11 }
            Text { text: "·  " + root.stateText; color: "#c9cee0"; font.pixelSize: 11 }
        }

        Item {
            id: stage
            x: 12; y: 30
            width: root.mapW
            height: root.mapH
            property string hitKey: "map:overview"

            Rectangle { anchors.fill: parent; radius: 6; color: "#2a2f3d" }

            Repeater {
                model: root.tiles
                Rectangle {
                    required property var modelData
                    property string hitKey: modelData.floating ? "map:focus:" + modelData.id : "map:frame:" + modelData.id
                    x: modelData.x * stage.width + (modelData.floating ? 0.75 : 1.5)
                    y: modelData.y * stage.height + (modelData.floating ? 0.75 : 1.5)
                    width: Math.max(1, modelData.w * stage.width - (modelData.floating ? 1.5 : 3))
                    height: Math.max(1, modelData.h * stage.height - (modelData.floating ? 1.5 : 3))
                    radius: 3
                    color: modelData.tint
                    opacity: root.hoverKey === hitKey ? 1 : (modelData.focused ? 0.95 : 0.75)
                    border.width: modelData.focused ? 1.5 : 0
                    border.color: "#ffffff"
                    Behavior on x { NumberAnimation { duration: 460; easing.type: Easing.OutQuint } }
                    Behavior on y { NumberAnimation { duration: 460; easing.type: Easing.OutQuint } }
                    Behavior on width { NumberAnimation { duration: 460; easing.type: Easing.OutQuint } }
                    Behavior on height { NumberAnimation { duration: 460; easing.type: Easing.OutQuint } }
                    Behavior on opacity { NumberAnimation { duration: 120 } }
                    Rectangle { width: parent.width; height: Math.min(parent.height, 3.5); radius: 1.5; color: "#38ffffff" }
                }
            }

            Rectangle {
                x: root.camX * stage.width + 0.75
                y: root.camY * stage.height + 0.75
                width: Math.max(1, root.camW * stage.width - 1.5)
                height: Math.max(1, root.camH * stage.height - 1.5)
                radius: 4
                color: "transparent"
                border.width: 1.5
                border.color: "#99b4ff"
            }
        }
    }
}
