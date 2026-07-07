import QtQuick
import "../indicators"

Item {
    id: speedometer

    Component.onCompleted: if (typeof bootTimer !== "undefined")
        console.log("[boot +" + bootTimer.elapsed() + "ms] SpeedometerDisplay completed")

    // Properties from stores
    readonly property real targetSpeed: typeof engineStore !== "undefined" ? engineStore.speed : 0
    readonly property real motorCurrent: typeof engineStore !== "undefined" ? engineStore.motorCurrent : 0
    readonly property bool ecuStale: typeof engineStore !== "undefined" && engineStore.faultCode === 20

    // Internal animated speed
    property real animatedSpeed: 0
    property real maxArcSpeed: 60

    // Animation state
    property bool isRegenerating: motorCurrent < 0
    property bool isAccelerating: motorCurrent > 0
    property real regenTransition: 0
    property real overspeedPulse: 0
    property real accelPulse: 0
    property real errorPulse: 0

    // Arc constants
    readonly property real arcStartAngle: 150
    readonly property real arcSweepAngle: 240
    readonly property real arcStrokeWidth: 20
    readonly property real canvasWidth: 300
    readonly property real canvasHeight: 240
    readonly property real centerX: canvasWidth / 2
    readonly property real centerY: 150
    readonly property real arcRadius: canvasWidth / 2

    // Speed labels to show (every 10 km/h for regulatory compliance)
    readonly property var speedLabels: [0, 10, 20, 30, 40, 50, 60]
    readonly property var majorSpeedLabels: [0, 30, 50, 60]

    // Fixed size matching Flutter (no scaling)
    readonly property real displayScale: 1.0

    // Imperative flag — avoids circular binding (animatedSpeed ↔ running)
    property bool _animationActive: false

    // Start animation when speed target changes
    onTargetSpeedChanged: _animationActive = true

    // Start animation when acceleration/pulse state changes
    onIsAcceleratingChanged: if (isAccelerating) _animationActive = true

    // Repaint on theme change (variant flip or color-theme reload)
    Connections {
        target: themeStore
        function onThemeChanged() { canvas.requestPaint() }
    }
    // ECU comm-lost (E20): kick the frame loop so the red glow pulses, and
    // repaint immediately on the rising/falling edge.
    onEcuStaleChanged: { _animationActive = true; canvas.requestPaint() }

    // Exponential smoothing via FrameAnimation — only runs when animating
    FrameAnimation {
        id: frameAnim
        running: speedometer._animationActive
        onTriggered: {
            var dtMs = frameTime * 1000
            if (dtMs <= 0 || dtMs > 500) dtMs = 16

            // Exponential smoothing: alpha = 1 - exp(-dt/100)
            var alpha = 1.0 - Math.exp(-dtMs / 100.0)
            var diff = targetSpeed - animatedSpeed
            if (Math.abs(diff) < 0.3) {
                animatedSpeed = targetSpeed
            } else {
                animatedSpeed += diff * alpha
            }

            // Overspeed pulse (800ms cycle)
            if (animatedSpeed > maxArcSpeed) {
                overspeedPulse = (Math.sin(Date.now() / 800 * Math.PI * 2) + 1) / 2
            } else {
                overspeedPulse = 0
            }

            // Acceleration pulse (1000ms cycle)
            if (isAccelerating) {
                accelPulse = (Math.sin(Date.now() / 1000 * Math.PI * 2) + 1) / 2
            } else {
                accelPulse = 0
            }

            // ECU comm-lost error pulse (1000ms cycle)
            if (ecuStale) {
                errorPulse = (Math.sin(Date.now() / 1000 * Math.PI * 2) + 1) / 2
            } else {
                errorPulse = 0
            }

            canvas.requestPaint()

            // Auto-stop when fully converged and no pulse effects active
            var converged = (animatedSpeed === targetSpeed)
            var noPulse = animatedSpeed <= maxArcSpeed && !isAccelerating
            if (converged && noPulse && !ecuStale) {
                speedometer._animationActive = false
            }
        }
    }

    // Regen transition animation
    Behavior on regenTransition {
        NumberAnimation { duration: 1000; easing.type: Easing.InOutQuad }
    }
    onIsRegeneratingChanged: regenTransition = isRegenerating ? 1.0 : 0.0
    onRegenTransitionChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -20
        width: canvasWidth * displayScale
        height: canvasHeight * displayScale

        property real s: displayScale

        // Defensive repaints: the FrameAnimation auto-stops once speed
        // converges, so a freshly (re)created Canvas may only ever get one
        // paint. Force a repaint on creation and whenever it becomes visible
        // again so the static markings/labels can't get stuck blank.
        Component.onCompleted: requestPaint()
        onVisibleChanged: if (visible) requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.scale(s, s)

            var cx = centerX
            var cy = centerY
            var r = arcRadius

            var startRad = arcStartAngle * Math.PI / 180
            var sweepRad = arcSweepAngle * Math.PI / 180

            // === Background arc ===
            ctx.beginPath()
            ctx.arc(cx, cy, r - arcStrokeWidth / 2, startRad, startRad + sweepRad)
            ctx.lineWidth = arcStrokeWidth
            ctx.lineCap = "round"

            // Regen color: arc background → regen transition
            if (regenTransition > 0) {
                ctx.strokeStyle = lerpColor(themeStore.arcBackground, themeStore.speedRegen, regenTransition)
            } else {
                ctx.strokeStyle = themeStore.arcBackground
            }
            ctx.stroke()

            // === ECU comm-lost (E20): the whole arc glows red, no live fill ===
            if (ecuStale) {
                ctx.beginPath()
                ctx.arc(cx, cy, r - arcStrokeWidth / 2, startRad, startRad + sweepRad)
                ctx.lineWidth = arcStrokeWidth
                ctx.lineCap = "round"
                ctx.shadowColor = themeStore.speedError
                ctx.shadowBlur = 8 + 20 * errorPulse
                ctx.strokeStyle = themeStore.speedError
                ctx.stroke()
                ctx.shadowBlur = 0
            }

            // === Speed fill arc ===
            if (animatedSpeed > 0 && !ecuStale) {
                var clampedSpeed = Math.min(animatedSpeed, maxArcSpeed)
                var progress = clampedSpeed / maxArcSpeed
                var fillSweep = sweepRad * progress

                ctx.beginPath()
                ctx.arc(cx, cy, r - arcStrokeWidth / 2, startRad, startRad + fillSweep)
                ctx.lineWidth = arcStrokeWidth
                ctx.lineCap = "round"

                // Color based on speed
                var fillColor
                if (animatedSpeed > maxArcSpeed) {
                    // Overspeed: pulse between the two overspeed colors
                    fillColor = lerpColor(themeStore.overspeedA, themeStore.overspeedB, overspeedPulse)
                } else if (animatedSpeed > 55) {
                    // Transition zone 55-60: accent → high-speed fill
                    var t = (animatedSpeed - 55) / 5
                    fillColor = lerpColor(themeStore.accent, themeStore.speedFillHigh, t)
                } else {
                    fillColor = themeStore.accent
                }

                // Acceleration pulse modifies opacity
                if (isAccelerating && animatedSpeed <= maxArcSpeed) {
                    var opacity = 0.7 + 0.3 * accelPulse
                    ctx.globalAlpha = opacity
                }

                ctx.strokeStyle = fillColor
                ctx.stroke()
                ctx.globalAlpha = 1.0
            }

            // === Tick marks ===
            var tickInward = 26
            for (var speed = 0; speed <= maxArcSpeed; speed += 5) {
                var tickAngle = startRad + sweepRad * (speed / maxArcSpeed)
                var isMajor = (speed % 10 === 0)
                var tickLen = isMajor ? 8 : 4
                var tickWidth = isMajor ? 1.5 : 1.0

                var outerR = r - tickInward
                var innerR = outerR - tickLen

                var cosA = Math.cos(tickAngle)
                var sinA = Math.sin(tickAngle)

                ctx.beginPath()
                ctx.moveTo(cx + outerR * cosA, cy + outerR * sinA)
                ctx.lineTo(cx + innerR * cosA, cy + innerR * sinA)
                ctx.lineWidth = tickWidth
                ctx.lineCap = "butt"
                ctx.strokeStyle = themeStore.speedTick
                ctx.stroke()
            }

            // === Speed labels ===
            var labelInward = 44
            ctx.textAlign = "center"
            ctx.textBaseline = "middle"

            for (var i = 0; i < speedLabels.length; i++) {
                var spd = speedLabels[i]
                if (spd > maxArcSpeed) continue
                var isMajor = majorSpeedLabels.indexOf(spd) >= 0
                var labelAngle = startRad + sweepRad * (spd / maxArcSpeed)
                var labelR = r - labelInward
                var lx = cx + labelR * Math.cos(labelAngle)
                var ly = cy + labelR * Math.sin(labelAngle)
                ctx.font = isMajor ? "600 13px Roboto" : "400 9px Roboto"
                ctx.fillStyle = isMajor ? themeStore.speedLabelMajor : themeStore.speedTick
                ctx.fillText(spd.toString(), lx, ly)
            }
        }
    }

    // Central speed, km/h and road info matching Flutter's Stack + Transform + Column
    // Speed number — anchored to arc center (centerY=150 in 240px canvas = parent.center + 30)
    Text {
        id: speedText
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height / 2 - height / 2
        text: speedometer.ecuStale ? "—" : Math.floor(speedometer.animatedSpeed).toString()
        font.pixelSize: themeStore.fontDisplay
        font.weight: Font.Bold
        color: themeStore.textColor
    }

    // km/h — tight below speed number
    Text {
        id: unitText
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: speedText.bottom
        anchors.topMargin: -12
        text: "km/h"
        font.pixelSize: themeStore.fontTitle
        color: themeStore.textSecondary
    }

    // Road name + speed limit — below km/h. Sits lower in the gap between the
    // speed-arc endpoints, with a larger limit sign and room for a wider name.
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: unitText.bottom
        anchors.topMargin: 12
        spacing: 4

        SpeedLimitIndicator {
            iconSize: 36
            anchors.verticalCenter: parent.verticalCenter
        }

        RoadNameDisplay {
            anchors.verticalCenter: parent.verticalCenter
            fontSize: 12
            maxTextWidth: 240
        }
    }

    // Helper: linear interpolation between two colors
    function lerpColor(c1, c2, t) {
        return Qt.rgba(c1.r + (c2.r - c1.r) * t,
                       c1.g + (c2.g - c1.g) * t,
                       c1.b + (c2.b - c1.b) * t,
                       c1.a + (c2.a - c1.a) * t)
    }
}
