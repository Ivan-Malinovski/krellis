import QtQuick

// The tab strip of every tabbed tile, along the top of the tile. The effect places the strips (the scene covers the
// work area) and does all the pointer handling through tabAt(); this only paints.
Item {
    required property QtObject theme
    id: root
    property var strips: []   // [{x, y, w, h, tabs: [{title, selected}], width (of one tab)}]
    Repeater {
        model: root.strips
        Item {
            id: strip
            required property var modelData
            x: modelData.x; y: modelData.y; width: modelData.w; height: modelData.h
            Rectangle {
                anchors.fill: parent
                color: Qt.alpha(root.theme.surface, 0.96)
                border.color: root.theme.cardBorder
                border.width: 1
            }
            Repeater {
                model: strip.modelData.tabs
                Rectangle {
                    id: tab
                    required property var modelData
                    required property int index
                    x: index * strip.modelData.width; y: 0
                    width: strip.modelData.width; height: strip.height
                    color: modelData.selected ? root.theme.accentStrong : "transparent"
                    border.color: modelData.selected ? root.theme.accent : root.theme.cardBorder
                    border.width: 1
                    Text {
                        anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                        verticalAlignment: Text.AlignVCenter
                        text: tab.modelData.title
                        color: tab.modelData.selected ? root.theme.text : root.theme.textDim
                        font.pixelSize: Math.max(8, Math.min(12, strip.height - 10))
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
