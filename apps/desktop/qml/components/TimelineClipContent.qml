pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    signal effectControlsRequested()
    signal noteRequested()
    property int noteCount: 0

    property string title: "Clip"
    property var clipData: ({})
    property string metadata: ""
    property string mediaType: "video"
    property var statusBadges: []
    property real horizontalInset: 0
    property real rightInset: horizontalInset
    property bool selected: false
    property bool labelOnly: false
    property bool hasPreview: false
    property string thumbnailSource: ""
    property string editSummary: ""
    readonly property real contentWidth: Math.max(0, width - horizontalInset - rightInset)
    readonly property bool compact: contentWidth < theme.scaled(110) || height < theme.scaled(46)
    readonly property bool pointerInside: clipContentHover.hovered
    readonly property string displayTitle: root.titleForDisplay()
    readonly property string displayMediaKind: root.mediaKindForDisplay()
    readonly property string displaySummary: root.summaryForDisplay()
    readonly property string completeDescription: root.descriptionForDisplay()
    readonly property int effectCount: root.resolvedEffectCount()
    readonly property bool sharedGradeApplied: (root.clipData || ({})).colorGradeRecipeEnabled === true
    readonly property bool hasAppliedEffects: effectCount > 0 || sharedGradeApplied
    readonly property bool effectsBypassed: root.effectCount > 0
                                              && !root.sharedGradeApplied
                                              && ((root.clipData || ({})).effectsEnabled === false
                                                  || ((root.clipData || ({})).effectDetails || []).length > 0
                                                     && ((root.clipData || ({})).effectDetails || []).every(function(effect) { return effect.enabled === false }))
    readonly property color effectIndicatorColor: !root.hasAppliedEffects ? theme.textFaint
                                                   : root.effectsBypassed ? theme.warning : theme.success
    readonly property string effectControlsAccessibleLabel: root.sharedGradeApplied
            ? "Effects and color layers applied. Open effect controls."
            : root.effectCount === 0
            ? "No effects applied. Open effect controls."
            : root.effectCount + (root.effectCount === 1 ? " effect" : " effects")
              + (root.effectsBypassed ? " bypassed. Open effect controls."
                                      : " enabled. Open effect controls.")
    readonly property var displayStatusBadges: root.statusBadgesForDisplay()
    readonly property string timelineClipContentContract: "timeline-clip-content:v3|title:fixed-readable-elide:true|metadata:fixed-readable-elide:true|status-strip:compact-consistent|tooltip-full:true|accessibility-full:true"
    readonly property string readableTextContract: "readable-text:v3|ellipsis:before-font-shrink|title-fixed-size:true|metadata-fixed-size:true|status-fit:true|tooltip-full:true|accessibility-full:true"

    Theme { id: theme }

    function cleanText(value) {
        return String(value === undefined || value === null ? "" : value)
                .replace(/[\r\n\t]+/g, " ").replace(/\s{2,}/g, " ").trim()
    }

    function humanize(value) {
        return cleanText(value).replace(/([a-z])([A-Z])/g, "$1 $2")
                .replace(/[-_]+/g, " ").replace(/\s{2,}/g, " ")
                .replace(/\b\w/g, function(letter) { return letter.toUpperCase() })
                .trim()
    }

    function genericLabel(value, mediaType) {
        const label = cleanText(value).toLowerCase()
        return label.length === 0 || label === "clip" || label === "animated"
                || label === "title" || label === "photo" || label === "video"
                || label === "audio" || label === "media" || label === "image"
                || label === "asset" || label === "item"
                || label === String(mediaType || "").toLowerCase()
                || label === "motion generator" || label === "motion-generator"
    }

    function sourceName() {
        const data = root.clipData || ({})
        let source = cleanText(data.filePath || data.originalSourceUrl || data.sourceUrl)
        if (source.length === 0)
            return ""
        source = source.split(/[?#]/)[0]
        let name = source.split(/[\\/]/).pop()
        try { name = decodeURIComponent(name) } catch (error) { }
        return name.replace(/\.[^.]+$/, "")
    }

    function firstTypeSummaryPart() {
        const data = root.clipData || ({})
        const summary = cleanText(data.timelineTypeSummary)
        return summary.length > 0 ? summary.split(/\s*[·|]\s*/)[0] : ""
    }

    function resolvedEffectCount() {
        const data = root.clipData || ({})
        const declaredCount = Number(data.effectCount)
        if (isFinite(declaredCount) && declaredCount > 0)
            return Math.floor(declaredCount)
        const names = data.effectNames || []
        const stack = data.effectStack || []
        return Math.max(Number(names.length || 0), Number(stack.length || 0))
    }

    function statusBadgesForDisplay() {
        const source = root.statusBadges || []
        const visibleBadges = []
        for (let index = 0; index < source.length; ++index) {
            const badge = source[index] || ({})
            const key = String(badge.key || "").toLowerCase()
            if (key === "effect" || key === "effects" || key === "fx")
                continue
            visibleBadges.push(badge)
        }
        return visibleBadges
    }

    function mediaKindForDisplay() {
        const data = root.clipData || ({})
        const type = String(data.mediaType || root.mediaType || "").toLowerCase()
        if (type === "photo" || type === "image") return "Photo"
        if (type === "video") return "Video"
        if (type === "audio") return "Audio"
        if (type === "caption") return "Caption"
        if (type === "adjustment") return "Adjustment"
        if (type === "motion-generator") return "Graphic"
        if (type === "title") {
            const actualText = cleanText(data.titleText || data.captionText)
            const shape = cleanText(data.titleShapeType)
            const typeSummary = cleanText(data.timelineTypeSummary)
            const supplied = cleanText(data.timelineLabel || root.title)
            return actualText.length === 0 && (shape.length > 0 || /shape/i.test(typeSummary)
                    || /^(rectangle|rounded rectangle|ellipse)$/i.test(supplied)) ? "Shape" : "Text"
        }
        return "Media"
    }

    function titleForDisplay() {
        const data = root.clipData || ({})
        const type = String(data.mediaType || root.mediaType || "").toLowerCase()
        const actualText = cleanText(data.titleText || data.captionText)
        if ((type === "title" || type === "caption") && actualText.length > 0)
            return actualText

        const supplied = cleanText(data.timelineLabel || root.title)
        if (type === "title" || type === "caption") {
            const suppliedShape = /^(rectangle|rounded rectangle|ellipse)$/i.test(supplied)
            if (!genericLabel(supplied, type) && !suppliedShape)
                return supplied
            let shape = cleanText(data.titleShapeType)
            const typeSummary = cleanText(data.timelineTypeSummary)
            if (shape.length === 0 && typeSummary.length > 0) {
                const parts = typeSummary.split(/\s*[·|]\s*/)
                if (/shape/i.test(parts[0] || "") && parts.length > 1)
                    shape = parts[parts.length - 1]
                else if (/shape/i.test(parts[0] || ""))
                    shape = parts[0].replace(/\s*shape\s*/i, "")
            }
            if (shape.length === 0 && /^(rectangle|rounded rectangle|ellipse)$/i.test(supplied))
                shape = supplied
            return shape.length > 0 ? humanize(shape) + " shape" : "Text graphic"
        }

        const cameraName = /^(IMG|DSC|PXL|MOV|VID|DJI|GOPR|MVI)[-_]?\d+/i.test(supplied)
        if (genericLabel(supplied, type) || cameraName) {
            const source = sourceName()
            if (source.length > 0)
                return humanize(source)
            const typePart = firstTypeSummaryPart()
            if (typePart.length > 0)
                return typePart
            const generator = humanize(data.generatorKind)
            if (generator.length > 0)
                return generator
            return type === "motion-generator" ? "Motion graphic" : "Clip"
        }
        if (/^[a-z][a-z0-9]*(?:-[a-z0-9]+){2,}$/.test(supplied))
            return humanize(supplied)
        return supplied.length > 0 ? supplied : "Clip"
    }

    function summaryForDisplay() {
        const data = root.clipData || ({})
        const parts = []
        function append(value) {
            const item = cleanText(value)
            if (item.length > 0 && parts.indexOf(item) < 0)
                parts.push(item)
        }

        append(root.displayMediaKind)

        if (data.mediaOnline === false)
            append("Offline")

        const maskSummary = cleanText(data.timelineMaskSummary)
        if (maskSummary.length > 0) {
            append(maskSummary)
        } else {
            const effectMasks = Number(data.enabledEffectMaskCount || 0)
            if (effectMasks > 0)
                append(effectMasks + (effectMasks === 1 ? " effect mask" : " effect masks"))
        }

        const effectNameValues = data.effectNames || []
        const effectNames = []
        for (let effectIndex = 0; effectIndex < Number(effectNameValues.length || 0); ++effectIndex) {
            const name = cleanText(effectNameValues[effectIndex])
            if (name.length > 0)
                effectNames.push(name)
        }
        // Keep operation names in the full description and accessibility text;
        // the compact FX button carries enabled/bypassed state on the clip.

        const keyframeCount = Number(data.keyframeCount || 0)
        const animationSummary = cleanText(data.timelineAnimationSummary).split(" · ")
                .map(function(value) { return humanize(value.replace(/^frame(?=[A-Z])/, "")) })
                .join(" · ")
        if (animationSummary.length > 0)
            append(animationSummary)
        else if (keyframeCount > 0)
            append(keyframeCount + " keyframes")

        const originalSummary = cleanText(root.editSummary)
        const originalParts = originalSummary.length > 0 ? originalSummary.split(/\s*[·|]\s*/) : []
        for (let index = 0; index < originalParts.length; ++index) {
            const item = cleanText(originalParts[index])
            const normalized = item.toLowerCase()
            if (normalized === "offline" || normalized === "masked" || normalized === "animated"
                    || normalized === "retimed" || normalized === "title" || normalized === "photo"
                    || normalized === "video" || normalized === "audio" || normalized === "motion-generator"
                    || /^fx\s+\d+$/i.test(item) || effectNames.indexOf(item) >= 0)
                continue
            append(item)
        }

        const rate = data.playbackRate === undefined ? 1 : Number(data.playbackRate)
        if (data.retimeActive === true || Math.abs(rate - 1) > 0.01) {
            const direction = String(data.retimeDirection || "")
            const prefix = direction === "reverse" ? "Reverse"
                    : direction === "freeze" ? "Freeze"
                    : data.retimeActive === true ? "Speed ramp" : "Speed"
            const value = data.effectivePlaybackRate === undefined
                    ? rate : Number(data.effectivePlaybackRate)
            append(prefix + " " + Math.abs(value).toFixed(1) + "×")
        }

        const typeSummary = cleanText(data.timelineTypeSummary)
        if (typeSummary.length > 0 && String(data.mediaType || "") === "motion-generator") {
            const firstPart = typeSummary.split(/\s*[·|]\s*/)[0]
            const title = cleanText(root.displayTitle).toLowerCase()
            const detail = firstPart.toLowerCase() === title
                    ? typeSummary.substring(firstPart.length).replace(/^\s*[·|]\s*/, "")
                    : typeSummary
            append(detail)
        }

        if (root.statusBadges) {
            for (let badgeIndex = 0; badgeIndex < root.statusBadges.length; ++badgeIndex) {
                const badge = root.statusBadges[badgeIndex] || ({})
                const badgeKey = String(badge.key || "")
                if ((badgeKey === "offline" && data.mediaOnline !== false)
                        || badgeKey === "disabled")
                    append(badge.label)
            }
        }
        return parts.join(" · ")
    }

    function descriptionForDisplay() {
        const details = []
        if (root.displaySummary.length > 0)
            details.push(root.displaySummary)
        const data = root.clipData || ({})
        const effectNames = data.effectNames || []
        for (let index = 0; index < effectNames.length; ++index) {
            const name = cleanText(effectNames[index])
            if (name.length > 0) details.push(name)
        }
        const keyframeCount = Number(data.keyframeCount || 0)
        if (keyframeCount > 0 && cleanText(data.timelineAnimationSummary).length > 0)
            details.push(keyframeCount + " keyframes")
        if (root.metadata.length > 0)
            details.push(root.metadata)
        if (root.displayStatusBadges) {
            for (let index = 0; index < root.displayStatusBadges.length; ++index) {
                const badge = root.displayStatusBadges[index] || ({})
                const description = cleanText(badge.tooltip || badge.label)
                if (description.length > 0 && details.indexOf(description) < 0)
                    details.push(description)
            }
        }
        if (root.selected)
            details.push("Selected clip")
        return details.join(". ")
    }

    clip: true
    Accessible.name: displayTitle
    Accessible.description: completeDescription.length > 0 ? completeDescription : "Clip"
    Accessible.role: Accessible.StaticText

    HoverHandler {
        id: clipContentHover
    }

    AppToolTip {
        id: clipPreviewTip
        targetItem: root
        visible: root.compact && root.pointerInside
        delay: 500
        text: root.displayTitle + (root.completeDescription.length > 0 ? "\n" + root.completeDescription : "")
        readonly property bool imagePreview: root.thumbnailSource.length > 0
                && (root.mediaType === "photo" || root.mediaType === "video")
        implicitWidth: imagePreview ? theme.scaled(272) : preferredTextWidth + leftPadding + rightPadding
        implicitHeight: clipTipContents.implicitHeight + topPadding + bottomPadding
        contentItem: Column {
            id: clipTipContents
            width: clipPreviewTip.availableWidth
            spacing: theme.space6
            Image {
                objectName: "timelineClipHoverPreview"
                visible: clipPreviewTip.imagePreview
                width: parent.width
                height: visible ? width * 0.5625 : 0
                source: clipPreviewTip.visible && visible ? root.thumbnailSource : ""
                sourceSize.width: 544
                sourceSize.height: 306
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
            }
            Text {
                width: parent.width
                text: clipPreviewTip.text
                wrapMode: Text.Wrap
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
            }
        }
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.labelOnly ? Math.min(parent.height, theme.scaled(20)) : parent.height
        visible: root.contentWidth >= theme.scaled(28)
        anchors.leftMargin: (root.compact ? theme.space2 : theme.space8) + root.horizontalInset
        anchors.rightMargin: (root.compact ? theme.space2 : theme.space8) + root.rightInset
        anchors.topMargin: root.compact ? theme.space2 : theme.space6
        anchors.bottomMargin: root.compact ? theme.space2 : theme.space6
        spacing: theme.space2

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space4

            Image {
                id: compactThumbnail
                objectName: "timelineCompactClipThumbnail"
                visible: root.height < theme.scaled(36) && root.contentWidth >= theme.scaled(132)
                         && (root.mediaType === "photo" || root.mediaType === "video")
                         && root.thumbnailSource.length > 0
                source: visible ? root.thumbnailSource : ""
                Layout.preferredWidth: theme.scaled(28)
                Layout.preferredHeight: theme.scaled(18)
                sourceSize.width: 64
                sourceSize.height: 40
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                Accessible.ignored: true
            }

            MediaTypeIcon {
                objectName: "timelineClipMediaIcon"
                mediaType: root.mediaType
                // Keep the title usable when a clip is zoomed down to a sliver.
                visible: !compactThumbnail.visible && root.width - root.horizontalInset - root.rightInset >= theme.scaled(140)
                color: root.selected ? theme.textStrong : theme.textMain
                Layout.preferredWidth: theme.scaled(13)
                Layout.preferredHeight: theme.scaled(13)
            }

            Label {
                objectName: "timelineClipMediaKindLabel"
                visible: root.contentWidth >= theme.scaled(240)
                         || (!compactThumbnail.visible && root.contentWidth >= theme.scaled(140))
                text: root.displayMediaKind.toUpperCase()
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
                font.weight: Font.DemiBold
                Accessible.name: root.displayMediaKind + " clip"
            }

            Label {
                id: titleLabel
                objectName: "timelineClipTitleLabel"
                Layout.fillWidth: true
                text: root.displayTitle
                color: root.selected ? theme.textStrong : theme.textMain
                font.family: theme.fontUi
                font.pixelSize: theme.typeLabel
                font.weight: root.selected ? Font.DemiBold : Font.Medium
                fontSizeMode: Text.FixedSize
                elide: Text.ElideRight
                AppToolTip {
                    id: sharedTooltip1
                    targetItem: titleLabel
                    visible: titleLabel.visible && (titleHover.hovered && titleLabel.contentWidth > titleLabel.width + 1)
                    delay: 350
                    timeout: 3600
                    text: root.displayTitle
                }
                HoverHandler { id: titleHover }
            }
            ToolButton {
                objectName: "timelineClipNoteButton"
                visible: root.noteCount > 0 && root.contentWidth >= theme.scaled(92)
                Layout.preferredWidth: theme.scaled(22)
                Layout.preferredHeight: theme.scaled(22)
                padding: 0
                Accessible.name: root.noteCount + " notes attached to this clip. Open note."
                contentItem: AppIcon { name: "edit"; color: "#47b5ff" }
                onClicked: root.noteRequested()
            }
            ToolButton {
                id: effectControlsButton
                objectName: "timelineClipEffectsButton"
                visible: root.contentWidth >= theme.scaled(52)
                Layout.preferredWidth: theme.scaled(24)
                Layout.minimumWidth: theme.scaled(24)
                Layout.maximumWidth: theme.scaled(24)
                Layout.preferredHeight: theme.scaled(22)
                Layout.alignment: Qt.AlignVCenter
                padding: 0
                hoverEnabled: true
                activeFocusOnTab: true
                Accessible.name: root.effectControlsAccessibleLabel
                Accessible.description: "Show this clip's applied effects and layers in Controls."
                Accessible.role: Accessible.Button
                contentItem: AppIcon {
                    objectName: "timelineClipEffectsIcon"
                    name: "effects"
                    anchors.centerIn: parent
                    width: theme.scaled(14)
                    height: theme.scaled(14)
                    color: root.effectIndicatorColor
                    opacity: root.hasAppliedEffects ? 1 : 0.72
                }
                onClicked: root.effectControlsRequested()

                AppToolTip {
                    targetItem: effectControlsButton
                    visible: effectControlsButton.hovered
                    delay: 350
                    text: root.effectControlsAccessibleLabel
                }
            }
        }

        Label {
            id: metadataLabel
            visible: !root.labelOnly && !root.compact && root.height >= theme.scaled(58)
            objectName: "timelineClipMetadataLabel"
            Layout.fillWidth: true
            text: root.metadata
            color: root.selected ? theme.textMain : theme.textMuted
            font.family: theme.fontNumeric
            font.features: theme.numericFontFeatures
            font.pixelSize: theme.typeCaption
            fontSizeMode: Text.FixedSize
            elide: Text.ElideRight
            AppToolTip {
                id: sharedTooltip2
                targetItem: metadataLabel
                visible: metadataLabel.visible && (metadataHover.hovered && metadataLabel.contentWidth > metadataLabel.width + 1)
                delay: 350
                timeout: 3600
                text: root.metadata
            }
            HoverHandler { id: metadataHover }
        }

        Flow {
            id: statusFlow
            objectName: "timelineClipStatusStrip"
            Layout.fillWidth: true
            spacing: theme.space4
            visible: !root.labelOnly && !root.compact && root.height >= theme.scaled(78) && root.contentWidth >= theme.scaled(160)
                     && root.displayStatusBadges.length > 0
            clip: true

            Repeater {
                model: statusFlow.visible ? root.displayStatusBadges : []

                delegate: Item {
                    id: statusBadge
                    required property var modelData

                    readonly property string badgeKey: String(modelData.key || "status")
                    readonly property string badgeLabel: String(modelData.label || "")
                    readonly property string badgeTone: String(modelData.tone || "neutral")
                    readonly property bool linkedBadge: badgeKey === "linked" || badgeKey === "group"
                    readonly property bool linkedTextBadge: badgeKey === "linked"

                    objectName: "timelineClipStatusBadge-" + badgeKey
                    width: statusBadgeRow.implicitWidth
                    height: statusBadgeRow.implicitHeight
                    Accessible.name: String(modelData.tooltip || badgeLabel)
                    Accessible.role: Accessible.StaticText
                    AppToolTip {
                        id: sharedTooltip3
                        targetItem: statusBadge
                        visible: statusBadge.visible && (statusHover.hovered)
                        delay: 350
                        timeout: 3600
                        text: String(statusBadge.modelData.tooltip || statusBadge.badgeLabel)
                    }
                    HoverHandler { id: statusHover }

                    Row {
                        id: statusBadgeRow
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: theme.space2

                        AppIcon {
                            id: statusBadgeIcon
                            visible: String(statusBadge.modelData.iconName || "").length > 0
                            width: theme.scaled(12)
                            height: theme.scaled(12)
                            name: String(statusBadge.modelData.iconName || "info")
                            color: statusBadge.linkedBadge
                                   ? theme.accentCool300
                                   : statusBadge.badgeTone === "danger"
                                   ? theme.error
                                   : statusBadge.badgeTone === "warning"
                                     ? theme.warning
                                     : statusBadge.badgeTone === "ready"
                                       ? theme.success
                                       : theme.textMuted
                        }

                        Label {
                            visible: !statusBadge.linkedTextBadge
                            text: statusBadge.badgeLabel
                            color: statusBadge.linkedBadge
                                   ? theme.accentCool300
                                   : statusBadge.badgeTone === "danger"
                                   ? theme.error
                                   : statusBadge.badgeTone === "warning"
                                     ? theme.warning
                                     : statusBadge.badgeTone === "ready"
                                       ? theme.success
                                       : theme.textMain
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            font.weight: Font.DemiBold
                            fontSizeMode: Text.FixedSize
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideNone
                        }
                    }
                }
            }
        }
    }

}
