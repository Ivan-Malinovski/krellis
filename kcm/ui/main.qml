import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

KCM.SimpleKCM {
    id: root
    implicitWidth: Kirigami.Units.gridUnit * 36
    implicitHeight: Kirigami.Units.gridUnit * 32

    readonly property var placements: [
        { text: i18n("Side by side"), value: "side" },
        { text: i18n("Stage (full screen)"), value: "stage" },
        { text: i18n("Floating"), value: "floating" }
    ]
    readonly property var scalings: [
        { text: i18n("Interactive when zoomed out"), value: "interactive" },
        { text: i18n("Inert when zoomed out"), value: "inert" },
        { text: i18n("Never scaled"), value: "none" }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.FormLayout {
            Layout.fillWidth: true

            Kirigami.Separator { Kirigami.FormData.isSection: true; Kirigami.FormData.label: i18n("Windows") }

            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("New windows:")
                text: i18n("Float instead of tiling")
                checked: kcm.floatFirst
                onToggled: kcm.floatFirst = checked
            }
            QQC2.Label {
                Layout.fillWidth: true
                Layout.preferredWidth: Kirigami.Units.gridUnit * 18
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                wrapMode: Text.WordWrap
                opacity: 0.7
                font: Kirigami.Theme.smallFont
                text: i18n("Windows float freely, and dropping one at a screen edge or corner tiles it there.")
            }
            QQC2.ComboBox {
                Kirigami.FormData.label: i18n("New tiled windows open:")
                model: [i18n("Beside the tile under the pointer"), i18n("Beside the focused window"), i18n("At the end of the row")]
                currentIndex: kcm.spawnTarget
                onActivated: index => kcm.spawnTarget = index
            }

            Kirigami.Separator { Kirigami.FormData.isSection: true; Kirigami.FormData.label: i18n("Zoomed view") }

            RowLayout {
                Kirigami.FormData.label: i18n("Neighbour edges:")
                QQC2.SpinBox {
                    from: 0; to: 200; stepSize: 5
                    value: kcm.peek
                    onValueModified: kcm.peek = value
                    textFromValue: (v) => v === 0 ? i18n("Hidden") : i18np("%1 pixel", "%1 pixels", v)
                    valueFromText: (t) => parseInt(t) || 0
                }
            }
            QQC2.Label {
                Layout.fillWidth: true
                Layout.preferredWidth: Kirigami.Units.gridUnit * 18
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                wrapMode: Text.WordWrap
                opacity: 0.7
                font: Kirigami.Theme.smallFont
                text: i18n("How much of the windows next to the framed ones stays visible, dimmed. Clicking one widens the frame to take it in.")
            }
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Minimap:")
                text: i18n("Show the minimap")
                checked: kcm.map
                onToggled: kcm.map = checked
            }
            QQC2.CheckBox {
                text: i18n("Keep pinned open")
                enabled: kcm.map
                checked: kcm.mapPinned
                onToggled: kcm.mapPinned = checked
            }

            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Scratchpad:")
                text: i18n("Enable (Meta+G shows and hides the scratch windows)")
                checked: kcm.scratchpad
                onToggled: kcm.scratchpad = checked
            }
            QQC2.Label {
                Layout.fillWidth: true
                Layout.preferredWidth: Kirigami.Units.gridUnit * 18
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                wrapMode: Text.WordWrap
                opacity: 0.7
                font: Kirigami.Theme.smallFont
                text: i18n("Marked windows float over the canvas and come and go together. Meta+Shift+G marks the active window; Meta+G is also KWin's Grid View, so free it in Shortcuts first. Off, the shortcuts are released and the minimap has no scratchpad row.")
            }

            Kirigami.Separator { Kirigami.FormData.isSection: true; Kirigami.FormData.label: i18n("Motion") }

            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Animations:")
                text: i18n("Smooth camera and layout animations")
                checked: kcm.glide
                onToggled: kcm.glide = checked
            }
            RowLayout {
                Kirigami.FormData.label: i18n("Window landing:")
                enabled: !kcm.glide
                QQC2.SpinBox {
                    from: 0; to: 1000; stepSize: 25
                    value: kcm.landMs
                    onValueModified: kcm.landMs = value
                    textFromValue: (v) => v === 0 ? i18n("Instant") : i18n("%1 ms", v)
                    valueFromText: (t) => parseInt(t) || 0
                }
            }
            QQC2.Label {
                Layout.fillWidth: true
                Layout.preferredWidth: Kirigami.Units.gridUnit * 18
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                wrapMode: Text.WordWrap
                opacity: 0.7
                font: Kirigami.Theme.smallFont
                text: i18n("With animations off, windows jump to their final place; this is how long the short move-and-scale after a resize takes.")
            }
        }

        Kirigami.Separator { Layout.fillWidth: true }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Heading {
                level: 3
                text: i18n("Application rules")
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                QQC2.TextField {
                    id: newApp
                    placeholderText: i18n("Desktop file or window class")
                    Layout.fillWidth: true
                    onAccepted: addButton.clicked()
                }
                QQC2.Button {
                    id: addButton
                    icon.name: "list-add"
                    text: i18n("Add")
                    enabled: newApp.text.trim().length > 0 && !kcm.rules.contains(newApp.text)
                    onClicked: {
                        if (!enabled) return
                        kcm.rules.add(newApp.text)
                        newApp.text = ""
                    }
                }
            }
        }

        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            visible: rulesRepeater.count === 0
            text: i18n("No application rules")
            explanation: i18n("A rule decides where one application's windows open, how they scale and how small they may get. The application is named by its desktop file (for example org.kde.dolphin) or window class.")
        }

        Repeater {
            id: rulesRepeater
            model: kcm.rules
            delegate: Kirigami.AbstractCard {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                required property int index
                required property string app
                required property string placement
                required property string scaling
                required property int minWidth
                required property int minHeight
                required property bool allowSide
                required property bool allowStage
                required property bool allowFloating
                required property bool singleton
                required property bool closable
                required property bool scratchpad

                header: RowLayout {
                    Kirigami.Heading {
                        level: 4
                        text: app
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    QQC2.ToolButton {
                        icon.name: "edit-delete"
                        text: i18n("Remove")
                        display: QQC2.AbstractButton.IconOnly
                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                        onClicked: kcm.rules.remove(index)
                    }
                }

                contentItem: Kirigami.FormLayout {
                    QQC2.ComboBox {
                        Kirigami.FormData.label: i18n("Opens:")
                        model: root.placements
                        textRole: "text"
                        valueRole: "value"
                        currentIndex: root.placements.findIndex(p => p.value === placement)
                        onActivated: kcm.rules.set(index, "placement", currentValue)
                    }
                    QQC2.ComboBox {
                        Kirigami.FormData.label: i18n("Scaling:")
                        model: root.scalings
                        textRole: "text"
                        valueRole: "value"
                        currentIndex: root.scalings.findIndex(p => p.value === scaling)
                        onActivated: kcm.rules.set(index, "scaling", currentValue)
                    }
                    RowLayout {
                        Kirigami.FormData.label: i18n("Smallest size:")
                        QQC2.SpinBox {
                            from: 1; to: 16384; stepSize: 10; editable: true
                            value: minWidth
                            onValueModified: kcm.rules.set(index, "minWidth", value)
                        }
                        QQC2.Label { text: "×" }
                        QQC2.SpinBox {
                            from: 1; to: 16384; stepSize: 10; editable: true
                            value: minHeight
                            onValueModified: kcm.rules.set(index, "minHeight", value)
                        }
                    }
                    QQC2.CheckBox {
                        Kirigami.FormData.label: i18n("May be:")
                        text: i18n("Side by side")
                        checked: allowSide
                        onToggled: kcm.rules.set(index, "allowSide", checked)
                    }
                    QQC2.CheckBox {
                        text: i18n("On stage")
                        checked: allowStage
                        onToggled: kcm.rules.set(index, "allowStage", checked)
                    }
                    QQC2.CheckBox {
                        text: i18n("Floating")
                        checked: allowFloating
                        onToggled: kcm.rules.set(index, "allowFloating", checked)
                    }
                    QQC2.CheckBox {
                        Kirigami.FormData.label: i18n("Other:")
                        text: i18n("Only one window at a time")
                        checked: singleton
                        onToggled: kcm.rules.set(index, "singleton", checked)
                    }
                    QQC2.CheckBox {
                        text: i18n("Can be closed from the title bar")
                        checked: closable
                        onToggled: kcm.rules.set(index, "closable", checked)
                    }
                    QQC2.CheckBox {
                        text: i18n("Scratchpad window (needs the scratchpad enabled above)")
                        checked: scratchpad
                        onToggled: kcm.rules.set(index, "scratchpad", checked)
                    }
                }
            }
        }
    }
}
