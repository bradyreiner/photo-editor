pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: cameraControls
    required property var navigation
    required property var viewport
    Theme { id: theme }
    objectName: "sceneCameraMotionHud"
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: theme.space12
    width: Math.min(motionControls.implicitWidth + theme.space16, Math.max(1, parent.width - theme.space24))
    height: motionControls.implicitHeight + theme.space8
    radius: theme.radiusPanel
    color: theme.alpha(theme.ink950, 0.88)
    border.width: theme.hairlineWidth
    border.color: theme.lineSoft
    clip: true
    RowLayout {
        id: motionControls
        anchors.centerIn: parent
        spacing: theme.space4
        ActionButton {
            objectName: "sceneCameraLookModeButton"
            autoExclusive: true
            compact: true; checkable: true; checked: !cameraControls.navigation.navigationOrbitMode && !cameraControls.navigation.navigationPanMode && !cameraControls.navigation.navigationRollMode
            text: "Look"; iconName: "focus-frame"; showText: cameraControls.viewport.width >= theme.scaled(760); showLeadingIcon: showText; active: checked
            accessibleName: "Look mode"
            accent: theme.textStrong
            tooltipText: "Drag or use arrows to look from this position. WASD moves; Q/E rise or lower; scroll moves forward. Alt+drag rolls."
            onClicked: { cameraControls.navigation.navigationOrbitMode = false; cameraControls.navigation.navigationPanMode = false; cameraControls.navigation.navigationRollMode = false; cameraControls.viewport.forceActiveFocus(Qt.OtherFocusReason) }
        }
        ActionButton {
            objectName: "sceneCameraOrbitModeButton"
            autoExclusive: true
            compact: true; checkable: true; checked: cameraControls.navigation.navigationOrbitMode
            text: "Orbit"; iconName: "orbit"; showText: cameraControls.viewport.width >= theme.scaled(760); showLeadingIcon: showText; active: checked
            accessibleName: "Orbit mode"
            accent: theme.textStrong
            tooltipText: "Drag around the focus or picked target. Double-click a surface to orbit around it."
            onClicked: { cameraControls.navigation.navigationOrbitMode = true; cameraControls.navigation.navigationPanMode = false; cameraControls.navigation.navigationRollMode = false; cameraControls.viewport.forceActiveFocus(Qt.OtherFocusReason) }
        }
        ActionButton {
            objectName: "sceneCameraPanModeButton"
            autoExclusive: true
            compact: true; checkable: true; checked: cameraControls.navigation.navigationPanMode
            text: "Pan"; iconName: "pan"; showText: cameraControls.viewport.width >= theme.scaled(760); showLeadingIcon: showText; active: checked
            accessibleName: "Pan mode"
            accent: theme.textStrong
            tooltipText: "Drag, use arrows, or scroll to pan the view."
            onClicked: { cameraControls.navigation.navigationOrbitMode = false; cameraControls.navigation.navigationPanMode = true; cameraControls.navigation.navigationRollMode = false; cameraControls.viewport.forceActiveFocus(Qt.OtherFocusReason) }
        }
        ActionButton {
            objectName: "sceneCameraRollModeButton"
            autoExclusive: true
            compact: true; checkable: true; checked: cameraControls.navigation.navigationRollMode
            text: "Roll"; iconName: "roll"; showText: cameraControls.viewport.width >= theme.scaled(760); showLeadingIcon: showText; active: checked
            accessibleName: "Roll mode"
            accent: theme.textStrong
            tooltipText: "Left-drag horizontally to roll the view. Alt+drag also rolls; Alt-click still picks focus."
            onClicked: { cameraControls.navigation.navigationOrbitMode = false; cameraControls.navigation.navigationPanMode = false; cameraControls.navigation.navigationRollMode = true; cameraControls.viewport.forceActiveFocus(Qt.OtherFocusReason) }
        }
        ActionButton { objectName: "sceneCameraNudgeLeftButton"; visible: cameraControls.viewport.width >= theme.scaled(640) && cameraControls.navigation.navigationPanMode; compact: true; showText: false; iconName: "arrow-left"; Layout.preferredWidth: Math.max(32, theme.scaled(32)); Layout.preferredHeight: Math.max(32, theme.scaled(32)); autoRepeat: true; autoRepeatDelay: 300; autoRepeatInterval: 70; accessibleName: "Move view left"; tooltipText: accessibleName; onClicked: cameraControls.navigation.nudgeCamera(-1, 0) }
        ActionButton { objectName: "sceneCameraNudgeUpButton"; visible: cameraControls.viewport.width >= theme.scaled(640) && cameraControls.navigation.navigationPanMode; compact: true; showText: false; iconName: "arrow-up"; Layout.preferredWidth: Math.max(32, theme.scaled(32)); Layout.preferredHeight: Math.max(32, theme.scaled(32)); autoRepeat: true; autoRepeatDelay: 300; autoRepeatInterval: 70; accessibleName: "Move view up"; tooltipText: accessibleName; onClicked: cameraControls.navigation.nudgeCamera(0, -1) }
        ActionButton { objectName: "sceneCameraNudgeDownButton"; visible: cameraControls.viewport.width >= theme.scaled(640) && cameraControls.navigation.navigationPanMode; compact: true; showText: false; iconName: "arrow-down"; Layout.preferredWidth: Math.max(32, theme.scaled(32)); Layout.preferredHeight: Math.max(32, theme.scaled(32)); autoRepeat: true; autoRepeatDelay: 300; autoRepeatInterval: 70; accessibleName: "Move view down"; tooltipText: accessibleName; onClicked: cameraControls.navigation.nudgeCamera(0, 1) }
        ActionButton { objectName: "sceneCameraNudgeRightButton"; visible: cameraControls.viewport.width >= theme.scaled(640) && cameraControls.navigation.navigationPanMode; compact: true; showText: false; iconName: "arrow-right"; Layout.preferredWidth: Math.max(32, theme.scaled(32)); Layout.preferredHeight: Math.max(32, theme.scaled(32)); autoRepeat: true; autoRepeatDelay: 300; autoRepeatInterval: 70; accessibleName: "Move view right"; tooltipText: accessibleName; onClicked: cameraControls.navigation.nudgeCamera(1, 0) }
        ActionButton { objectName: "sceneCameraDollyOutButton"; visible: cameraControls.viewport.width >= theme.scaled(640) && cameraControls.navigation.navigationPanMode; compact: true; showText: false; iconName: "zoom-out"; Layout.preferredWidth: Math.max(32, theme.scaled(32)); Layout.preferredHeight: Math.max(32, theme.scaled(32)); autoRepeat: true; autoRepeatDelay: 300; autoRepeatInterval: 70; accessibleName: "Move camera backward"; tooltipText: "Move camera backward"; onClicked: cameraControls.navigation.dollyCamera(-1) }
        ActionButton { objectName: "sceneCameraDollyInButton"; visible: cameraControls.viewport.width >= theme.scaled(640) && cameraControls.navigation.navigationPanMode; compact: true; showText: false; iconName: "zoom-in"; Layout.preferredWidth: Math.max(32, theme.scaled(32)); Layout.preferredHeight: Math.max(32, theme.scaled(32)); autoRepeat: true; autoRepeatDelay: 300; autoRepeatInterval: 70; accessibleName: "Move camera forward"; tooltipText: "Move camera forward"; onClicked: cameraControls.navigation.dollyCamera(1) }
        ActionButton {
            objectName: "sceneCameraOrbitTargetButton"
            compact: true
            text: cameraControls.navigation.orbitPickArmed ? "Click target…" : "Target"
            showText: cameraControls.viewport.width >= theme.scaled(760)
            iconName: "target"
            showLeadingIcon: showText
            accent: theme.textStrong
            tooltipText: "Pick a surface to orbit around (F). Double-click also sets the target."
            active: cameraControls.navigation.orbitPickArmed
            onClicked: cameraControls.navigation.armOrbitTarget()
        }
        ActionButton {
            objectName: "sceneCameraResetViewButton"
            compact: true; text: "Reset"; showText: cameraControls.viewport.width >= theme.scaled(760); iconName: "reset"; showLeadingIcon: showText
            accessibleName: "Reset camera view"
            accent: theme.textStrong
            tooltipText: "Reset the camera view (R)."
            onClicked: { cameraControls.viewport.resetCamera(); cameraControls.viewport.forceActiveFocus(Qt.OtherFocusReason) }
        }
    }
}
