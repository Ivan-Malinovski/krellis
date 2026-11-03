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
    readonly property var spawnTargets: [i18n("Beside the tile under the pointer"), i18n("Beside the focused window"), i18n("At the end of the row"), i18n("Halving the focused tile (dwindle)")]
    readonly property var dwindleSides: [i18n("The half the pointer is in"), i18n("To the right, or below")]
    readonly property var windowAnimations: ["popin", "slide", "gnomed"]

    // One explanation style everywhere: small, dimmed, wrapped, and part of its own row.
    component Explanation: QQC2.Label {
        Layout.fillWidth: true
        // The form is as wide as the page, but a wrapped label still reports its whole text as its implicit
        // width; left unbounded that is what a narrow System Settings window clips. Clamp it to the page
        // instead, keeping a readable minimum when the page is very narrow.
        Layout.maximumWidth: Math.min(Kirigami.Units.gridUnit * 18, Math.max(Kirigami.Units.gridUnit * 12, root.width - Kirigami.Units.gridUnit * 10))
        wrapMode: Text.WordWrap
        opacity: 0.7
        font: Kirigami.Theme.smallFont
    }

    // A form row: its label, its control and its explanation travel together, so an explanation can never end
    // up under the wrong control (a separate label row used to be easy to place one row too low).
    component Setting: ColumnLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing
    }

    component Pixels: QQC2.SpinBox {
        Layout.preferredWidth: Kirigami.Units.gridUnit * 8
        editable: true
        from: 0
        to: 4000
        stepSize: 20
        textFromValue: (v) => i18n("%1 px", v)
        valueFromText: (t) => parseInt(t) || 0
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.FormLayout {
            Layout.fillWidth: true

            Kirigami.Separator { Kirigami.FormData.isSection: true; Kirigami.FormData.label: i18n("Windows") }

            Setting {
                Kirigami.FormData.label: i18n("New tiled windows open:")
                QQC2.ComboBox {
                    model: root.spawnTargets
                    currentIndex: kcm.spawnTarget
                    onActivated: index => kcm.spawnTarget = index
                }
                Explanation {
                    text: i18n("Beside the tile under the pointer keeps a new window where you are working. Dwindle halves the focused tile, the way Hyprland does it.")
                }
            }
            Setting {
                Kirigami.FormData.label: i18n("A halved tile:")
                visible: kcm.spawnTarget === 3
                QQC2.ComboBox {
                    model: root.dwindleSides
                    currentIndex: kcm.dwindleSide
                    onActivated: index => kcm.dwindleSide = index
                }
                Explanation {
                    text: i18n("Closing one of a halved pair gives the other one the whole pair, so a split does not leave a gap.")
                }
            }
            Setting {
                Kirigami.FormData.label: i18n("New windows:")
                QQC2.CheckBox {
                    text: i18n("Float instead of tiling")
                    checked: kcm.floatFirst
                    onToggled: kcm.floatFirst = checked
                }
                Explanation {
                    text: i18n("Floating windows move freely, and dropping one at a screen edge or corner tiles it there.")
                }
            }
            Setting {
                Kirigami.FormData.label: i18n("Dragging a window:")
                QQC2.CheckBox {
                    text: i18n("Shake it by its title bar to make it float")
                    checked: kcm.shakeFloat
                    onToggled: kcm.shakeFloat = checked
                }
                Explanation {
                    text: i18n("While the canvas tiles a dragged window, a few quick shakes pull it out into a floating one and put it back where you dropped it.")
                }
            }
            Setting {
                Kirigami.FormData.label: i18n("Closing a window:")
                QQC2.CheckBox {
                    text: i18n("Focus its neighbour in the layout")
                    checked: kcm.closeFocus
                    onToggled: kcm.closeFocus = checked
                }
            }
            Setting {
                Kirigami.FormData.label: i18n("Smallest tile:")
                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    Pixels {
                        from: 1
                        value: kcm.tileMinWidth
                        onValueModified: kcm.tileMinWidth = value
                    }
                    QQC2.Label { text: "×" }
                    Pixels {
                        from: 1
                        value: kcm.tileMinHeight
                        onValueModified: kcm.tileMinHeight = value
                    }
                }
                Explanation {
                    text: i18n("Applications without a rule of their own are not made smaller than this, however little room their share of the canvas leaves.")
                }
            }
            Setting {
                Kirigami.FormData.label: i18n("Very small tiles:")
                Pixels {
                    to: 1000
                    stepSize: 10
                    value: kcm.iconBelow
                    onValueModified: kcm.iconBelow = value
                    textFromValue: (v) => v === 0 ? i18n("Always live") : i18n("Icon below %1 px", v)
                    valueFromText: (t) => parseInt(t) || 0
                }
                Explanation {
                    text: i18n("Drawn that small, a window is unreadable, so the tile shows the application's icon instead of the live window. 0 keeps every window live, as it costs less to draw a live tile than it looks.")
                }
            }
            Setting {
                Kirigami.FormData.label: i18n("A window on its own:")
                QQC2.SpinBox {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                    editable: true
                    from: 0
                    to: 100
                    stepSize: 5
                    value: kcm.loneWidth
                    onValueModified: kcm.loneWidth = value
                    textFromValue: (v) => (v === 0 || v === 100) ? i18n("Full width") : i18n("%1 % of the width", v)
                    valueFromText: (t) => parseInt(t) || 0
                }
                Explanation {
                    text: i18n("The only window on the canvas is centred at this share of the screen instead of filling it, which keeps a browser readable on an ultrawide. A second window brings the usual split back. A rule can set a different share for one application.")
                }
            }

            Kirigami.Separator { Kirigami.FormData.isSection: true; Kirigami.FormData.label: i18n("Zoomed view") }

            Setting {
                Kirigami.FormData.label: i18n("Neighbouring windows:")
                Pixels {
                    to: 200
                    stepSize: 5
                    value: kcm.peek
                    onValueModified: kcm.peek = value
                    textFromValue: (v) => v === 0 ? i18n("Hidden") : i18n("%1 pixels", v)
                    valueFromText: (t) => parseInt(t) || 0
                }
                Explanation {
                    text: i18n("How much of the windows next to the framed ones stays visible, dimmed. Clicking one widens the frame to take it in.")
                }
            }

            Kirigami.Separator { Kirigami.FormData.isSection: true; Kirigami.FormData.label: i18n("Extras") }

            Setting {
                Kirigami.FormData.label: i18n("Minimap:")
                QQC2.CheckBox {
                    id: mapBox
                    text: i18n("Show the minimap")
                    checked: kcm.map
                    onToggled: kcm.map = checked
                }
                QQC2.CheckBox {
                    text: i18n("Keep it pinned open")
                    enabled: mapBox.checked
                    checked: kcm.mapPinned
                    onToggled: kcm.mapPinned = checked
                }
            }
            Setting {
                Kirigami.FormData.label: i18n("Scratchpad:")
                QQC2.CheckBox {
                    text: i18n("Enable (Meta+G shows and hides the scratch windows)")
                    checked: kcm.scratchpad
                    onToggled: kcm.scratchpad = checked
                }
                Explanation {
                    text: i18n("Marked windows float over the canvas and come and go together. Meta+Shift+G marks the active window; Meta+G is also KWin's Grid View, so free it in Shortcuts first. Off, the shortcuts are released and the minimap has no scratchpad row.")
                }
            }
            Setting {
                Kirigami.FormData.label: i18n("Attention:")
                QQC2.CheckBox {
                    text: i18n("Mark windows that ask for attention (Meta+U jumps to one)")
                    checked: kcm.urgent
                    onToggled: kcm.urgent = checked
                }
                Explanation {
                    text: i18n("When a window you are not using asks for attention (a chat message, a finished build, a program that may not take focus), it gets an amber outline, an amber tile on the minimap and a first place in the quick-jump list. If it is stashed, its drawer's tab pulses instead.")
                }
            }
            Setting {
                Kirigami.FormData.label: i18n("Pin:")
                QQC2.CheckBox {
                    text: i18n("Enable (Meta+Shift+P pins the active tile)")
                    checked: kcm.pin
                    onToggled: kcm.pin = checked
                }
                Explanation {
                    text: i18n("A pinned tile keeps its place in the layout, and while it is scrolled out of view it shows as a live thumbnail in the bottom-left corner; click it to go back. Off, the shortcut is released and pins are dropped.")
                }
            }
            Setting {
                Kirigami.FormData.label: i18n("Tabs:")
                QQC2.CheckBox {
                    text: i18n("Enable tabbed tiles")
                    checked: kcm.tabs
                    onToggled: kcm.tabs = checked
                }
                Explanation {
                    text: i18n("Several windows can share one tile and show as tabs along its top. Drop a window on a tile's title bar to add it as a tab; drag a tab, or the bare strip, out to split it off again. Switched off, tabbed tiles are split back into separate tiles.")
                }
            }

            Kirigami.Separator { Kirigami.FormData.isSection: true; Kirigami.FormData.label: i18n("Motion") }

            Setting {
                Kirigami.FormData.label: i18n("Animation profile:")
                QQC2.ComboBox {
                    model: [i18n("Trellis"), i18n("Hyprland")]
                    currentIndex: kcm.animationProfile === "hyprland" ? 1 : 0
                    onActivated: kcm.animationProfile = currentIndex === 1 ? "hyprland" : "trellis"
                }
                Explanation {
                    text: i18n("Hyprland animates window geometry and fades separately: apps resize once per destination, the camera moves directly, and window contents may scale during the transition.")
                }
            }
            Setting {
                Kirigami.FormData.label: i18n("Window opening and closing:")
                visible: kcm.animationProfile === "hyprland"
                QQC2.ComboBox {
                    model: [i18n("Pop in (87%)"), i18n("Slide from the nearest edge"), i18n("Vertical collapse")]
                    currentIndex: Math.max(0, root.windowAnimations.indexOf(kcm.windowAnimation))
                    onActivated: kcm.windowAnimation = root.windowAnimations[currentIndex]
                }
            }
            Setting {
                Kirigami.FormData.label: i18n("Animations:")
                QQC2.CheckBox {
                    text: i18n("Smooth camera and layout changes")
                    enabled: kcm.animationProfile !== "hyprland"
                    checked: kcm.glide
                    onToggled: kcm.glide = checked
                }
            }
            Setting {
                Kirigami.FormData.label: i18n("Window landing:")
                QQC2.SpinBox {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                    editable: true
                    from: 0
                    to: 1000
                    stepSize: 25
                    enabled: !kcm.glide && kcm.animationProfile !== "hyprland"
                    value: kcm.landMs
                    onValueModified: kcm.landMs = value
                    textFromValue: (v) => v === 0 ? i18n("Instant") : i18n("%1 ms", v)
                    valueFromText: (t) => parseInt(t) || 0
                }
                Explanation {
                    text: i18n("With animations off, windows jump to their final place; this is how long the short move-and-scale after a resize takes.")
                }
            }
            Setting {
                Kirigami.FormData.label: i18n("Long jumps:")
                QQC2.CheckBox {
                    text: i18n("Zoom out and back in when focus jumps far")
                    enabled: kcm.glide && kcm.animationProfile !== "hyprland"
                    checked: kcm.flyTo
                    onToggled: kcm.flyTo = checked
                }
            }

            Kirigami.Separator { Kirigami.FormData.isSection: true; Kirigami.FormData.label: i18n("Appearance and help") }

            Setting {
                Kirigami.FormData.label: i18n("Colours:")
                QQC2.CheckBox {
                    text: i18n("Follow the Plasma colour scheme and accent")
                    checked: kcm.followTheme
                    onToggled: kcm.followTheme = checked
                }
            }
            Setting {
                Kirigami.FormData.label: i18n("Notices:")
                QQC2.CheckBox {
                    text: i18n("Show a brief label for toggles, resize mode, undo and redo")
                    checked: kcm.toasts
                    onToggled: kcm.toasts = checked
                }
            }
            Setting {
                Kirigami.FormData.label: i18n("Shortcut hints:")
                QQC2.CheckBox {
                    id: whichKeyBox
                    text: i18n("Hold Meta+Alt to see the keyboard shortcuts")
                    checked: kcm.whichKey
                    onToggled: kcm.whichKey = checked
                }
            }
            Setting {
                Kirigami.FormData.label: i18n("Hint delay:")
                QQC2.SpinBox {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                    editable: true
                    from: 100
                    to: 2000
                    stepSize: 100
                    enabled: whichKeyBox.checked
                    value: kcm.whichKeyDelayMs
                    onValueModified: kcm.whichKeyDelayMs = value
                    textFromValue: (v) => i18n("%1 ms", v)
                    valueFromText: (t) => parseInt(t) || 400
                }
            }
        }

        Kirigami.Separator { Layout.fillWidth: true }

        Kirigami.Heading {
            level: 3
            text: i18n("Application rules")
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            QQC2.TextField {
                id: newApp
                Layout.fillWidth: true
                placeholderText: i18n("Desktop file or window class, for example org.kde.dolphin")
                onAccepted: addButton.clicked()
            }
            QQC2.Button {
                text: i18n("Pick a window…")
                icon.name: "pointer"
                QQC2.ToolTip.text: i18n("Click a window on screen to fill in which application it is")
                QQC2.ToolTip.visible: hovered
                onClicked: kcm.pickWindow()
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

        Explanation {
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            text: i18n("A rule decides where one application's windows open, how they scale and how small they may get. The application is named by its desktop file or its window class; Pick a window… fills it in for a window you click.")
        }

        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            visible: rulesRepeater.count === 0
            text: i18n("No application rules")
            explanation: i18n("Add one above to make a single application behave differently from the rest, or to keep it out of the canvas altogether.")
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
                required property int loneWidth
                required property int floatWidth
                required property int floatHeight
                required property bool floatAtPointer

                header: RowLayout {
                    Kirigami.Heading {
                        level: 4
                        text: app
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    QQC2.ToolButton {
                        icon.name: "edit-delete"
                        Accessible.name: i18n("Remove the rule for %1", app)
                        QQC2.ToolTip.text: Accessible.name
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
                        currentIndex: Math.max(0, root.placements.findIndex(p => p.value === placement))
                        onActivated: kcm.rules.set(index, "placement", currentValue)
                    }
                    Setting {
                        Kirigami.FormData.label: i18n("May be:")
                        QQC2.CheckBox {
                            text: i18n("Side by side, in the layout")
                            checked: allowSide
                            onToggled: kcm.rules.set(index, "allowSide", checked)
                        }
                        QQC2.CheckBox {
                            text: i18n("On stage, filling the screen")
                            checked: allowStage
                            onToggled: kcm.rules.set(index, "allowStage", checked)
                        }
                        QQC2.CheckBox {
                            text: i18n("Floating over the canvas")
                            checked: allowFloating
                            onToggled: kcm.rules.set(index, "allowFloating", checked)
                        }
                        Explanation {
                            visible: !allowSide && !allowStage && !allowFloating
                            opacity: 1
                            color: Kirigami.Theme.negativeTextColor
                            text: i18n("With none of these on, Krellis leaves the application's windows alone: they are not put on the canvas at all.")
                        }
                    }
                    RowLayout {
                        Kirigami.FormData.label: i18n("Smallest size:")
                        Pixels {
                            from: 1
                            to: 16384
                            stepSize: 10
                            value: minWidth
                            onValueModified: kcm.rules.set(index, "minWidth", value)
                        }
                        QQC2.Label { text: "×" }
                        Pixels {
                            from: 1
                            to: 16384
                            stepSize: 10
                            value: minHeight
                            onValueModified: kcm.rules.set(index, "minHeight", value)
                        }
                    }
                    RowLayout {
                        Kirigami.FormData.label: i18n("Floating size:")
                        Pixels {
                            to: 16384
                            value: floatWidth
                            onValueModified: kcm.rules.set(index, "floatWidth", value)
                        }
                        QQC2.Label { text: "×" }
                        Pixels {
                            to: 16384
                            value: floatHeight
                            onValueModified: kcm.rules.set(index, "floatHeight", value)
                        }
                    }
                    Explanation {
                        text: i18n("0 keeps the window's own size on that axis. A floating window without a size opens at 60% × 65% of the screen.")
                    }
                    QQC2.CheckBox {
                        Kirigami.FormData.label: i18n("Floating windows:")
                        text: i18n("Open at the pointer")
                        checked: floatAtPointer
                        onToggled: kcm.rules.set(index, "floatAtPointer", checked)
                    }
                    QQC2.ComboBox {
                        Kirigami.FormData.label: i18n("Scaling:")
                        model: root.scalings
                        textRole: "text"
                        valueRole: "value"
                        currentIndex: Math.max(0, root.scalings.findIndex(p => p.value === scaling))
                        onActivated: kcm.rules.set(index, "scaling", currentValue)
                    }
                    QQC2.SpinBox {
                        Kirigami.FormData.label: i18n("Alone on the canvas:")
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                        editable: true
                        from: -1
                        to: 100
                        stepSize: 5
                        value: loneWidth
                        onValueModified: kcm.rules.set(index, "loneWidth", value)
                        textFromValue: (v) => v < 0 ? i18n("As the general setting") : (v === 0 || v === 100) ? i18n("Full width") : i18n("%1 % of the width", v)
                        valueFromText: (t) => isNaN(parseInt(t)) ? -1 : parseInt(t)
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

    Connections {
        target: kcm
        function onWindowPicked(app) {
            if (app.length > 0) newApp.text = app
        }
    }
}
