.pragma library

function handleKey(event, viewport, navigation) {
                    if (!viewport.activeFocus) return
                    const panModifier = (event.modifiers & Qt.ShiftModifier) !== 0
                    const rollModifier = (event.modifiers & Qt.AltModifier) !== 0
                    let horizontal = 0
                    let vertical = 0
                    let strafe = 0
                    let forward = 0
                    switch (event.key) {
                    case Qt.Key_Left: horizontal = -1; break
                    case Qt.Key_Right: horizontal = 1; break
                    case Qt.Key_Up: vertical = -1; break
                    case Qt.Key_Down: vertical = 1; break
                    case Qt.Key_A: strafe = -1; break
                    case Qt.Key_D: strafe = 1; break
                    case Qt.Key_W: forward = 1; break
                    case Qt.Key_S: forward = -1; break
                    case Qt.Key_Q: viewport.moveCamera(0, -navigation.navigationMoveStep, 0); event.accepted = true; return
                    case Qt.Key_E: viewport.moveCamera(0, navigation.navigationMoveStep, 0); event.accepted = true; return
                    case Qt.Key_F: navigation.armOrbitTarget(); event.accepted = true; return
                    case Qt.Key_R: viewport.resetCamera(); event.accepted = true; return
                    default: return
                    }
                    if (rollModifier && horizontal !== 0) viewport.roll(horizontal * 5)
                    else if (horizontal !== 0 || vertical !== 0) {
                        if (rollModifier) return
                        if (panModifier || navigation.navigationPanMode)
                            viewport.pan(horizontal * 0.06, vertical * 0.06)
                        else if (navigation.navigationRollMode && horizontal !== 0) viewport.roll(horizontal * 5)
                        else if (navigation.navigationOrbitMode) viewport.orbit(horizontal * 10, vertical * 10)
                        else viewport.look(-horizontal * 5, -vertical * 5)
                    } else if (strafe !== 0 || forward !== 0) {
                        viewport.moveCamera(strafe * navigation.navigationMoveStep, 0, forward * navigation.navigationMoveStep)
                    }
                    event.accepted = true
                
}
