pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../../components"

ColumnLayout {
    id: content
    required property var root
    required property var theme
    required property var carouselDocument
    required property var carouselBrandKitEditorDialog
    property int currentSection: 0
    property bool previewVisible: true
    readonly property bool compactLayout: width < content.theme.scaled(760)
    onCompactLayoutChanged: if (!compactLayout && currentSection === 4) currentSection = 0
    anchors.fill: parent
    anchors.margins: content.theme.space18
    spacing: content.theme.space12

    function draftValue(key, fallback) {
        const value = content.carouselBrandKitEditorDialog.draft[key];
        return value === undefined || value === null ? fallback : value;
    }
    function focusInitialTarget() {
        currentSection = 0;
        brandNameField.forceActiveFocus(Qt.TabFocusReason);
    }
    function ensureControlVisible(item) {
        Qt.callLater(function() {
            const flickable = editorScroll.contentItem;
            if (!item || !item.activeFocus || !item.visible || !flickable || !flickable.contentItem)
                return;
            const top = item.mapToItem(flickable.contentItem, 0, 0).y;
            const margin = content.theme.space8;
            let target = flickable.contentY;
            if (top < target + margin)
                target = top - margin;
            else if (top + item.height > target + flickable.height - margin)
                target = top + item.height - flickable.height + margin;
            flickable.contentY = Math.max(0, Math.min(target, flickable.contentHeight - flickable.height));
        });
    }
    function showSection(index) {
        currentSection = index;
        editorScroll.contentItem.contentY = 0;
    }

    RowLayout {
        objectName: "carouselBrandKitHeader"
        Layout.fillWidth: true
        spacing: content.theme.space12
        Label {
            Layout.fillWidth: true
            text: String(content.carouselBrandKitEditorDialog.draft.brandId || "").length > 0 ? "Edit brand kit" : "New brand kit"
            color: content.theme.textStrong
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeDialogTitle
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }
        ActionButton {
            objectName: "carouselBrandKitPreviewToggle"
            visible: !content.compactLayout
            compact: true
            text: "Preview"
            active: content.previewVisible
            onClicked: content.previewVisible = !content.previewVisible
        }
        ActionButton {
            objectName: "carouselBrandKitCloseButton"
            compact: true
            text: "Close"
            iconName: "close"
            tooltipText: "Discard brand kit changes"
            onClicked: content.carouselBrandKitEditorDialog.close()
        }
    }

    Flow {
        objectName: "carouselBrandKitNavigation"
        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height
        spacing: content.theme.space6
        Repeater {
            model: ["Palette", "Typography", "Logos", "Geometry", "Preview & checks"]
            delegate: FilterChip {
                required property int index
                required property string modelData
                objectName: "carouselBrandKitPage_" + index
                visible: index < 4 || content.compactLayout
                text: modelData
                active: content.currentSection === index
                onClicked: content.showSection(index)
            }
        }
    }

    ScrollView {
        id: editorScroll
        objectName: "carouselBrandKitViewport"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        contentWidth: availableWidth
        rightPadding: content.theme.space8
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: ScrollBar {
            parent: editorScroll
            x: editorScroll.width - width
            y: editorScroll.topPadding
            width: content.theme.scaled(4)
            height: editorScroll.availableHeight
            policy: ScrollBar.AsNeeded
            active: true
        }
        RowLayout {
            width: editorScroll.availableWidth
            spacing: content.theme.space18
            ColumnLayout {
                id: settingsPages
                objectName: "carouselBrandKitSettings"
                visible: !content.compactLayout || content.currentSection < 4
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 0
                ColumnLayout {
                    objectName: "carouselBrandKitSettingsPage_0"
                    visible: content.currentSection === 0
                    Layout.fillWidth: true
                    spacing: content.theme.space12
                    Label {
                        text: "Identity"
                        color: content.theme.textStrong
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeSection
                        font.weight: Font.DemiBold
                    }
                    ToneTextField {
                        onActiveFocusChanged: if (activeFocus) content.ensureControlVisible(this)
                        id: brandNameField
                        Layout.fillWidth: true
                        objectName: "carouselBrandKitNameField"
                        accessibleLabel: "Brand kit name"
                        placeholderText: "Brand kit name"
                        text: String(content.carouselBrandKitEditorDialog.draft.name || "")
                        onTextEdited: content.carouselDocument.setBrandKitDraftField("name", text)
                    }

                    Label {
                        text: "Named palette"
                        color: content.theme.textStrong
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeSection
                        font.weight: Font.DemiBold
                    }
                    Repeater {
                        model: [
                            {
                                label: "Primary",
                                nameKey: "primaryName",
                                colorKey: "primaryColor",
                                fallback: "#fbf7f1"
                            },
                            {
                                label: "Secondary",
                                nameKey: "secondaryName",
                                colorKey: "secondaryColor",
                                fallback: "#ffffff"
                            },
                            {
                                label: "Accent",
                                nameKey: "accentName",
                                colorKey: "accentColor",
                                fallback: "#36f2a6"
                            },
                            {
                                label: "Text",
                                nameKey: "",
                                colorKey: "textColor",
                                name: "Primary Text",
                                fallback: "#1e1a17"
                            }
                        ]

                        delegate: RowLayout {
                            id: paletteField
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: content.theme.space8
                            Rectangle {
                                Layout.preferredWidth: content.theme.controlHeightDense
                                Layout.preferredHeight: content.theme.controlHeightDense
                                radius: content.theme.radiusControl
                                color: String(content.draftValue(paletteField.modelData.colorKey, paletteField.modelData.fallback))
                                border.width: 1
                                border.color: content.theme.alpha(content.theme.lineSoft, 0.55)
                            }
                            ToneTextField {
                                onActiveFocusChanged: if (activeFocus) content.ensureControlVisible(this)
                                Layout.fillWidth: true
                                visible: paletteField.modelData.nameKey.length > 0
                                objectName: "carouselBrandField_" + paletteField.modelData.nameKey
                                accessibleLabel: paletteField.modelData.label + " token name"
                                placeholderText: paletteField.modelData.label + " token name"
                                text: paletteField.modelData.nameKey.length > 0 ? String(content.draftValue(paletteField.modelData.nameKey, "")) : "Primary Text"
                                onTextEdited: {
                                    if (paletteField.modelData.nameKey.length > 0)
                                        content.carouselDocument.setBrandKitDraftField(paletteField.modelData.nameKey, text);
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: paletteField.modelData.nameKey.length === 0
                                text: "Text"
                                color: content.theme.textMuted
                                font.family: content.theme.fontUi
                                font.pixelSize: content.theme.typeBody
                            }
                            ToneTextField {
                                onActiveFocusChanged: if (activeFocus) content.ensureControlVisible(this)
                                Layout.preferredWidth: content.theme.scaled(112)
                                placeholderText: "#RRGGBB"
                                objectName: "carouselBrandField_" + paletteField.modelData.colorKey
                                accessibleLabel: paletteField.modelData.label + " color"
                                text: String(content.draftValue(paletteField.modelData.colorKey, paletteField.modelData.fallback))
                                onEditingFinished: content.carouselDocument.setBrandKitDraftField(paletteField.modelData.colorKey, text)
                            }
                        }
                    }

                    Label {
                        text: "Theme variants"
                        color: content.theme.textStrong
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeSection
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "Light follows the named palette. Define the dark and campaign surfaces, text, and accent tokens here."
                        color: content.theme.textMuted
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeCaption
                        wrapMode: Text.Wrap
                    }
                    Repeater {
                        objectName: "carouselBrandThemeVariantEditor"
                        model: [
                            { label: "Dark surface", key: "darkBackgroundColor", fallback: "#12161c" },
                            { label: "Dark text", key: "darkTextColor", fallback: "#f7f3ec" },
                            { label: "Dark accent", key: "darkAccentColor", fallback: "#36f2a6" },
                            { label: "Campaign surface", key: "campaignBackgroundColor", fallback: "#36f2a6" },
                            { label: "Campaign text", key: "campaignTextColor", fallback: "#1e1a17" },
                            { label: "Campaign accent", key: "campaignAccentColor", fallback: "#1e1a17" }
                        ]

                        delegate: RowLayout {
                            id: variantField
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: content.theme.space8
                            Rectangle {
                                Layout.preferredWidth: content.theme.controlHeightDense
                                Layout.preferredHeight: content.theme.controlHeightDense
                                radius: content.theme.radiusControl
                                color: String(content.draftValue(variantField.modelData.key, variantField.modelData.fallback))
                                border.width: 1
                                border.color: content.theme.alpha(content.theme.lineSoft, 0.55)
                            }
                            Label {
                                Layout.fillWidth: true
                                text: variantField.modelData.label
                                color: content.theme.textMuted
                                font.family: content.theme.fontUi
                                font.pixelSize: content.theme.typeCaption
                                wrapMode: Text.WordWrap
                            }
                            ToneTextField {
                                onActiveFocusChanged: if (activeFocus) content.ensureControlVisible(this)
                                Layout.preferredWidth: content.theme.scaled(112)
                                placeholderText: "#RRGGBB"
                                objectName: "carouselBrandField_" + variantField.modelData.key
                                accessibleLabel: variantField.modelData.label
                                text: String(content.draftValue(variantField.modelData.key, variantField.modelData.fallback))
                                onEditingFinished: content.carouselDocument.setBrandKitDraftField(variantField.modelData.key, text)
                            }
                        }
                    }
                }
                ColumnLayout {
                    objectName: "carouselBrandKitSettingsPage_1"
                    visible: content.currentSection === 1
                    Layout.fillWidth: true
                    spacing: content.theme.space12
                    Label {
                        text: "Typography roles"
                        color: content.theme.textStrong
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeSection
                        font.weight: Font.DemiBold
                    }
                    Repeater {
                        model: [
                            {
                                label: "Titles",
                                fontKey: "titleFont",
                                weightKey: "titleWeight",
                                familyFallback: "display",
                                weightFallback: "bold"
                            },
                            {
                                label: "Body",
                                fontKey: "bodyFont",
                                weightKey: "bodyWeight",
                                familyFallback: "system",
                                weightFallback: "regular"
                            }
                        ]

                        delegate: RowLayout {
                            id: typeField
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: content.theme.space8
                            Label {
                                Layout.preferredWidth: content.theme.scaled(64)
                                text: typeField.modelData.label
                                color: content.theme.textMuted
                                font.family: content.theme.fontUi
                                font.pixelSize: content.theme.typeCaption
                                wrapMode: Text.WordWrap
                            }
                            ToneTextField {
                                onActiveFocusChanged: if (activeFocus) content.ensureControlVisible(this)
                                Layout.fillWidth: true
                                placeholderText: "Font family"
                                objectName: "carouselBrandField_" + typeField.modelData.fontKey
                                accessibleLabel: typeField.modelData.label
                                text: String(content.draftValue(typeField.modelData.fontKey, typeField.modelData.familyFallback))
                                onTextEdited: content.carouselDocument.setBrandKitDraftField(typeField.modelData.fontKey, text)
                            }
                            ToneTextField {
                                onActiveFocusChanged: if (activeFocus) content.ensureControlVisible(this)
                                Layout.preferredWidth: content.theme.scaled(112)
                                objectName: "carouselBrandField_" + typeField.modelData.weightKey
                                accessibleLabel: typeField.modelData.label + " font weight"
                                placeholderText: "Weight"
                                text: String(content.draftValue(typeField.modelData.weightKey, typeField.modelData.weightFallback))
                                onTextEdited: content.carouselDocument.setBrandKitDraftField(typeField.modelData.weightKey, text)
                            }
                        }
                    }
                    ToneTextField {
                        onActiveFocusChanged: if (activeFocus) content.ensureControlVisible(this)
                        Layout.fillWidth: true
                        placeholderText: "Font license, source, renewal, or handoff reminder"
                        objectName: "carouselBrandField_fontLicenseNotes"
                        accessibleLabel: "Font license notes"
                        text: String(content.carouselBrandKitEditorDialog.draft.fontLicenseNotes || "")
                        onTextEdited: content.carouselDocument.setBrandKitDraftField("fontLicenseNotes", text)
                    }
                }
                ColumnLayout {
                    objectName: "carouselBrandKitSettingsPage_2"
                    visible: content.currentSection === 2
                    Layout.fillWidth: true
                    spacing: content.theme.space12
                    Label {
                        text: "Logo asset"
                        color: content.theme.textStrong
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeSection
                        font.weight: Font.DemiBold
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ToneTextField {
                            onActiveFocusChanged: if (activeFocus) content.ensureControlVisible(this)
                            Layout.fillWidth: true
                            objectName: "carouselBrandField_logoName"
                            accessibleLabel: "Logo name"
                            placeholderText: "Logo name"
                            text: String(content.carouselBrandKitEditorDialog.draft.logoName || "Primary Logo")
                            onTextEdited: content.carouselDocument.setBrandKitDraftField("logoName", text)
                        }
                        ToneCheckBox {
                            objectName: "carouselBrandKitLogoRequired"
                            text: "Required"
                            checked: Boolean(content.carouselBrandKitEditorDialog.draft.logoRequired)
                            accessibleDescription: "Required assets block saving when their file is missing."
                            onToggled: content.carouselDocument.setBrandKitDraftField("logoRequired", checked)
                        }
                    }
                    Repeater {
                        objectName: "carouselBrandLogoVariantEditor"
                        model: [
                            { label: "Primary", key: "logoPath", fallback: "" },
                            { label: "Light on dark", key: "logoLightPath", fallback: "" },
                            { label: "Dark on light", key: "logoDarkPath", fallback: "" },
                            { label: "Monochrome", key: "logoMonochromePath", fallback: "" }
                        ]

                        delegate: ColumnLayout {
                            id: logoField
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: content.theme.space4
                            Label {
                                Layout.fillWidth: true
                                text: logoField.modelData.label
                                color: content.theme.textMuted
                                font.family: content.theme.fontUi
                                font.pixelSize: content.theme.typeCaption
                                wrapMode: Text.WordWrap
                                font.weight: Font.DemiBold
                                elide: Text.ElideNone
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: content.theme.space6
                                ToneTextField {
                                    onActiveFocusChanged: if (activeFocus) content.ensureControlVisible(this)
                                    Layout.fillWidth: true
                                    placeholderText: "/path/to/logo.png"
                                    objectName: "carouselBrandField_" + logoField.modelData.key
                                    accessibleLabel: logoField.modelData.label
                                    text: String(content.draftValue(logoField.modelData.key, ""))
                                    onTextEdited: content.carouselDocument.setBrandKitDraftField(logoField.modelData.key, text)
                                }
                                ActionButton {
                                    compact: true
                                    text: "Choose"
                                    onClicked: {
                                        content.root.brandLogoDraftField = logoField.modelData.key;
                                        content.root.openMediaPicker("brand-logo");
                                    }
                                }
                                ActionButton {
                                    compact: true
                                    text: "Clear"
                                    enabled: String(content.draftValue(logoField.modelData.key, "")).length > 0
                                    onClicked: content.carouselDocument.setBrandKitDraftField(logoField.modelData.key, "")
                                }
                            }
                        }
                    }

                    Label {
                        text: "Logo usage rules"
                        color: content.theme.textStrong
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeSection
                        font.weight: Font.DemiBold
                    }
                    Repeater {
                        objectName: "carouselBrandLogoRuleEditor"
                        model: [
                            { label: "Clear space (%)", key: "logoSafeAreaPercent", fallback: 10 },
                            { label: "Minimum width (px)", key: "logoMinimumWidth", fallback: 96 },
                            { label: "Minimum height (px)", key: "logoMinimumHeight", fallback: 32 },
                            { label: "Minimum contrast", key: "logoMinimumContrast", fallback: 3 }
                        ]

                        delegate: RowLayout {
                            id: ruleField
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: content.theme.space8
                            Label {
                                Layout.fillWidth: true
                                text: ruleField.modelData.label
                                color: content.theme.textMuted
                                font.family: content.theme.fontUi
                                font.pixelSize: content.theme.typeCaption
                                wrapMode: Text.WordWrap
                                elide: Text.ElideNone
                            }
                            ToneTextField {
                                onActiveFocusChanged: if (activeFocus) content.ensureControlVisible(this)
                                Layout.preferredWidth: content.theme.scaled(112)
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                objectName: "carouselBrandField_" + ruleField.modelData.key
                                accessibleLabel: ruleField.modelData.label
                                text: String(content.draftValue(ruleField.modelData.key, ruleField.modelData.fallback))
                                onTextEdited: content.carouselDocument.setBrandKitDraftField(ruleField.modelData.key, Number(text))
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "Logo checks only warn. They never resize, move, recolor, or swap placed artwork."
                        color: content.theme.textFaint
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeCaption
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                    }
                }
                ColumnLayout {
                    objectName: "carouselBrandKitSettingsPage_3"
                    visible: content.currentSection === 3
                    Layout.fillWidth: true
                    spacing: content.theme.space12
                    Label {
                        text: "Design-token scales"
                        color: content.theme.textStrong
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeSection
                        font.weight: Font.DemiBold
                    }
                    Repeater {
                        model: [
                            { label: "Radius small", key: "cornerSmall", fallback: 0 },
                            { label: "Radius medium", key: "cornerMedium", fallback: 0 },
                            { label: "Radius large", key: "cornerLarge", fallback: 0 },
                            { label: "Stroke hairline", key: "strokeHairline", fallback: 0 },
                            { label: "Stroke standard", key: "strokeStandard", fallback: 0 },
                            { label: "Stroke strong", key: "strokeStrong", fallback: 0 },
                            { label: "Space XS", key: "spacingXs", fallback: 0 },
                            { label: "Space S", key: "spacingSm", fallback: 0 },
                            { label: "Space M", key: "spacingMd", fallback: 0 },
                            { label: "Space L", key: "spacingLg", fallback: 0 },
                            { label: "Space XL", key: "spacingXl", fallback: 0 }
                        ]

                        delegate: RowLayout {
                            id: scaleField
                            required property var modelData
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                text: scaleField.modelData.label
                                color: content.theme.textMuted
                                font.family: content.theme.fontUi
                                font.pixelSize: content.theme.typeCaption
                                wrapMode: Text.WordWrap
                            }
                            ToneTextField {
                                onActiveFocusChanged: if (activeFocus) content.ensureControlVisible(this)
                                Layout.preferredWidth: content.theme.scaled(112)
                                objectName: "carouselBrandField_" + scaleField.modelData.key
                                accessibleLabel: scaleField.modelData.label
                                text: String(content.draftValue(scaleField.modelData.key, scaleField.modelData.fallback))
                                validator: DoubleValidator {
                                    bottom: 0
                                }
                                onEditingFinished: content.carouselDocument.setBrandKitDraftField(scaleField.modelData.key, Number(text))
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                id: previewPanel
                objectName: "carouselBrandKitPreviewPanel"
                visible: content.compactLayout ? content.currentSection === 4 : content.previewVisible
                Layout.fillWidth: content.compactLayout
                Layout.preferredWidth: content.compactLayout ? editorScroll.availableWidth : content.theme.scaled(280)
                Layout.alignment: Qt.AlignTop
                spacing: content.theme.space12
                Label {
                    text: "Live preview"
                    color: content.theme.textStrong
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeSection
                    font.weight: Font.DemiBold
                }
                Rectangle {
                    Layout.fillWidth: true
                    objectName: "carouselBrandLivePreview"
                    implicitHeight: previewSampleBody.implicitHeight + content.theme.space20 * 2
                    radius: Number(content.draftValue("cornerMedium", 12))
                    color: String(content.carouselBrandKitEditorDialog.draft.primaryColor || "#fbf7f1")
                    border.width: Number(content.draftValue("strokeHairline", 1))
                    border.color: String(content.carouselBrandKitEditorDialog.draft.accentColor || "#36f2a6")
                    clip: true

                    ColumnLayout {
                        id: previewSampleBody
                        anchors.fill: parent
                        anchors.margins: content.theme.space20
                        spacing: content.theme.space10
                        Rectangle {
                            Layout.preferredWidth: content.theme.scaled(54)
                            Layout.preferredHeight: content.theme.scaled(8)
                            radius: content.theme.scaled(4)
                            color: String(content.carouselBrandKitEditorDialog.draft.accentColor || "#36f2a6")
                        }
                        Label {
                            Layout.fillWidth: true
                            text: String(content.carouselBrandKitEditorDialog.draft.name || "Brand title")
                            color: String(content.carouselBrandKitEditorDialog.draft.textColor || "#1e1a17")
                            font.family: content.root.textFontFamily(String(content.carouselBrandKitEditorDialog.draft.titleFont || "display"))
                            font.pixelSize: content.theme.scaled(25)
                            font.weight: content.root.textFontWeight(String(content.carouselBrandKitEditorDialog.draft.titleWeight || "bold"))
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                        }
                        Label {
                            Layout.fillWidth: true
                            text: "Body text uses the selected font and palette."
                            color: String(content.carouselBrandKitEditorDialog.draft.textColor || "#1e1a17")
                            font.family: content.root.textFontFamily(String(content.carouselBrandKitEditorDialog.draft.bodyFont || "system"))
                            font.pixelSize: content.theme.typeBody
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                        }
                        Item {
                            Layout.fillHeight: true
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: content.theme.scaled(42)
                            radius: Number(content.draftValue("cornerSmall", 4))
                            color: String(content.carouselBrandKitEditorDialog.draft.secondaryColor || "#ffffff")
                            border.width: Number(content.draftValue("strokeStandard", 1))
                            border.color: String(content.carouselBrandKitEditorDialog.draft.accentColor || "#36f2a6")
                            Label {
                                anchors.centerIn: parent
                                text: "Primary action"
                                color: String(content.carouselBrandKitEditorDialog.draft.textColor || "#1e1a17")
                                font.family: content.root.textFontFamily(String(content.carouselBrandKitEditorDialog.draft.bodyFont || "system"))
                                font.pixelSize: content.theme.typeCaption
                                font.weight: Font.Bold
                            }
                        }
                    }
                }
                InspectorSection {
                    objectName: "carouselBrandKitRequiredChecks"
                    Layout.fillWidth: true
                    title: "Required checks"
                    visible: (content.carouselBrandKitEditorDialog.validation.blockingIssues || []).length > 0
                    Repeater {
                        model: content.carouselBrandKitEditorDialog.validation.blockingIssues || []
                        delegate: Label {
                            required property var modelData
                            Layout.fillWidth: true
                            text: String(modelData)
                            color: content.theme.error
                            font.family: content.theme.fontUi
                            font.pixelSize: content.theme.typeCaption
                            wrapMode: Text.WordWrap
                        }
                    }
                }
                InspectorSection {
                    objectName: "carouselBrandKitReminders"
                    onActiveFocusChanged: if (activeFocus) content.ensureControlVisible(this)
                    Layout.fillWidth: true
                    title: "Reminders (" + (content.carouselBrandKitEditorDialog.validation.warnings || []).length + ")"
                    visible: (content.carouselBrandKitEditorDialog.validation.warnings || []).length > 0
                    expandable: true
                    expanded: false
                    Repeater {
                        model: content.carouselBrandKitEditorDialog.validation.warnings || []
                        delegate: Label {
                            required property var modelData
                            Layout.fillWidth: true
                            text: String(modelData)
                            color: content.theme.textMuted
                            font.family: content.theme.fontUi
                            font.pixelSize: content.theme.typeCaption
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }

    RowLayout {
        objectName: "carouselBrandKitFooter"
        Layout.fillWidth: true
        spacing: content.theme.space8
        Label {
            objectName: "carouselBrandKitBlockingSummary"
            Layout.fillWidth: true
            visible: !Boolean(content.carouselBrandKitEditorDialog.validation.canSave)
            text: String((content.carouselBrandKitEditorDialog.validation.blockingIssues || [])[0] || "")
            color: content.theme.error
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeCaption
            wrapMode: Text.WordWrap
        }
        Item {
            Layout.fillWidth: true
            visible: Boolean(content.carouselBrandKitEditorDialog.validation.canSave)
        }
        ActionButton {
            objectName: "carouselBrandKitCancelButton"
            compact: true
            text: "Cancel"
            onClicked: content.carouselBrandKitEditorDialog.close()
        }
        ActionButton {
            objectName: "carouselBrandKitSaveButton"
            compact: true
            tone: "primary"
            text: "Save Brand Kit"
            enabled: Boolean(content.carouselBrandKitEditorDialog.validation.canSave)
            disabledReason: (content.carouselBrandKitEditorDialog.validation.blockingIssues || []).join(" ")
            onClicked: {
                if (content.carouselDocument.saveBrandKitDraft())
                    content.carouselBrandKitEditorDialog.close();
            }
        }
    }
}
