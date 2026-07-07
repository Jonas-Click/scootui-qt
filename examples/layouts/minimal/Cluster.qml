// Example layout pack: a deliberately minimal cluster.
//
// Copy this directory to /data/scootui/layouts/minimal/ on the scooter
// (or $SCOOTUI_DATA_DIR/scootui/layouts/minimal/ on the desktop simulator)
// and select it under Settings → Layout. Edits to this file hot-reload
// while the dashboard is running.
//
// Everything the built-in screens can see is available here too: the
// stores are global context properties (engineStore, vehicleStore,
// battery0Store, tripStore, themeStore, ...). See docs/THEMING.md for the
// designer API. Built-in widgets can be reused via qrc directory imports.

import QtQuick
import ScootUI 1.0
import "qrc:/ScootUI/qml/widgets/components"

Rectangle {
    id: root
    color: themeStore.backgroundColor

    // Main.qml reads this to position the blinker overlay above the bottom bar
    readonly property real bottomBarHeight: batteryBar.height + odometerText.height + 16

    readonly property real speed: typeof engineStore !== "undefined" ? engineStore.speed : 0
    readonly property real charge: typeof battery0Store !== "undefined" ? battery0Store.charge : 0
    readonly property int blinker: typeof vehicleStore !== "undefined" ? vehicleStore.blinkerState : 0
    readonly property bool parked: typeof vehicleStore !== "undefined"
                                   && vehicleStore.state === Scooter.VehicleState.Parked

    // Giant speed readout
    Text {
        id: speedText
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -20
        text: Math.floor(root.speed)
        font.pixelSize: 190
        font.weight: Font.Bold
        color: root.parked ? themeStore.textSecondary : themeStore.textColor
    }

    Text {
        anchors.top: speedText.bottom
        anchors.topMargin: -24
        anchors.horizontalCenter: parent.horizontalCenter
        text: "km/h"
        font.pixelSize: themeStore.fontTitle
        color: themeStore.textSecondary
    }

    // Blinker arrows (reusing the built-in SvgIcon + shared blink clock)
    SvgIcon {
        anchors.left: parent.left
        anchors.leftMargin: 72
        anchors.verticalCenter: speedText.verticalCenter
        width: 48; height: 48
        source: "qrc:/ScootUI/assets/icons/librescoot-turn-left.svg"
        color: themeStore.statusSuccess
        visible: root.blinker === 1 || root.blinker === 3
        opacity: typeof vehicleStore !== "undefined" ? vehicleStore.blinkOpacity : 0
    }
    SvgIcon {
        anchors.right: parent.right
        anchors.rightMargin: 72
        anchors.verticalCenter: speedText.verticalCenter
        width: 48; height: 48
        source: "qrc:/ScootUI/assets/icons/librescoot-turn-right.svg"
        color: themeStore.statusSuccess
        visible: root.blinker === 2 || root.blinker === 3
        opacity: typeof vehicleStore !== "undefined" ? vehicleStore.blinkOpacity : 0
    }

    // Odometer
    Text {
        id: odometerText
        anchors.bottom: batteryBar.top
        anchors.bottomMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        text: typeof engineStore !== "undefined"
              ? (engineStore.odometer / 1000).toFixed(0) + " km" : ""
        font.pixelSize: themeStore.fontCaption
        color: themeStore.textHint
    }

    // Battery bar across the bottom
    Rectangle {
        id: batteryBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 8
        color: themeStore.powerBarBg

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * root.charge / 100
            color: root.charge > 20 ? themeStore.accent : themeStore.statusError

            Behavior on width { NumberAnimation { duration: 300 } }
        }
    }
}
