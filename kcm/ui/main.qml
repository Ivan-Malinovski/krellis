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
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Dragging a window:")
                text: i18n("Shake it to make it float")
                checked: kcm.shakeFloat
                onToggled: kcm.shakeFloat = checked
            }
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Closing a window:")
                text: i18n("Focus its neighbour in the layout")
                checked: kcm.closeFocus
                onToggled: kcm.closeFocus = checked
            }
            RowLayout {
                Kirigami.FormData.label: i18n("Tiny windows:")
                QQC2.SpinBox {
                    from: 0; to: 1000; stepSize: 20
                    value: kcm.iconBelow
                    onValueModified: kcm.iconBelow = value
                    textFromValue: (v) => v === 0 ? i18n("Always live") : i18n("Icon below %1 px", v)
                    valueFromText: (t) => parseInt(t) || 0
                }
            }
            RowLayout {
                Kirigami.FormData.label: i18n("Smallest tile:")
                QQC2.SpinBox {
                    from: 1; to: 4000; stepSize: 20
                    value: kcm.tileMinWidth
                    onValueModified: kcm.tileMinWidth = value
                    textFromValue: (v) => i18n("%1 px wide", v)
                    valueFromText: (t) => parseInt(t) || 1
                }
                QQC2.SpinBox {
                    from: 1; to: 4000; stepSize: 20
                    value: kcm.tileMinHeight
                    onValueModified: kcm.tileMinHeight = value
                    textFromValue: (v) => i18n("%1 px tall", v)
                    valueFromText: (t) => parseInt(t) || 1
                }
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
                model: [i18n("Beside the tile under the pointer"), i18n("Beside the focused window"), i18n("At the end of the row"), i18n("Halving the focused tile (dwindle)")]
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

            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Attention:")
                text: i18n("Mark windows that ask for attention (Meta+U jumps to one)")
                checked: kcm.urgent
                onToggled: kcm.urgent = checked
            }
            QQC2.Label {
                Layout.fillWidth: true
                Layout.preferredWidth: Kirigami.Units.gridUnit * 18
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                wrapMode: Text.WordWrap
                opacity: 0.7
                font: Kirigami.Theme.smallFont
                text: i18n("When a window you are not using asks for attention (a chat message, a finished build, a program that may not take focus), it gets an amber outline, an amber tile on the minimap and a first place in the quick-jump list.")
            }

            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Pin:")
                text: i18n("Enable (Meta+Shift+P pins the active tile)")
                checked: kcm.pin
                onToggled: kcm.pin = checked
            }
            QQC2.Label {
                Layout.fillWidth: true
                Layout.preferredWidth: Kirigami.Units.gridUnit * 18
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                wrapMode: Text.WordWrap
                opacity: 0.7
                font: Kirigami.Theme.smallFont
                text: i18n("A pinned tile keeps its place in the layout, and while it is scrolled out of view it shows as a live thumbnail in the bottom-left corner; click it to go back. Off, the shortcut is released and pins are dropped.")
            }

            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Tabs:")
                text: i18n("Enable tabbed tiles")
                checked: kcm.tabs
                onToggled: kcm.tabs = checked
            }
            QQC2.Label {
                Layout.fillWidth: true
                Layout.preferredWidth: Kirigami.Units.gridUnit * 18
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                wrapMode: Text.WordWrap
                opacity: 0.7
                font: Kirigami.Theme.smallFont
                text: i18n("Several windows can share one tile and show as tabs along its top. Drop a window on a tile's title bar to add it as a tab; drag a tab, or the bare strip, out to split it off again. Switched off, tabbed tiles are split back into separate tiles.")
            }

            Kirigami.Separator { Kirigami.FormData.isSection: true; Kirigami.FormData.label: i18n("Motion") }

            QQC2.ComboBox {
                Kirigami.FormData.label: i18n("Animation profile:")
                model: [i18n("Trellis"), i18n("Hyprland")]
                currentIndex: kcm.animationProfile === "hyprland" ? 1 : 0
                onActivated: kcm.animationProfile = currentIndex === 1 ? "hyprland" : "trellis"
            }
            QQC2.ComboBox {
                Kirigami.FormData.label: i18n("Window opening and closing:")
                visible: kcm.animationProfile === "hyprland"
                model: [i18n("Pop in (87%)"), i18n("Slide from nearest edge"), i18n("Vertical collapse")]
                currentIndex: ["popin", "slide", "gnomed"].indexOf(kcm.windowAnimation)
                onActivated: kcm.windowAnimation = ["popin", "slide", "gnomed"][currentIndex]
            }
            QQC2.Label {
                visible: kcm.animationProfile === "hyprland"
                Layout.fillWidth: true
                Layout.preferredWidth: Kirigami.Units.gridUnit * 18
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                wrapMode: Text.WordWrap
                text: i18n("Hyprland animates window geometry and fades separately. Apps resize once per destination; the camera moves directly. Window contents may scale during the transition.")
            }
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Animations:")
                enabled: kcm.animationProfile !== "hyprland"
                text: i18n("Smooth camera and layout animations")
                checked: kcm.glide
                onToggled: kcm.glide = checked
            }
            RowLayout {
                Kirigami.FormData.label: i18n("Window landing:")
                enabled: !kcm.glide && kcm.animationProfile !== "hyprland"
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
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Long jumps:")
                text: i18n("Zoom out and back in when focus jumps far")
                enabled: kcm.glide && kcm.animationProfile !== "hyprland"
                checked: kcm.flyTo
                onToggled: kcm.flyTo = checked
            }

            Kirigami.Separator { Kirigami.FormData.isSection: true; Kirigami.FormData.label: i18n("Appearance and help") }

            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Colours:")
                text: i18n("Follow the Plasma colour scheme and accent")
                checked: kcm.followTheme
                onToggled: kcm.followTheme = checked
            }
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Notices:")
                text: i18n("Show a brief label for toggles, resize mode, undo and redo")
                checked: kcm.toasts
                onToggled: kcm.toasts = checked
            }
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Shortcut hints:")
                text: i18n("Hold Meta+Alt to see the keyboard shortcuts")
                checked: kcm.whichKey
                onToggled: kcm.whichKey = checked
            }
            RowLayout {
                Kirigami.FormData.label: i18n("Hint delay:")
                enabled: kcm.whichKey
                QQC2.SpinBox {
                    from: 100; to: 2000; stepSize: 100
                    value: kcm.whichKeyDelayMs
                    onValueModified: kcm.whichKeyDelayMs = value
                    textFromValue: (v) => i18n("%1 ms", v)
                    valueFromText: (t) => parseInt(t) || 400
                }
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
