import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    color: "transparent"
    focus: true

    property real manualZoom: 1.0
    property bool fitToView: true
    property bool inspectorCollapsed: false
    property string editedDisplaySource: ""
    property string referenceDisplaySource: ""
    property var hslChannels: [
        {
            name: "Reds",
            color: "#d45a62"
        },
        {
            name: "Oranges",
            color: "#dd8a39"
        },
        {
            name: "Yellows",
            color: "#d7bd42"
        },
        {
            name: "Greens",
            color: "#61b56f"
        },
        {
            name: "Aquas",
            color: "#49b8c4"
        },
        {
            name: "Blues",
            color: "#4e86d9"
        },
        {
            name: "Purples",
            color: "#8b6be3"
        },
        {
            name: "Magentas",
            color: "#c85ac1"
        }
    ]

    function clampZoom(value) {
        return Math.max(0.25, Math.min(4.0, value));
    }

    function maxPanX() {
        return Math.max(0, zoomPreviewContent.width - zoomPreviewFlick.width);
    }

    function maxPanY() {
        return Math.max(0, zoomPreviewContent.height - zoomPreviewFlick.height);
    }

    function clampPanPosition() {
        zoomPreviewFlick.contentX = Math.max(0, Math.min(maxPanX(), zoomPreviewFlick.contentX));
        zoomPreviewFlick.contentY = Math.max(0, Math.min(maxPanY(), zoomPreviewFlick.contentY));
    }

    function centerZoomPreview() {
        zoomPreviewFlick.contentX = maxPanX() / 2;
        zoomPreviewFlick.contentY = maxPanY() / 2;
    }

    function imagePixelWidth(image) {
        return Math.max(1, image.implicitWidth > 0 ? image.implicitWidth : image.sourceSize.width);
    }

    function imagePixelHeight(image) {
        return Math.max(1, image.implicitHeight > 0 ? image.implicitHeight : image.sourceSize.height);
    }

    function fitZoomLevel() {
        if (editedDisplaySource.length === 0)
            return 1.0;

        const imageWidth = imagePixelWidth(editedPreviewZoom);
        const imageHeight = imagePixelHeight(editedPreviewZoom);
        const availableWidth = editorPreview.compareMode
                               ? Math.max(1, (fitPreviewRow.width - 8) / 2)
                               : Math.max(1, fitPreviewRow.width);
        const availableHeight = Math.max(1, fitPreviewRow.height);
        return Math.min(availableWidth / imageWidth, availableHeight / imageHeight);
    }

    function currentZoomLevel() {
        return fitToView ? fitZoomLevel() : manualZoom;
    }

    function contentWidthForZoom(zoomLevel) {
        const editedWidth = editedDisplaySource.length > 0 ? imagePixelWidth(editedPreviewZoom) * zoomLevel : 0;
        const referenceWidth = editorPreview.compareMode && referenceDisplaySource.length > 0
                               ? imagePixelWidth(referencePreviewZoom) * zoomLevel
                               : 0;
        const combinedWidth = editorPreview.compareMode && referenceWidth > 0
                              ? editedWidth + referenceWidth + 16
                              : editedWidth;
        return Math.max(zoomPreviewFlick.width, combinedWidth);
    }

    function contentHeightForZoom(zoomLevel) {
        const editedHeight = editedDisplaySource.length > 0 ? imagePixelHeight(editedPreviewZoom) * zoomLevel : 0;
        const referenceHeight = editorPreview.compareMode && referenceDisplaySource.length > 0
                                ? imagePixelHeight(referencePreviewZoom) * zoomLevel
                                : 0;
        return Math.max(zoomPreviewFlick.height, Math.max(editedHeight, referenceHeight));
    }

    function setZoomLevel(value, pivotX, pivotY) {
        const nextZoom = clampZoom(value);
        const currentZoom = currentZoomLevel();
        if (Math.abs(nextZoom - currentZoom) < 0.0001)
            return;

        const fitZoom = fitZoomLevel();
        if (nextZoom <= fitZoom * 1.01) {
            fitToView = true;
            zoomPreviewFlick.contentX = 0;
            zoomPreviewFlick.contentY = 0;
            return;
        }

        const viewportPivotX = pivotX === undefined ? zoomPreviewFlick.width / 2 : pivotX;
        const viewportPivotY = pivotY === undefined ? zoomPreviewFlick.height / 2 : pivotY;
        const oldContentWidth = fitToView ? zoomPreviewFlick.width : contentWidthForZoom(currentZoom);
        const oldContentHeight = fitToView ? zoomPreviewFlick.height : contentHeightForZoom(currentZoom);
        const oldFocusX = (fitToView ? 0 : zoomPreviewFlick.contentX) + viewportPivotX;
        const oldFocusY = (fitToView ? 0 : zoomPreviewFlick.contentY) + viewportPivotY;
        const widthRatio = oldFocusX / Math.max(1, oldContentWidth);
        const heightRatio = oldFocusY / Math.max(1, oldContentHeight);

        fitToView = false;
        manualZoom = nextZoom;

        const newContentWidth = contentWidthForZoom(nextZoom);
        const newContentHeight = contentHeightForZoom(nextZoom);
        zoomPreviewFlick.contentX = widthRatio * newContentWidth - viewportPivotX;
        zoomPreviewFlick.contentY = heightRatio * newContentHeight - viewportPivotY;
        clampPanPosition();
    }

    function hslColorForChannel(channelName) {
        for (let index = 0; index < hslChannels.length; ++index) {
            if (hslChannels[index].name === channelName)
                return hslChannels[index].color;
        }

        return "#c99a52";
    }

    function selectedHslColor() {
        return hslColorForChannel(editorPreview.hslChannel);
    }

    function selectAsset(index, assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, recipeJson) {
        catalogAssets.selectIndex(index);
    }

    function selectRelativeAsset(offset) {
        if (catalogAssets.assetCount === 0)
            return;

        const baseIndex = catalogAssets.selectedIndex >= 0 ? catalogAssets.selectedIndex : 0;
        const nextIndex = Math.max(0, Math.min(catalogAssets.assetCount - 1, baseIndex + offset));
        if (nextIndex === baseIndex)
            return;

        catalogAssets.selectIndex(nextIndex);
    }

    Connections {
        target: editorPreview

        function onPreviewChanged() {
            editedDisplaySource = editorPreview.previewSource;
            referenceDisplaySource = editorPreview.referencePreviewSource;
        }

        function onCompareModeChanged() {
            if (!fitToView)
                centerZoomPreview();
        }
    }

    Connections {
        target: catalogAssets

        function onSelectedIndexChanged() {
            if (catalogAssets.selectedIndex >= 0)
                filmstripView.positionViewAtIndex(catalogAssets.selectedIndex, ListView.Center);
        }
    }

    Shortcut {
        sequence: "Left"
        context: Qt.WindowShortcut
        enabled: catalogAssets.selectedIndex > 0
        onActivated: selectRelativeAsset(-1)
    }

    Shortcut {
        sequence: "Right"
        context: Qt.WindowShortcut
        enabled: catalogAssets.selectedIndex >= 0 && catalogAssets.selectedIndex < catalogAssets.assetCount - 1
        onActivated: selectRelativeAsset(1)
    }

    Component.onCompleted: {
        editedDisplaySource = editorPreview.previewSource;
        referenceDisplaySource = editorPreview.referencePreviewSource;
    }

    RowLayout {
        anchors.fill: parent
        spacing: 12

        BrandPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            headerVisible: false
            bodyMargin: 4
            bodySpacing: 6
            title: "Editor"

            Rectangle {
                id: previewSurface
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: "#14171a"
                clip: true

                Item {
                    id: fitPreviewRow
                    anchors.fill: parent
                    anchors.margins: 4
                    visible: fitToView

                    Image {
                        id: editedPreviewFit
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.right: editorPreview.compareMode ? compareFitDivider.left : parent.right
                        anchors.rightMargin: editorPreview.compareMode ? 8 : 0
                        source: editedDisplaySource
                        fillMode: Image.PreserveAspectFit
                        asynchronous: false
                        cache: false
                        visible: editedDisplaySource.length > 0
                    }

                    Rectangle {
                        id: compareFitDivider
                        visible: editorPreview.compareMode && editedPreviewFit.visible
                        width: 1
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: "#2d3339"
                    }

                    Image {
                        id: referencePreviewFit
                        anchors.left: compareFitDivider.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.right: parent.right
                        anchors.leftMargin: 8
                        source: referenceDisplaySource
                        fillMode: Image.PreserveAspectFit
                        asynchronous: false
                        cache: false
                        visible: editorPreview.compareMode && referenceDisplaySource.length > 0
                    }
                }

                MouseArea {
                    anchors.fill: fitPreviewRow
                    visible: fitToView && editorPreview.whiteBalanceSampling && editedPreviewFit.visible
                    cursorShape: Qt.CrossCursor
                    onClicked: function (mouse) {
                        const imageX = editedPreviewFit.x + (editedPreviewFit.width - editedPreviewFit.paintedWidth) / 2;
                        const imageY = editedPreviewFit.y + (editedPreviewFit.height - editedPreviewFit.paintedHeight) / 2;
                        const imageWidth = editedPreviewFit.paintedWidth;
                        const imageHeight = editedPreviewFit.paintedHeight;

                        if (mouse.x < imageX || mouse.x > imageX + imageWidth || mouse.y < imageY || mouse.y > imageY + imageHeight) {
                            editorPreview.whiteBalanceSampling = false;
                            return;
                        }

                        editorPreview.sampleWhiteBalanceAt((mouse.x - imageX) / imageWidth, (mouse.y - imageY) / imageHeight);
                    }
                }

                Flickable {
                    id: zoomPreviewFlick
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: false
                    visible: !fitToView
                    contentWidth: zoomPreviewContent.width
                    contentHeight: zoomPreviewContent.height
                    onContentWidthChanged: clampPanPosition()
                    onContentHeightChanged: clampPanPosition()

                    Item {
                        id: zoomPreviewContent
                        width: editorPreview.compareMode
                               ? Math.max(zoomPreviewFlick.width, editedPreviewZoom.width + referencePreviewZoom.width + 16)
                               : Math.max(zoomPreviewFlick.width, editedPreviewZoom.width)
                        height: Math.max(zoomPreviewFlick.height, Math.max(editedPreviewZoom.height, referencePreviewZoom.height))

                        Image {
                            id: editedPreviewZoom
                            x: editorPreview.compareMode
                               ? Math.max(0, (zoomPreviewContent.width - (editedPreviewZoom.width + referencePreviewZoom.width + 16)) / 2)
                               : Math.max(0, (zoomPreviewContent.width - editedPreviewZoom.width) / 2)
                            y: Math.max(0, (zoomPreviewContent.height - editedPreviewZoom.height) / 2)
                            width: imagePixelWidth(editedPreviewZoom) * manualZoom
                            height: imagePixelHeight(editedPreviewZoom) * manualZoom
                            source: editedDisplaySource
                            fillMode: Image.Stretch
                            asynchronous: true
                            cache: false
                            visible: editedDisplaySource.length > 0
                        }

                        Image {
                            id: referencePreviewZoom
                            x: editorPreview.compareMode ? editedPreviewZoom.x + editedPreviewZoom.width + 16 : 0
                            y: Math.max(0, (zoomPreviewContent.height - referencePreviewZoom.height) / 2)
                            width: imagePixelWidth(referencePreviewZoom) * manualZoom
                            height: imagePixelHeight(referencePreviewZoom) * manualZoom
                            source: referenceDisplaySource
                            fillMode: Image.Stretch
                            asynchronous: true
                            cache: false
                            visible: editorPreview.compareMode && referenceDisplaySource.length > 0
                        }

                        MouseArea {
                            anchors.fill: parent
                            visible: editorPreview.whiteBalanceSampling && editedPreviewZoom.visible
                            cursorShape: Qt.CrossCursor
                            onClicked: function (mouse) {
                                if (mouse.x < editedPreviewZoom.x || mouse.x > editedPreviewZoom.x + editedPreviewZoom.width || mouse.y < editedPreviewZoom.y || mouse.y > editedPreviewZoom.y + editedPreviewZoom.height) {
                                    editorPreview.whiteBalanceSampling = false;
                                    return;
                                }

                                editorPreview.sampleWhiteBalanceAt((mouse.x - editedPreviewZoom.x) / editedPreviewZoom.width, (mouse.y - editedPreviewZoom.y) / editedPreviewZoom.height);
                            }
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    propagateComposedEvents: true
                    enabled: editorPreview.hasAsset
                    onWheel: function (wheel) {
                        const delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.pixelDelta.y;
                        if (delta === 0)
                            return;

                        const zoomFactor = delta > 0 ? 1.05 : (1 / 1.05);
                        setZoomLevel(currentZoomLevel() * zoomFactor, wheel.x, wheel.y);
                        wheel.accepted = true;
                    }
                }

                MouseArea {
                    anchors.fill: zoomPreviewFlick
                    visible: !fitToView && !editorPreview.whiteBalanceSampling
                    acceptedButtons: Qt.LeftButton
                    preventStealing: true
                    cursorShape: maxPanX() > 0 || maxPanY() > 0 ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.ArrowCursor

                    property real lastX: 0
                    property real lastY: 0

                    onPressed: function (mouse) {
                        lastX = mouse.x;
                        lastY = mouse.y;
                    }

                    onPositionChanged: function (mouse) {
                        if (!pressed)
                            return;

                        zoomPreviewFlick.contentX -= mouse.x - lastX;
                        zoomPreviewFlick.contentY -= mouse.y - lastY;
                        clampPanPosition();
                        lastX = mouse.x;
                        lastY = mouse.y;
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 4
                    radius: 10
                    visible: editorPreview.previewSource.length === 0
                    color: "#181c20"

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 8

                        Label {
                            text: catalogAssets.selectedAssetId.length > 0 ? "Preview unavailable for this asset" : "Select an image in Library"
                            color: "#f2f4f6"
                            font.family: "Avenir Next"
                            font.pixelSize: 18
                            font.weight: Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: catalogAssets.selectedAssetId.length > 0 ? "RAW decode is not wired yet for this source path." : "The editor is selection-driven. Pick an image in Library to load the preview and recipe."
                            color: "#8f98a2"
                            font.family: "Avenir Next"
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            width: 420
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 74
                radius: 10
                color: "#1a1d21"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label {
                            Layout.fillWidth: true
                            text: editorPreview.processing ? "Rendering preview..." : editorPreview.status
                            color: "#8e98a2"
                            font.family: "Avenir Next"
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 24
                            radius: 6
                            color: fitToView ? Qt.rgba(0.788, 0.604, 0.322, 0.14) : "#151a1f"

                            Label {
                                anchors.centerIn: parent
                                text: "Fit"
                                color: fitToView ? "#f0d4a8" : "#d2d9df"
                                font.family: "Avenir Next"
                                font.pixelSize: 10
                                font.weight: Font.Medium
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    fitToView = true;
                                    zoomPreviewFlick.contentX = 0;
                                    zoomPreviewFlick.contentY = 0;
                                }
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 24
                            radius: 6
                            color: !fitToView && Math.abs(manualZoom - 1.0) < 0.01 ? Qt.rgba(0.788, 0.604, 0.322, 0.14) : "#151a1f"

                            Label {
                                anchors.centerIn: parent
                                text: "1:1"
                                color: !fitToView && Math.abs(manualZoom - 1.0) < 0.01 ? "#f0d4a8" : "#d2d9df"
                                font.family: "Menlo"
                                font.pixelSize: 10
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    setZoomLevel(1.0);
                                    centerZoomPreview();
                                }
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 74
                            Layout.preferredHeight: 24
                            radius: 6
                            color: editorPreview.compareMode ? Qt.rgba(0.788, 0.604, 0.322, 0.14) : "#151a1f"
                            opacity: editorPreview.hasAsset ? 1.0 : 0.55

                            Label {
                                anchors.centerIn: parent
                                text: "Compare"
                                color: editorPreview.compareMode ? "#f0d4a8" : "#d2d9df"
                                font.family: "Avenir Next"
                                font.pixelSize: 10
                                font.weight: Font.Medium
                            }

                            MouseArea {
                                anchors.fill: parent
                                enabled: editorPreview.hasAsset
                                onClicked: editorPreview.compareMode = !editorPreview.compareMode
                            }
                        }

                        Label {
                            text: fitToView ? "Fit" : (Math.round(manualZoom * 100) + "%")
                            color: "#8f98a2"
                            font.family: "Menlo"
                            font.pixelSize: 10
                        }
                    }

                    ListView {
                        id: filmstripView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        orientation: ListView.Horizontal
                        boundsBehavior: Flickable.StopAtBounds
                        spacing: 8
                        model: catalogAssets
                        currentIndex: catalogAssets.selectedIndex

                        delegate: Rectangle {
                            required property int index
                            required property string assetId
                            required property string thumbnailSource

                            width: 52
                            height: filmstripView.height
                            radius: 5
                            color: catalogAssets.selectedIndex === index ? "#1d2228" : "#15191e"

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 3
                                radius: 4
                                color: "#11161b"
                                clip: true

                                Image {
                                    anchors.fill: parent
                                    source: thumbnailSource
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    cache: false
                                    visible: thumbnailSource.length > 0
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    color: "#20262d"
                                    visible: thumbnailSource.length === 0
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: catalogAssets.selectIndex(index)
                            }
                        }

                        footer: Item {
                            width: 10
                            height: filmstripView.height
                        }
                    }
                }
            }
        }

        Item {
            Layout.preferredWidth: inspectorCollapsed ? 28 : 348
            Layout.fillHeight: true

            Rectangle {
                anchors.fill: parent
                visible: inspectorCollapsed
                radius: 10
                color: "#16191d"

                Label {
                    anchors.centerIn: parent
                    text: "<"
                    color: "#d5dbe1"
                    font.family: "Menlo"
                    font.pixelSize: 12
                    rotation: 180
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: inspectorCollapsed = false
                }
            }

            BrandPanel {
                anchors.fill: parent
                visible: !inspectorCollapsed
                headerVisible: false
                bodyMargin: 6
                bodySpacing: 6
                title: "Inspector"

                Rectangle {
                    implicitHeight: inspectorSummary.implicitHeight + 16
                    Layout.fillWidth: true
                    radius: 8
                    color: "#14181d"

                    ColumnLayout {
                        id: inspectorSummary
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Rectangle {
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                radius: 7
                                color: "#1a1f24"

                                Label {
                                    anchors.centerIn: parent
                                    text: "<"
                                    color: "#d5dbe1"
                                    font.family: "Menlo"
                                    font.pixelSize: 12
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: inspectorCollapsed = true
                                }
                            }

                            Label {
                                text: "Adjust"
                                color: "#d8dee4"
                                font.family: "Avenir Next"
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            ActionButton {
                                text: editorPreview.whiteBalanceSampling ? "Cancel" : "WB"
                                active: editorPreview.whiteBalanceSampling
                                enabled: editorPreview.hasAsset
                                font.pixelSize: 10
                                implicitWidth: 58
                                implicitHeight: 28
                                onClicked: editorPreview.whiteBalanceSampling = !editorPreview.whiteBalanceSampling
                            }

                            ActionButton {
                                text: editorPreview.compareMode ? "Compare On" : "Compare"
                                active: editorPreview.compareMode
                                enabled: editorPreview.hasAsset
                                font.pixelSize: 10
                                implicitWidth: 90
                                implicitHeight: 28
                                onClicked: editorPreview.compareMode = !editorPreview.compareMode
                            }

                            ActionButton {
                                text: "Reset"
                                enabled: editorPreview.hasAsset
                                font.pixelSize: 10
                                implicitWidth: 64
                                implicitHeight: 28
                                onClicked: editorPreview.resetAdjustments()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 28
                                radius: 7
                                color: "#171c21"

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 8

                                    Rectangle {
                                        Layout.preferredWidth: 5
                                        Layout.preferredHeight: 5
                                        radius: 3
                                        color: "#c99a52"
                                    }

                                    Label {
                                        text: catalogAssets.selectedRecipeJson.length > 0 ? "Edited" : "Base"
                                        color: "#e8edf1"
                                        font.family: "Avenir Next"
                                        font.pixelSize: 10
                                        font.weight: Font.Medium
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                    }

                                    Label {
                                        text: editorPreview.compareMode ? "Compare" : "Single"
                                        color: "#8f98a2"
                                        font.family: "Menlo"
                                        font.pixelSize: 9
                                    }

                                    Label {
                                        text: editorPreview.linearMaskEnabled ? "Mask On" : "Mask Off"
                                        color: "#8f98a2"
                                        font.family: "Menlo"
                                        font.pixelSize: 9
                                    }
                                }
                            }
                        }
                    }
                }

                ScrollView {
                    id: inspectorScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth

                    Column {
                        width: inspectorScroll.availableWidth
                        spacing: 10

                        InspectorSection {
                            width: parent.width
                            title: "Light"
                            expandable: true
                            expanded: true

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Exposure"
                                valueText: editorPreview.exposure.toFixed(2) + " EV"
                                from: -2
                                to: 2
                                value: editorPreview.exposure
                                onValueDragged: editorPreview.exposure = value
                                onResetRequested: editorPreview.exposure = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Contrast"
                                valueText: Math.round(editorPreview.contrast).toString()
                                from: -100
                                to: 100
                                value: editorPreview.contrast
                                onValueDragged: editorPreview.contrast = value
                                onResetRequested: editorPreview.contrast = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Highlights"
                                valueText: Math.round(editorPreview.highlights).toString()
                                from: -100
                                to: 100
                                value: editorPreview.highlights
                                onValueDragged: editorPreview.highlights = value
                                onResetRequested: editorPreview.highlights = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Shadows"
                                valueText: Math.round(editorPreview.shadows).toString()
                                from: -100
                                to: 100
                                value: editorPreview.shadows
                                onValueDragged: editorPreview.shadows = value
                                onResetRequested: editorPreview.shadows = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Whites"
                                valueText: Math.round(editorPreview.whites).toString()
                                from: -100
                                to: 100
                                value: editorPreview.whites
                                onValueDragged: editorPreview.whites = value
                                onResetRequested: editorPreview.whites = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Blacks"
                                valueText: Math.round(editorPreview.blacks).toString()
                                from: -100
                                to: 100
                                value: editorPreview.blacks
                                onValueDragged: editorPreview.blacks = value
                                onResetRequested: editorPreview.blacks = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            title: "Presence"
                            expandable: true
                            expanded: true

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Texture"
                                valueText: Math.round(editorPreview.texture).toString()
                                from: -100
                                to: 100
                                value: editorPreview.texture
                                onValueDragged: editorPreview.texture = value
                                onResetRequested: editorPreview.texture = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Clarity"
                                valueText: Math.round(editorPreview.clarity).toString()
                                from: -100
                                to: 100
                                value: editorPreview.clarity
                                onValueDragged: editorPreview.clarity = value
                                onResetRequested: editorPreview.clarity = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Dehaze"
                                valueText: Math.round(editorPreview.dehaze).toString()
                                from: -100
                                to: 100
                                value: editorPreview.dehaze
                                onValueDragged: editorPreview.dehaze = value
                                onResetRequested: editorPreview.dehaze = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            title: "Color"
                            expandable: true
                            expanded: true

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Temperature"
                                valueText: Math.round(editorPreview.temperature).toString()
                                trackColors: ["#4c78cb", "#c8d0d8", "#e6a14f"]
                                from: -100
                                to: 100
                                value: editorPreview.temperature
                                onValueDragged: editorPreview.temperature = value
                                onResetRequested: editorPreview.temperature = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Tint"
                                valueText: Math.round(editorPreview.tint).toString()
                                trackColors: ["#4ea56b", "#c8d0d8", "#bb67d6"]
                                from: -100
                                to: 100
                                value: editorPreview.tint
                                onValueDragged: editorPreview.tint = value
                                onResetRequested: editorPreview.tint = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Vibrance"
                                valueText: Math.round(editorPreview.vibrance).toString()
                                from: -100
                                to: 100
                                value: editorPreview.vibrance
                                onValueDragged: editorPreview.vibrance = value
                                onResetRequested: editorPreview.vibrance = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Saturation"
                                valueText: Math.round(editorPreview.saturation).toString()
                                from: -100
                                to: 100
                                value: editorPreview.saturation
                                onValueDragged: editorPreview.saturation = value
                                onResetRequested: editorPreview.saturation = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            title: "Tone Curve"
                            expandable: true
                            expanded: true

                            ToneCurveEditor {
                                Layout.fillWidth: true
                                shadowsValue: editorPreview.curveShadows
                                darksValue: editorPreview.curveDarks
                                lightsValue: editorPreview.curveLights
                                highlightsValue: editorPreview.curveHighlights
                                onCurveValueDragged: function (pointName, value) {
                                    if (pointName === "shadows")
                                        editorPreview.curveShadows = value;
                                    else if (pointName === "darks")
                                        editorPreview.curveDarks = value;
                                    else if (pointName === "lights")
                                        editorPreview.curveLights = value;
                                    else if (pointName === "highlights")
                                        editorPreview.curveHighlights = value;
                                }
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            title: "Color Mixer"
                            expandable: true
                            expanded: true

                            Flow {
                                Layout.fillWidth: true
                                spacing: 6

                                Repeater {
                                    model: hslChannels

                                    delegate: ColorChannelButton {
                                        required property var modelData

                                        label: modelData.name
                                        swatch: modelData.color
                                        selected: editorPreview.hslChannel === modelData.name
                                        onClicked: editorPreview.hslChannel = modelData.name
                                    }
                                }
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Hue"
                                valueText: Math.round(editorPreview.hueAdjustment).toString()
                                trackColors: ["#d54f60", "#d78c38", "#d7bf43", "#61b56f", "#49b8c4", "#4e86d9", "#8b6be3", "#c85ac1"]
                                from: -100
                                to: 100
                                value: editorPreview.hueAdjustment
                                onValueDragged: editorPreview.hueAdjustment = value
                                onResetRequested: editorPreview.hueAdjustment = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Saturation"
                                valueText: Math.round(editorPreview.saturationAdjustment).toString()
                                trackColors: ["#505963", selectedHslColor(), "#ffffff"]
                                from: -100
                                to: 100
                                value: editorPreview.saturationAdjustment
                                onValueDragged: editorPreview.saturationAdjustment = value
                                onResetRequested: editorPreview.saturationAdjustment = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Luminance"
                                valueText: Math.round(editorPreview.luminanceAdjustment).toString()
                                trackColors: ["#101419", selectedHslColor(), "#eff3f6"]
                                from: -100
                                to: 100
                                value: editorPreview.luminanceAdjustment
                                onValueDragged: editorPreview.luminanceAdjustment = value
                                onResetRequested: editorPreview.luminanceAdjustment = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            title: "Crop"
                            expandable: true
                            expanded: true

                            ToneComboBox {
                                Layout.fillWidth: true
                                model: ["Original", "Square", "4x5", "16x9"]
                                currentIndex: model.indexOf(editorPreview.cropPreset)
                                onActivated: editorPreview.cropPreset = currentText
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Zoom"
                                valueText: editorPreview.cropZoom.toFixed(2) + "x"
                                from: 1
                                to: 3
                                stepSize: 0.01
                                value: editorPreview.cropZoom
                                onValueDragged: editorPreview.cropZoom = value
                                onResetRequested: editorPreview.cropZoom = 1
                            }
                        }
                    }
                }
            }
        }
    }
}
