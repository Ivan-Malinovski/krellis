import QtQuick

// The workspace minimap: every panel as a tile, the live camera as a frame. The effect feeds
// `tiles` when the layout changes and the camera every frame; clicks are routed through hit().
Item {
    id: root
    property var tiles: []           // [{id, x, y, w, h, focused, floating, vacant, tint}] in 0..1 units
    property real camX: 0
    property real camY: 0
    property real camW: 1
    property real camH: 1
    property real aspect: 0.6
    property bool shown: false
    property string stateText: "Overview"
    property bool floatFirst: false  // the active mode, named in the header
    property string hoverKey: ""
    property int scratchCount: 0     // scratch windows on any output
    property bool scratchEnabled: false
    property bool scratchShown: false
    property bool scratchActive: false   // the active window is a scratch window
    property bool scratchCanMark: false
    property bool pinned: false      // stays out instead of sliding off the screen edge
    // Auto-hide: out while the pointer is over it, for a moment after it appears or is left, or while a
    // square is dragged on it. Otherwise only a slim tab shows at the screen edge.
    readonly property bool hot: hoverKey.indexOf("map:") === 0
    readonly property bool open: shown && (pinned || hot || bandOn || peek.running || linger.running)
    property real slide: open ? 1 : 0
    Behavior on slide { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
    Timer { id: peek; interval: 1800 }
    Timer { id: linger; interval: 450 }
    onShownChanged: if (shown) peek.restart()
    onHotChanged: if (!hot) linger.restart()
    property rect band: Qt.rect(0, 0, 0, 0)   // the square being dragged, in stage pixels
    property bool bandOn: false
    property var picks: []                    // tile ids the square touches
    readonly property rect stageRect: Qt.rect(card.x + stage.x, card.y + stage.y, stage.width, stage.height)
    readonly property bool helpOpen: open && hoverKey === "map:help"
    readonly property var help: scratchEnabled ? helpAll : helpAll.filter(r => r[0] !== "Meta+G" && r[0] !== "Meta+Shift+G")
    readonly property var helpAll: [
        ["Alt / Meta + scroll", "Zoom a column and row"],
        ["Scroll on a title bar", "Zoom, no key needed"],
        ["Drag a square on the map", "Zoom to the tiles it touches"],
        ["Click the Tiling / Floating pill", "Switch mode"],
        ["Click a zoomed tile on the map", "Zoom back out"],
        ["Ctrl+Alt + drag", "Pan"],
        ["Meta + drag", "Zoom to a rectangle"],
        ["Meta+Alt + Up", "Overview"],
        ["Meta+Alt + Down", "Step out"],
        ["Meta+Alt + Left / Right", "Back / forward"],
        ["Meta + [ / ]", "Previous / next panel"],
        ["Meta+Ctrl + arrows", "Focus the tile in that direction"],
        ["Meta+Ctrl+Shift + arrows", "Move the panel that way"],
        ["Meta+Ctrl+R", "Resize with the arrow keys"],
        ["Meta+G", "Show / hide the scratchpad"],
        ["Meta+Shift+G", "Make / unmake a scratch window"],
        ["Meta+Shift+Return", "Maximize panel"],
        ["Meta+Shift+F", "Float / tile panel"],
        ["Meta+Shift+D", "Dock beside the stage"],
        ["Meta+Shift+H", "Hide to the dock"],
        ["Meta+Shift+W", "Close panel"],
        ["Shift + drag a title bar", "Float it (inverts the mode)"],
        ["Meta+Alt+F", "Floating / tiling mode"],
        ["Meta+Alt+B / M / S", "Bar / map / save view"],
        ["Meta+Alt+Escape", "Turn Trellis off"]
    ]
    readonly property real helpH: 30 + help.length * 15 + 8
    readonly property real cardH: mapH + 24 + 26 + (scratchEnabled ? 24 : 0)
    readonly property real mapW: 232
    readonly property real mapH: Math.round(mapW * aspect)
    readonly property bool settled: shown ? (card.opacity > 0.99 && Math.abs(slide - (open ? 1 : 0)) < 0.002 && !peek.running && !linger.running)
                                          : card.opacity < 0.01
    // The scene always has room for the help sheet (a resize does not re-render); only inputRect takes input.
    readonly property real sheetW: 340
    implicitWidth: sheetW
    implicitHeight: cardH + helpH
    readonly property real cardW: mapW + 24
    readonly property real edge: 12    // gap between the open card and the screen edge
    // Collapsed, only the tab takes input; open, the card (and the sheet while it is shown), at its final place.
    readonly property rect inputRect: !open ? Qt.rect(width - tab.width + 4, height - tab.height - 8, tab.width - 4, tab.height)
                                            : (helpOpen ? Qt.rect(0, 0, width, height)
                                                        : Qt.rect(width - edge - cardW, height - cardH, cardW + edge, cardH))

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

    // Shortcut sheet above the card. The scene grows to hold it only while the (?) is hovered, so it never
    // blocks the windows behind it otherwise. It carries the help key itself so it stays open under the pointer.
    Rectangle {
        id: sheet
        visible: root.helpOpen
        x: 0; y: 0
        width: parent.width - root.edge
        height: root.helpH
        radius: 14
        color: "#f21a1d26"
        border.width: 1
        border.color: "#26ffffff"
        opacity: root.helpOpen ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 120 } }
        property string hitKey: "map:help"
        Text { x: 12; y: 9; text: "Trellis shortcuts"; color: "#c9cee0"; font.pixelSize: 12; font.bold: true }
        Column {
            x: 12; y: 30
            spacing: 0
            Repeater {
                model: root.help
                Row {
                    required property var modelData
                    height: 15
                    spacing: 8
                    Text { width: 150; text: modelData[0]; color: "#e6e9f5"; font.pixelSize: 10; elide: Text.ElideRight }
                    Text { text: modelData[1]; color: "#8c93a8"; font.pixelSize: 10 }
                }
            }
        }
    }

    Rectangle {
        id: card
        anchors.bottom: parent.bottom
        property string hitKey: "map:card"
        x: (root.width + 4) + ((root.width - root.edge - width) - (root.width + 4)) * root.slide
        width: root.cardW
        height: root.cardH
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

        Rectangle {
            id: mode
            property string hitKey: "map:mode"
            x: parent.width - 12 - 16 - 6 - 16 - 6 - width
            y: 7
            width: modeText.width + 14
            height: 16
            radius: 8
            color: root.floatFirst ? (root.hoverKey === "map:mode" ? "#66d8ab35" : "#40d8ab35") : (root.hoverKey === "map:mode" ? "#663f8fd6" : "#403f8fd6")
            border.width: 1
            border.color: root.floatFirst ? "#80d8ab35" : "#803f8fd6"
            Text {
                id: modeText
                anchors.centerIn: parent
                text: root.floatFirst ? "Floating" : "Tiling"
                color: "#e6e9f5"
                font.pixelSize: 10
            }
        }

        // The pin keeps the map out; unpinned, it slides off the edge when the pointer is elsewhere.
        Rectangle {
            id: pin
            property string hitKey: "map:pin"
            x: parent.width - 12 - 16 - 6 - 16
            y: 7
            width: 16; height: 16
            radius: 8
            color: root.pinned ? "#4d99b4ff" : (root.hoverKey === "map:pin" ? "#40ffffff" : "#26ffffff")
            border.width: 1
            border.color: root.pinned ? "#99b4ff" : "#33ffffff"
            Item {
                anchors.centerIn: parent
                width: 10; height: 10
                rotation: root.pinned ? 0 : 40
                Behavior on rotation { NumberAnimation { duration: 140 } }
                readonly property color ink: root.pinned ? "#cfdcff" : "#c9cee0"
                Rectangle { x: 3; y: 0; width: 4; height: 4; radius: 1; color: parent.ink }
                Rectangle { x: 1; y: 4; width: 8; height: 1.5; color: parent.ink }
                Rectangle { x: 4.25; y: 5.5; width: 1.5; height: 4.5; color: parent.ink }
            }
        }

        // Hovering the (?) opens the shortcut sheet. The hit area reaches up to the card's top edge so the
        // pointer can travel from the button into the sheet without the hover dropping.
        Item {
            id: helpArea
            x: parent.width - 12 - 16 - 6
            y: 0
            width: 28
            height: 30
            property string hitKey: "map:help"
            Rectangle {
                id: helpButton
                x: 6; y: 7
                width: 16; height: 16
                radius: 8
                color: root.helpOpen ? "#4d99b4ff" : "#26ffffff"
                border.width: 1
                border.color: root.helpOpen ? "#99b4ff" : "#33ffffff"
                Text { anchors.centerIn: parent; text: "?"; color: "#e6e9f5"; font.pixelSize: 10; font.bold: true }
            }
        }

        // The scratchpad: summon or dismiss it, and mark the active window as one.
        Row {
            visible: root.scratchEnabled
            x: 12; y: 30 + root.mapH + 8
            spacing: 6
            Rectangle {
                property string hitKey: "map:scratch"
                width: scratchText.width + 18; height: 18; radius: 9
                opacity: root.scratchCount > 0 ? 1 : 0.5
                color: root.scratchShown ? (root.hoverKey === "map:scratch" ? "#668496ec" : "#408496ec") : (root.hoverKey === "map:scratch" ? "#40ffffff" : "#26ffffff")
                border.width: 1
                border.color: root.scratchShown ? "#808496ec" : "#33ffffff"
                Text {
                    id: scratchText
                    anchors.centerIn: parent
                    text: root.scratchCount === 0 ? "Scratchpad" : "Scratchpad  ·  " + (root.scratchShown ? "hide" : "show" + (root.scratchCount > 1 ? " (" + root.scratchCount + ")" : ""))
                    color: "#e6e9f5"; font.pixelSize: 10
                }
            }
            Rectangle {
                property string hitKey: "map:scratch-mark"
                width: markText.width + 18; height: 18; radius: 9
                opacity: root.scratchCanMark ? 1 : 0.5
                color: root.hoverKey === "map:scratch-mark" ? "#40ffffff" : "#26ffffff"
                border.width: 1
                border.color: "#33ffffff"
                Text {
                    id: markText
                    anchors.centerIn: parent
                    text: root.scratchActive ? "Unmark" : "Mark active"
                    color: "#e6e9f5"; font.pixelSize: 10
                }
            }
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
                    color: modelData.vacant ? "transparent" : modelData.tint
                    readonly property bool picked: root.bandOn && root.picks.indexOf(modelData.id) >= 0
                    opacity: modelData.vacant ? 0.7 : picked ? 1 : (root.bandOn ? 0.4 : (root.hoverKey === hitKey ? 1 : (modelData.focused ? 0.95 : 0.75)))
                    border.width: modelData.vacant ? 1 : picked ? 1.5 : (modelData.focused ? 1.5 : 0)
                    border.color: modelData.vacant ? "#8b94b3" : "#ffffff"
                    Behavior on x { NumberAnimation { duration: 460; easing.type: Easing.OutQuint } }
                    Behavior on y { NumberAnimation { duration: 460; easing.type: Easing.OutQuint } }
                    Behavior on width { NumberAnimation { duration: 460; easing.type: Easing.OutQuint } }
                    Behavior on height { NumberAnimation { duration: 460; easing.type: Easing.OutQuint } }
                    Behavior on opacity { NumberAnimation { duration: 120 } }
                    Rectangle { visible: !modelData.vacant; width: parent.width; height: Math.min(parent.height, 3.5); radius: 1.5; color: "#38ffffff" }
                }
            }

            Rectangle {
                visible: root.bandOn
                x: root.band.x; y: root.band.y
                width: root.band.width; height: root.band.height
                radius: 2
                color: "#2699b4ff"
                border.width: 1
                border.color: "#99b4ff"
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

    // The strip between the open card and the screen edge counts as the card, so the pointer can rest on the
    // spot where the tab was without the map closing under it.
    Item {
        property string hitKey: "map:card"
        visible: root.open
        x: root.width - root.edge; y: root.height - root.cardH
        width: root.edge; height: root.cardH
    }

    // What shows while the map is tucked away: a slim tab at the screen edge. Hovering it brings the map back.
    Rectangle {
        id: tab
        property string hitKey: "map:tab"
        visible: root.shown && root.slide < 0.5
        x: root.width - 20
        y: root.height - height - 8
        width: 24; height: 44
        radius: 8
        color: root.hoverKey === "map:tab" ? "#e62c3142" : "#b31a1d26"
        border.width: 1
        border.color: "#33ffffff"
        opacity: root.shown ? 1 - root.slide * 2 : 0
        Text { x: 4; anchors.verticalCenter: parent.verticalCenter; text: "‹"; color: "#c9cee0"; font.pixelSize: 18 }
    }
}
