import QtQuick

// Passive decoration only: input continues to use the painted window rectangles.
Item {
    id: root
    required property QtObject theme
    property var panels: []
    property var frames: []
    function labelCount() {
        var count = 0
        for (var i = 0; i < windows.count; ++i) {
            var card = windows.itemAt(i)
            if (card && card.detail > 0.1 && card.width > 68 && card.height > 52 && card.pieces.length > 0) ++count
        }
        return count
    }
    function setPanel(index, data) {
        var item = windows.itemAt(index)
        if (!item) return
        item.x = data.x; item.y = data.y; item.width = data.w; item.height = data.h
        item.lift = data.lift; item.flash = data.flash; item.urgent = data.urgent; item.detail = data.detail
        item.title = data.title; item.icon = data.icon; item.pieces = data.pieces
    }
    Repeater {
        model: root.frames.length
        Rectangle {
            id: frame
            required property int index
            readonly property var modelData: root.frames[index] || ({x: 0, y: 0, w: 0, h: 0, amount: 0, name: ""})
            x: modelData.x; y: modelData.y; width: modelData.w; height: modelData.h
            radius: 12; color: "transparent"
            border.width: 1; border.color: Qt.alpha(root.theme.accent, 0.35)
            opacity: modelData.amount
            TextMetrics { id: frameMetrics; text: frame.modelData.name; font.pixelSize: 12; font.bold: true }
            Rectangle {
                x: 10; y: 10; height: 24
                width: Math.max(0, Math.min(frame.width - 20, Math.ceil(frameMetrics.advanceWidth) + 24))
                radius: 6; color: Qt.alpha(root.theme.surface, 0.94)
                visible: width > 36 && frame.height > 48
                border.width: 1; border.color: Qt.alpha(root.theme.accent, 0.4)
                Text {
                    x: 10; anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, parent.width - 20)
                    text: frame.modelData.name; elide: Text.ElideRight
                    color: root.theme.accent; font.pixelSize: 12; font.bold: true
                    textFormat: Text.PlainText
                }
            }
        }
    }
    Repeater {
        id: windows
        model: root.panels
        Item {
            id: card
            required property string modelData
            property real lift: 0
            property real flash: 0
            property real urgent: 0
            property real detail: 0
            property string title: ""
            property string icon: ""
            property var pieces: []
            // Concentric translucent borders soften the lifted card's shadow without
            // sampling or distorting client contents. They draw outside the card.
            Repeater {
                model: card.lift > 0 ? 16 : 0
                Rectangle {
                    required property int index
                    readonly property real spread: 1 + index
                    x: -spread; y: -spread; width: card.width + spread * 2; height: card.height + spread * 2
                    radius: 8 + spread; color: "transparent"
                    border.width: 1; border.color: Qt.alpha(root.theme.halo, 0.12 * Math.pow(1 - index / 16, 2) * card.lift)
                }
            }
            Rectangle {
                anchors.fill: parent; anchors.margins: -1
                radius: 8; color: "transparent"; border.width: 2
                border.color: Qt.alpha(root.theme.accent, card.lift * 0.8)
            }
            Repeater {
                // Updating clip rectangles must not destroy and rebuild the label
                // delegates on every compositor frame. Only their count is a model.
                model: card.pieces.length
                Item {
                    required property int index
                    readonly property var piece: card.pieces[index] || ({x: 0, y: 0, w: 0, h: 0})
                    x: piece.x; y: piece.y; width: piece.w; height: piece.h
                    clip: true
                    Item {
                        x: -parent.piece.x; y: -parent.piece.y; width: card.width; height: card.height
                        Rectangle {
                            anchors.fill: parent
                            radius: 8; color: "transparent"; border.width: 2
                            border.color: Qt.alpha(root.theme.accent, card.flash)
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: 8; color: "transparent"; border.width: 3
                            border.color: Qt.alpha(root.theme.warm, card.urgent * 0.9)
                        }
                        Rectangle {
                            opacity: card.detail
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: Math.max(6, parent.height - height - 10)
                            width: Math.min(parent.width - 16, Math.ceil(titleMetrics.advanceWidth) + (card.icon !== "" ? 52 : 28))
                            height: 32; radius: 9
                            color: Qt.alpha(root.theme.surface, 0.94)
                            border.width: 1; border.color: Qt.alpha(root.theme.cardBorder, 0.8)
                            visible: width > 52 && card.height > 52
                            Image {
                                id: app; x: 8; anchors.verticalCenter: parent.verticalCenter
                                width: 20; height: 20; source: card.icon
                                visible: source != ""; fillMode: Image.PreserveAspectFit
                            }
                            TextMetrics { id: titleMetrics; text: card.title; font.pixelSize: 12 }
                            Text {
                                id: label; x: card.icon !== "" ? 36 : 12
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.max(0, parent.width - x - 12)
                                text: card.title; elide: Text.ElideRight
                                color: root.theme.text; font.pixelSize: 12
                                textFormat: Text.PlainText
                            }
                        }
                    }
                }
            }
        }
    }
}
