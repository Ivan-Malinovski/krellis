import QtQuick
Item {
    required property QtObject theme
    id: root
    clip: true
    property var panels: []
    function setTile(index, p) {
        var tile = tiles.itemAt(index)
        if (!tile) { tiles.model = panels; tile = tiles.itemAt(index) }
        if (!tile) return
        tile.x = p.x; tile.y = p.y; tile.width = Math.max(0, p.w); tile.height = Math.max(0, p.h)
        tile.opacity = p.amount; tile.icon = p.icon; tile.title = p.title; tile.pieces = p.pieces
    }
    Repeater {
        id: tiles
        model: root.panels
        Item {
            id: tile
            required property string modelData
            property string icon: ""
            property string title: ""
            property var pieces: []
            opacity: 0
            Repeater {
                model: tile.pieces
                Item {
                    required property var modelData
                    x: modelData.x; y: modelData.y; width: modelData.w; height: modelData.h
                    clip: true
                    Rectangle {
                        x: -modelData.x; y: -modelData.y; width: tile.width; height: tile.height
                        color: root.theme.card; border.color: root.theme.cardBorder
                        radius: Math.min(12, width / 4, height / 4)
                        readonly property real iconSize: Math.max(0, Math.min(40, width - 12, height - 12))
                        Image { anchors.centerIn: parent; width: parent.iconSize; height: width; source: tile.icon; fillMode: Image.PreserveAspectFit; visible: source != "" }
                        Text { anchors.centerIn: parent; text: tile.title.slice(0, 1).toUpperCase(); color: root.theme.text; font.pixelSize: parent.iconSize * 0.7; visible: tile.icon === "" }
                    }
                }
            }
        }
    }
}
