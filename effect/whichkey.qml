import QtQuick

// The which-key cheat sheet: a centred card with the keyboard shortcuts in columns. The effect fades it with
// hintOpacity from its frame clock; the item takes no input. `groups` is [{name, rows: [{label, keys}]}].
Item {
    required property QtObject theme
    property var groups: []
    property real hintOpacity: 0

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, columns.implicitWidth + 56)
        height: columns.implicitHeight + 52
        radius: 14
        opacity: hintOpacity
        color: Qt.alpha(theme.surface, 0.96)
        border.width: 1
        border.color: Qt.alpha(theme.wash, 0.2)

        Row {
            id: columns
            anchors.centerIn: parent
            spacing: 36
            Repeater {
                model: groups
                delegate: Column {
                    required property var modelData
                    spacing: 7
                    Text {
                        text: modelData.name
                        color: theme.accent
                        font.pixelSize: 12
                        font.bold: true
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 1
                        bottomPadding: 4
                    }
                    Repeater {
                        model: modelData.rows
                        delegate: Row {
                            required property var modelData
                            spacing: 18
                            Text {
                                width: 150
                                text: modelData.label
                                color: theme.text
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                            Text {
                                text: modelData.keys
                                color: theme.textDim
                                font.pixelSize: 13
                            }
                        }
                    }
                }
            }
        }
    }
}
