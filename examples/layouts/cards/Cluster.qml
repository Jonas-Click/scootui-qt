// Example layout pack: speed on the left, info cards on the right.
//
// Copy this directory to /data/scootui/layouts/cards/ on the scooter
// (or $SCOOTUI_DATA_DIR/scootui/layouts/cards/ on the desktop simulator)
// and select it under Settings → Layout. Edits hot-reload while the
// dashboard is running.
//
// This example demonstrates pack-local icons: the SVGs in icons/ ship
// with the pack and are resolved relative to this file. Rendering them
// through the built-in SvgIcon widget tints them with theme tokens at
// runtime, so one white SVG works in every color theme, dark and light.

import QtQuick
import ScootUI 1.0
import "qrc:/ScootUI/qml/widgets/components"

Rectangle {
    id: root
    color: themeStore.backgroundColor

    // Main.qml reads this to position the blinker overlay above the bottom bar
    readonly property real bottomBarHeight: bottomBar.height

    readonly property real speed: typeof engineStore !== "undefined" ? engineStore.speed : 0
    readonly property real charge0: typeof battery0Store !== "undefined" && battery0Store.present
                                    ? battery0Store.charge : -1
    readonly property real charge1: typeof battery1Store !== "undefined" && battery1Store.present
                                    ? battery1Store.charge : -1
    readonly property real soh0: typeof battery0Store !== "undefined" ? battery0Store.stateOfHealth : 100
    readonly property real soh1: typeof battery1Store !== "undefined" ? battery1Store.stateOfHealth : 100

    // Same estimate the built-in battery widget uses: 45 km per battery,
    // scaled by state of health and charge
    readonly property int rangeKm: Math.floor(
        (charge0 >= 0 ? 45.0 * (soh0 / 100) * (charge0 / 100) : 0)
        + (charge1 >= 0 ? 45.0 * (soh1 / 100) * (charge1 / 100) : 0))

    // Speed, filling the left half
    Item {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: bottomBar.top
        width: parent.width * 0.55

        Text {
            id: speedText
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -14
            text: Math.floor(root.speed)
            font.pixelSize: 150
            font.weight: Font.Bold
            color: themeStore.textColor
        }
        Text {
            anchors.top: speedText.bottom
            anchors.topMargin: -18
            anchors.horizontalCenter: parent.horizontalCenter
            text: "km/h"
            font.pixelSize: themeStore.fontTitle
            color: themeStore.textSecondary
        }
    }

    // Info cards down the right side
    Column {
        anchors.right: parent.right
        anchors.rightMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -root.bottomBarHeight / 2
        width: parent.width * 0.34
        spacing: 14

        // Battery card: pack-local icon, tinted by charge level
        Rectangle {
            width: parent.width
            height: 88
            radius: themeStore.radiusCard
            color: themeStore.surfaceColor
            border.color: themeStore.borderColor

            SvgIcon {
                id: boltIcon
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                width: 30; height: 30
                source: Qt.resolvedUrl("icons/bolt.svg")
                color: root.charge0 > 20 ? themeStore.accent : themeStore.statusError
            }
            Column {
                anchors.left: boltIcon.right
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    text: root.charge0 >= 0 ? Math.round(root.charge0) + "%" : "--"
                    font.pixelSize: themeStore.fontHeading
                    font.weight: Font.Bold
                    color: themeStore.textColor
                }
                Text {
                    text: root.charge1 >= 0 ? "2nd: " + Math.round(root.charge1) + "%" : "battery"
                    font.pixelSize: themeStore.fontCaption
                    color: themeStore.textHint
                }
            }
        }

        // Range card
        Rectangle {
            width: parent.width
            height: 88
            radius: themeStore.radiusCard
            color: themeStore.surfaceColor
            border.color: themeStore.borderColor

            SvgIcon {
                id: pinIcon
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                width: 30; height: 30
                source: Qt.resolvedUrl("icons/pin.svg")
                color: themeStore.accent
            }
            Column {
                anchors.left: pinIcon.right
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    text: root.rangeKm + " km"
                    font.pixelSize: themeStore.fontHeading
                    font.weight: Font.Bold
                    color: themeStore.textColor
                }
                Text {
                    text: "range"
                    font.pixelSize: themeStore.fontCaption
                    color: themeStore.textHint
                }
            }
        }
    }

    // Bottom bar: trip and odometer
    Rectangle {
        id: bottomBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56
        color: themeStore.surfaceColor

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            text: typeof tripStore !== "undefined"
                  ? "TRIP " + (tripStore.distance / 1000).toFixed(1) + " km" : ""
            font.pixelSize: themeStore.fontBody
            color: themeStore.textSecondary
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            text: typeof engineStore !== "undefined"
                  ? (engineStore.odometer / 1000).toFixed(0) + " km" : ""
            font.pixelSize: themeStore.fontBody
            color: themeStore.textSecondary
        }
    }
}
