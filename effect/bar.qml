import QtQuick

// The bottom bar: navigation on the left, the dock of hidden windows in the middle, the map toggle
// on the right. Input is routed by the effect: it calls hit() for the item under the pointer and
// sets hoverKey, so the QML never has to receive real events.
Item {
    id: root
    property var framings: []        // [{id, name, current}]
    property var labels: []          // dock chips (hidden windows)
    property bool overviewCurrent: false
    property bool canBack: false
    property bool canForward: false
    property bool mapOn: false
    property string countText: ""
    property string hoverKey: ""
    property string pressKey: ""
    property bool naming: false
    property bool nameError: false
    property alias framingName: nameInput.text
    function beginName() { nameInput.text = ""; nameInput.forceActiveFocus(); }


    readonly property color ink: "#e6e9f2"
    readonly property color dim: "#8c93a8"
    readonly property color well: "#3a4054"
    readonly property var curve: [0.2, 0.75, 0.2, 1, 1, 1]

    // The key of the deepest item under (px, py) that declares a hitKey.
    function hit(px, py, item) {
        item = item || root
        var kids = item.children
        for (var i = kids.length - 1; i >= 0; --i) {
            var c = kids[i]
            if (!c.visible || c.enabled === false || c.width <= 0 || c.height <= 0) continue
            var p = item.mapToItem(c, px, py)
            if (p.x < 0 || p.y < 0 || p.x > c.width || p.y > c.height) continue
            var r = hit(p.x, p.y, c)
            if (r) return r
            if (c.hitKey) return c.hitKey
        }
        return ""
    }
    function chipRect(i) {
        chips.forceLayout()
        var it = chipRepeater.itemAt(i)
        if (!it) return Qt.rect(0, 0, 0, 0)
        var p = it.mapToItem(root, 0, 0)
        return Qt.rect(p.x, p.y, it.width, it.height)
    }

    Rectangle {
        anchors.fill: parent
        color: "#e61a1d26"
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: "#26ffffff" }
    }

    component Pill: Rectangle {
        id: pill
        property string hitKey: ""
        property alias label: text.text
        property bool current: false
        property bool enabledLook: true
        property int pad: 12
        height: 30
        width: text.implicitWidth + pad * 2
        radius: 9
        scale: root.pressKey === hitKey && hitKey !== "" ? 0.94 : 1
        opacity: enabledLook ? 1 : 0.4
        color: current ? "#4a5fb4" : (root.hoverKey === hitKey && hitKey !== "" && enabledLook ? "#4b5270" : root.well)
        Behavior on color { ColorAnimation { duration: 120 } }
        Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }
        Text {
            id: text
            anchors.centerIn: parent
            color: root.ink
            font.pixelSize: 13
        }
    }

    Row {
        id: left
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        Pill { hitKey: "overview"; label: "▦  Overview"; current: root.overviewCurrent }
        Row {
            spacing: 6
            Repeater {
                model: root.framings
                Item {
                    required property var modelData
                    property string hitKey: ""
                    height: 30
                    width: go.width + rm.width + 2
                    opacity: 0
                    scale: 0.8
                    Component.onCompleted: { opacity = 1; scale = 1 }
                    Behavior on opacity { NumberAnimation { duration: 260; easing.type: Easing.BezierSpline; easing.bezierCurve: root.curve } }
                    Behavior on scale { NumberAnimation { duration: 260; easing.type: Easing.BezierSpline; easing.bezierCurve: root.curve } }
                    Row {
                        spacing: 2
                        Pill { id: go; hitKey: "framing:" + modelData.id; label: "◲  " + modelData.name; current: modelData.current }
                        Pill { id: rm; hitKey: "rm:" + modelData.id; label: "×"; pad: 8 }
                    }
                }
            }
        }
        Pill { hitKey: "save"; label: root.naming ? "Save" : "+"; pad: 10 }
        Item {
            width: root.naming ? 220 : 0
            height: 30
            clip: true
            opacity: root.naming ? 1 : 0
            Behavior on width { NumberAnimation { duration: 460; easing.type: Easing.OutQuint } }
            Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.22, 1, 0.36, 1, 1, 1] } }
            Rectangle {
                width: 220; height: 30; radius: 9
                color: "#252b3d"
                border.color: root.nameError ? "#eb8b9c" : "#8496ec"
                TextInput {
                    id: nameInput
                    property string hitKey: "name-edit"
                    anchors.fill: parent; anchors.margins: 7
                    enabled: root.naming
                    color: root.ink; selectionColor: "#4a5fb4"; selectedTextColor: "white"
                    font.pixelSize: 13
                    maximumLength: 80
                    selectByMouse: true; clip: true
                    Text { text: root.nameError ? "Enter a name" : "Name this view"; color: root.dim; visible: nameInput.text.length === 0 }
                }
            }
        }
        Pill { visible: root.naming; hitKey: "name-cancel"; label: "×"; pad: 8 }
        Pill { visible: !root.naming; hitKey: "back"; label: "‹"; pad: 10; enabledLook: root.canBack }
        Pill { visible: !root.naming; hitKey: "forward"; label: "›"; pad: 10; enabledLook: root.canForward }
        Text {
            anchors.verticalCenter: undefined
            height: 30
            verticalAlignment: Text.AlignVCenter
            visible: !root.naming
            text: root.countText
            color: root.dim
            font.pixelSize: 12
        }
    }

    Row {
        id: chips
        anchors.centerIn: parent
        spacing: 8
        Repeater {
            id: chipRepeater
            model: root.labels
            Rectangle {
                required property string modelData
                property string hitKey: "dock:" + index
                required property int index
                height: 30
                width: chipText.implicitWidth + 24
                radius: 9
                color: root.hoverKey === hitKey ? "#4b5270" : root.well
                opacity: 0
                scale: 0.6
                Component.onCompleted: { opacity = 1; scale = 1 }
                Behavior on color { ColorAnimation { duration: 120 } }
                Behavior on opacity { NumberAnimation { duration: 380; easing.type: Easing.BezierSpline; easing.bezierCurve: root.curve } }
                Behavior on scale { NumberAnimation { duration: 380; easing.type: Easing.BezierSpline; easing.bezierCurve: root.curve } }
                Text {
                    id: chipText
                    anchors.centerIn: parent
                    text: modelData
                    color: root.ink
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
            }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8
        Pill { hitKey: "map"; label: "◫  Map"; current: root.mapOn }
    }
}
