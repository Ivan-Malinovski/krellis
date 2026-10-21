import QtQuick

// One tab per closed edge drawer, on the drawer's slice of the edge, so same-looking windows can be told apart.
// The effect places them (scene covers the work area) and does the hovering; this only paints.
Item {
    id: root
    property var tabs: []   // [{x, y, w, h, edge (0 left, 1 right, 2 top, 3 bottom), icon, title}]
    Repeater {
        model: root.tabs
        Rectangle {
            id: tab
            required property var modelData
            readonly property bool vertical: modelData.edge === 0 || modelData.edge === 1
            x: modelData.x; y: modelData.y; width: modelData.w; height: modelData.h
            color: "#242b3b"
            border.color: "#56627f"
            border.width: 1
            radius: 8
            Image {
                id: icon
                width: 18; height: 18
                source: tab.modelData.icon
                fillMode: Image.PreserveAspectFit
                visible: source != ""
                x: tab.vertical ? (tab.width - width) / 2 : 6
                y: tab.vertical ? 6 : (tab.height - height) / 2
            }
            Text {
                visible: tab.modelData.icon === ""
                text: tab.modelData.title.slice(0, 1).toUpperCase()
                color: "#e6e9f2"; font.pixelSize: 14
                x: tab.vertical ? (tab.width - width) / 2 : 6
                y: tab.vertical ? 6 : (tab.height - height) / 2
            }
            Item {
                // The title runs along the tab: up the left edge, down the right one.
                readonly property real lead: 30
                x: tab.vertical ? 0 : lead; y: tab.vertical ? lead : 0
                width: tab.vertical ? tab.width : Math.max(0, tab.width - lead - 6)
                height: tab.vertical ? Math.max(0, tab.height - lead - 6) : tab.height
                clip: true
                Text {
                    anchors.centerIn: parent
                    width: tab.vertical ? parent.height : parent.width
                    rotation: !tab.vertical ? 0 : tab.modelData.edge === 0 ? -90 : 90
                    horizontalAlignment: tab.vertical ? Text.AlignHCenter : Text.AlignLeft
                    text: tab.modelData.title
                    color: "#e6e9f2"; font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }
    }
}
