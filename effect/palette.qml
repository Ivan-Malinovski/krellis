import QtQuick

// The quick-jump palette: a search field over a short list of windows and saved views. The scene covers the work
// area (a dim wash behind the card); the effect owns the selection, scrolling and ranking and pushes the visible
// slice in `rows`, reads the typed text back from `query`, and routes the pointer through hit(). The only real
// input this item takes is keys, forwarded to the text field.
Item {
    required property QtObject theme
    id: root
    property var rows: []            // the visible slice: [{title, sub, detail, view, badge}]
    property int selected: 0         // index within rows
    property int first: 0            // rank of rows[0]
    property int total: 0
    property real paletteOpacity: 0
    property alias query: input.text
    readonly property int rowHeight: 50

    function begin() { input.text = ""; input.forceActiveFocus() }

    // "row:N" for the Nth visible row, "card" for the rest of the card, "" outside it.
    function hit(px, py) {
        var c = card.mapFromItem(root, px, py)
        if (c.x < 0 || c.y < 0 || c.x > card.width || c.y > card.height) return ""
        var l = list.mapFromItem(root, px, py)
        if (l.x >= 0 && l.x <= list.width && l.y >= 0) {
            var n = Math.floor(l.y / rowHeight)
            if (n >= 0 && n < rows.length) return "row:" + n
        }
        return "card"
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.alpha(theme.halo, 0.38)
        opacity: root.paletteOpacity
    }

    Rectangle {
        id: card
        width: Math.min(root.width - 48, 640)
        height: searchRow.height + 1 + (root.rows.length > 0 ? list.height : emptyRow.height) + footer.height
        x: Math.round((root.width - width) / 2)
        y: Math.round(root.height * 0.14)
        radius: 14
        opacity: root.paletteOpacity
        color: Qt.alpha(theme.surface, 0.98)
        border.width: 1
        border.color: Qt.alpha(theme.wash, 0.2)

        Item {
            id: searchRow
            width: parent.width
            height: 56
            Text {
                id: chevron
                x: 20
                anchors.verticalCenter: parent.verticalCenter
                text: "›"
                color: theme.accent
                font.pixelSize: 26
            }
            TextInput {
                id: input
                anchors { left: chevron.right; right: parent.right; leftMargin: 12; rightMargin: 20; verticalCenter: parent.verticalCenter }
                color: theme.text
                selectionColor: theme.accentStrong
                selectedTextColor: theme.text
                font.pixelSize: 18
                maximumLength: 80
                clip: true
                Text {
                    text: "Jump to a window or saved view"
                    color: theme.textDim
                    font.pixelSize: 18
                    visible: input.text.length === 0
                }
            }
        }
        Rectangle { id: rule; y: searchRow.height; width: parent.width; height: 1; color: Qt.alpha(theme.wash, 0.14) }

        Column {
            id: list
            y: searchRow.height + 1
            width: parent.width
            visible: root.rows.length > 0
            Repeater {
                model: root.rows
                delegate: Item {
                    required property var modelData
                    required property int index
                    width: list.width
                    height: root.rowHeight
                    readonly property bool current: index === root.selected

                    Rectangle {
                        anchors { fill: parent; leftMargin: 8; rightMargin: 8; topMargin: 2; bottomMargin: 2 }
                        radius: 9
                        visible: parent.current
                        color: Qt.alpha(theme.accent, 0.2)
                        border.width: 1
                        border.color: Qt.alpha(theme.accent, 0.5)
                    }
                    Rectangle {
                        id: badge
                        x: 20
                        anchors.verticalCenter: parent.verticalCenter
                        width: 30; height: 30
                        radius: modelData.view ? 8 : 15
                        color: modelData.view ? Qt.alpha(theme.accent, 0.3) : theme.well
                        Text {
                            anchors.centerIn: parent
                            text: modelData.badge
                            color: modelData.view ? theme.accentSoft : theme.textDim
                            font.pixelSize: 14
                            font.bold: true
                        }
                    }
                    Column {
                        anchors { left: badge.right; right: detail.left; leftMargin: 12; rightMargin: 12; verticalCenter: parent.verticalCenter }
                        spacing: 1
                        Text {
                            width: parent.width
                            text: modelData.title
                            color: theme.text
                            font.pixelSize: 15
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: modelData.sub
                            color: theme.textDim
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            visible: modelData.sub !== ""
                        }
                    }
                    Text {
                        id: detail
                        anchors { right: parent.right; rightMargin: 22; verticalCenter: parent.verticalCenter }
                        text: modelData.detail
                        color: theme.textDim
                        font.pixelSize: 12
                    }
                }
            }
        }

        Item {
            id: emptyRow
            y: searchRow.height + 1
            width: parent.width
            height: 64
            visible: root.rows.length === 0
            Text {
                anchors.centerIn: parent
                text: "Nothing matches"
                color: theme.textDim
                font.pixelSize: 14
            }
        }

        Item {
            id: footer
            y: card.height - height
            width: parent.width
            height: 34
            Rectangle { width: parent.width; height: 1; color: Qt.alpha(theme.wash, 0.14) }
            Text {
                x: 20
                anchors.verticalCenter: parent.verticalCenter
                text: "↑↓ choose    ↵ jump    esc close"
                color: theme.textDim
                font.pixelSize: 12
            }
            Text {
                anchors { right: parent.right; rightMargin: 20; verticalCenter: parent.verticalCenter }
                text: root.total > root.rows.length ? (root.first + 1) + "–" + (root.first + root.rows.length) + " of " + root.total
                                                    : (root.total === 1 ? "1 result" : root.total + " results")
                color: theme.textDim
                font.pixelSize: 12
                visible: root.total > 0
            }
        }
    }
}
