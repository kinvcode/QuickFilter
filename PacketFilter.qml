import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1280
    height: 820
    minimumWidth: 1080
    minimumHeight: 680
    visible: false
    title: "收包过滤"
    color: "#07101c"
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowSystemMenuHint | Qt.WindowMinimizeButtonHint
    font.family: "Microsoft YaHei UI"
    font.pixelSize: 13

    property int selectedPacketRow: -1
    property int contextFilterRow: -1
    property int memoryDataType: 0
    property bool memoryHexadecimal: true
    property int memoryTextEncoding: 0
    property bool autoScroll: true

    PacketFilterController {
        id: controller
    }

    Component.onCompleted: controller.bindWindow(win)

    // 关闭只隐藏，实例留给主窗口复用；关闭前先确认并清理 HOOK
    onClosing: function(close) {
        close.accepted = false
        confirmDialog.open()
    }

    Dialog {
        id: confirmDialog
        anchors.centerIn: parent
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        padding: 18
        background: Rectangle {
            color: "#122033"
            radius: 10
            border.color: "#3ee0ff"
        }
        contentItem: ColumnLayout {
            spacing: 16
            Text {
                Layout.preferredWidth: 320
                text: "是否确认关闭？关闭后过滤数据不可恢复"
                color: "#e7f2ff"
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Item { Layout.fillWidth: true }
                NeonButton {
                    text: "取消"
                    implicitWidth: 96
                    onClicked: confirmDialog.close()
                }
                NeonButton {
                    text: "确认"
                    primary: true
                    accent: "#ff4fd8"
                    implicitWidth: 96
                    onClicked: {
                        // 清理失败时留在窗口内，提示由 setNotice 显示
                        if (!controller.prepareClose())
                            return
                        confirmDialog.close()
                        win.hide()
                    }
                }
            }
        }
    }

    component TableHeader: Rectangle {
        id: header
        property var titles: []
        property var widths: []
        color: "#122033"
        implicitHeight: 32
        function columnWidth(index) {
            let stretch = titles.length - 1
            let used = 0
            for (let i = 0; i < titles.length; ++i) {
                const fixed = i < widths.length ? widths[i] : -1
                if (fixed < 0)
                    stretch = i
                else
                    used += fixed
            }
            if (index === stretch)
                return Math.max(40, width - used)
            return widths[index]
        }
        Row {
            anchors.fill: parent
            Repeater {
                model: header.titles
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    width: header.columnWidth(index)
                    height: 32
                    color: "#122033"
                    border.color: "#1c314d"
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        text: modelData
                        color: "#9eb4d6"
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 12
                    }
                }
            }
        }
    }

    Menu {
        id: filterMenu
        MenuItem {
            text: "复制"
            enabled: win.contextFilterRow >= 0
            onTriggered: controller.duplicateFilter(win.contextFilterRow)
        }
        MenuItem {
            text: "删除"
            enabled: win.contextFilterRow >= 0
            onTriggered: {
                deleteFilterDialog.filterRow = win.contextFilterRow
                deleteFilterDialog.open()
            }
        }
        MenuSeparator {}
        MenuItem {
            text: "新增滤镜"
            onTriggered: controller.addFilter()
        }
        MenuItem {
            text: "全部启用"
            onTriggered: controller.setAllFiltersActive(true)
        }
        MenuItem {
            text: "全部禁用"
            onTriggered: controller.setAllFiltersActive(false)
        }
    }

    Dialog {
        id: deleteFilterDialog
        property int filterRow: -1
        anchors.centerIn: parent
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        padding: 18
        background: Rectangle {
            color: "#122033"
            radius: 10
            border.color: "#ff4fd8"
        }
        contentItem: ColumnLayout {
            spacing: 16
            Text {
                text: "确定删除该滤镜吗？"
                color: "#e7f2ff"
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                NeonButton {
                    text: "取消"
                    implicitWidth: 88
                    onClicked: deleteFilterDialog.close()
                }
                NeonButton {
                    text: "删除"
                    primary: true
                    accent: "#ff4fd8"
                    implicitWidth: 88
                    onClicked: {
                        controller.deleteFilter(deleteFilterDialog.filterRow)
                        win.contextFilterRow = -1
                        deleteFilterDialog.close()
                    }
                }
            }
        }
    }

    Dialog {
        id: filterEditor
        property int filterRow: -1
        property var patternValues: []
        property int filledCount: 0
        property string errorText: ""

        function recalculateFilledCount() {
            let count = 0
            for (let i = 0; i < patternValues.length; ++i) {
                if (String(patternValues[i]).trim().length > 0)
                    ++count
            }
            filledCount = count
        }

        function setPatternValue(index, value) {
            patternValues[index] = value
            recalculateFilledCount()
        }

        function openFor(row) {
            const details = controller.filterDetails(row)
            if (!details || details.name === undefined)
                return
            filterRow = row
            filterNameField.text = details.name
            behaviorBox.currentIndex = details.behavior
            const values = []
            for (let i = 0; i < details.values.length; ++i)
                values.push(String(details.values[i]))
            patternValues = values
            filledCount = details.filledCount
            errorText = ""
            open()
        }

        anchors.centerIn: parent
        width: Math.min(win.width - 80, 1120)
        height: 270
        modal: true
        focus: true
        padding: 18
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: "#122033"
            radius: 10
            border.color: "#3ee0ff"
        }
        contentItem: ColumnLayout {
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Text { text: "滤镜名称"; color: "#9eb4d6" }
                TextField {
                    id: filterNameField
                    Layout.fillWidth: true
                    color: "#e7f2ff"
                    selectByMouse: true
                    background: Rectangle {
                        color: "#0c1424"
                        radius: 6
                        border.color: "#1c314d"
                    }
                }
                Text { text: "行为"; color: "#9eb4d6" }
                ComboBox {
                    id: behaviorBox
                    model: ["高亮", "过滤"]
                    Layout.preferredWidth: 110
                }
                Text {
                    text: "已填写 " + filterEditor.filledCount + " 格"
                    color: "#3ee0ff"
                    Layout.preferredWidth: 110
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 76
                color: "#09111d"
                border.color: "#1c314d"
                clip: true
                ListView {
                    id: patternView
                    anchors.fill: parent
                    orientation: ListView.Horizontal
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    spacing: 0
                    model: 1024
                    delegate: Column {
                        required property int index
                        width: 54
                        height: 74
                        Rectangle {
                            width: 54
                            height: 28
                            color: "#122033"
                            border.color: "#1c314d"
                            Text {
                                anchors.centerIn: parent
                                text: String(index + 1).padStart(3, "0")
                                color: "#9eb4d6"
                                font.family: "Consolas"
                                font.pixelSize: 11
                            }
                        }
                        TextField {
                            width: 54
                            height: 44
                            text: filterEditor.patternValues[index] || ""
                            color: "#e7f2ff"
                            horizontalAlignment: Text.AlignHCenter
                            selectByMouse: true
                            font.family: "Consolas"
                            maximumLength: 4
                            validator: RegularExpressionValidator {
                                regularExpression: /^(?:|(?:0[xX])?[0-9A-Fa-f]{1,2})$/
                            }
                            background: Rectangle {
                                color: "#0c1424"
                                border.color: parent.activeFocus ? "#3ee0ff" : "#1c314d"
                            }
                            onTextEdited: filterEditor.setPatternValue(index, text)
                            onEditingFinished: text = text.toUpperCase()
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: filterEditor.errorText.length > 0
                text: filterEditor.errorText
                color: "#ffb020"
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                NeonButton {
                    text: "取消"
                    implicitWidth: 90
                    onClicked: filterEditor.close()
                }
                NeonButton {
                    text: "保存"
                    primary: true
                    implicitWidth: 90
                    onClicked: {
                        if (controller.saveFilter(
                                filterEditor.filterRow,
                                filterNameField.text,
                                behaviorBox.currentIndex,
                                filterEditor.patternValues)) {
                            filterEditor.close()
                        } else {
                            filterEditor.errorText = "存在无效的十六进制字节"
                        }
                    }
                }
            }
        }
    }

    Menu {
        id: memoryMenu
        Menu {
            title: "数据类型"
            MenuItem {
                text: "字节"
                checkable: true
                checked: win.memoryDataType === 0
                onTriggered: {
                    win.memoryDataType = 0
                    controller.setMemoryDisplay(0, win.memoryHexadecimal, win.memoryTextEncoding)
                }
            }
            MenuItem {
                text: "双字节"
                checkable: true
                checked: win.memoryDataType === 1
                onTriggered: {
                    win.memoryDataType = 1
                    controller.setMemoryDisplay(1, win.memoryHexadecimal, win.memoryTextEncoding)
                }
            }
            MenuItem {
                text: "四字节"
                checkable: true
                checked: win.memoryDataType === 2
                onTriggered: {
                    win.memoryDataType = 2
                    controller.setMemoryDisplay(2, win.memoryHexadecimal, win.memoryTextEncoding)
                }
            }
            MenuItem {
                text: "八字节"
                checkable: true
                checked: win.memoryDataType === 3
                onTriggered: {
                    win.memoryDataType = 3
                    controller.setMemoryDisplay(3, win.memoryHexadecimal, win.memoryTextEncoding)
                }
            }
            MenuItem {
                text: "浮点数"
                checkable: true
                checked: win.memoryDataType === 4
                onTriggered: {
                    win.memoryDataType = 4
                    controller.setMemoryDisplay(4, win.memoryHexadecimal, win.memoryTextEncoding)
                }
            }
            MenuItem {
                text: "双浮点"
                checkable: true
                checked: win.memoryDataType === 5
                onTriggered: {
                    win.memoryDataType = 5
                    controller.setMemoryDisplay(5, win.memoryHexadecimal, win.memoryTextEncoding)
                }
            }
        }
        Menu {
            title: "整数进制"
            enabled: win.memoryDataType < 4
            MenuItem {
                text: "十六进制"
                checkable: true
                checked: win.memoryHexadecimal
                onTriggered: {
                    win.memoryHexadecimal = true
                    controller.setMemoryDisplay(win.memoryDataType, true, win.memoryTextEncoding)
                }
            }
            MenuItem {
                text: "十进制"
                checkable: true
                checked: !win.memoryHexadecimal
                onTriggered: {
                    win.memoryHexadecimal = false
                    controller.setMemoryDisplay(win.memoryDataType, false, win.memoryTextEncoding)
                }
            }
        }
        Menu {
            title: "文本编码"
            MenuItem {
                text: "ASCII"
                checkable: true
                checked: win.memoryTextEncoding === 0
                onTriggered: {
                    win.memoryTextEncoding = 0
                    controller.setMemoryDisplay(win.memoryDataType, win.memoryHexadecimal, 0)
                }
            }
            MenuItem {
                text: "UNICODE (UTF-16)"
                checkable: true
                checked: win.memoryTextEncoding === 1
                onTriggered: {
                    win.memoryTextEncoding = 1
                    controller.setMemoryDisplay(win.memoryDataType, win.memoryHexadecimal, 1)
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#07101c"

        Rectangle {
            id: titleBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 40
            color: "#0b1626"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 8
                Text { text: "收包过滤"; color: "#e7f2ff"; font.pixelSize: 14 }
                Item { Layout.fillWidth: true }
                NeonButton { text: "—"; implicitWidth: 36; onClicked: win.showMinimized() }
                NeonButton { text: "×"; accent: "#ff4fd8"; implicitWidth: 36; onClicked: win.close() }
            }
            MouseArea {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.rightMargin: 96
                onPressed: function(mouse) {
                    if (mouse.button === Qt.LeftButton)
                        win.startSystemMove()
                }
            }
        }

        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: titleBar.bottom
            anchors.bottom: parent.bottom
            anchors.margins: 16
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Text {
                    text: "测试HOOK"
                    color: "#9eb4d6"
                }
                Text {
                    text: "进程名称"
                    color: "#e7f2ff"
                    horizontalAlignment: Text.AlignRight
                    Layout.preferredWidth: 72
                }
                TextField {
                    Layout.preferredWidth: 140
                    implicitHeight: 40
                    text: controller.processName
                    selectByMouse: true
                    color: "#e7f2ff"
                    placeholderText: "进程名称"
                    placeholderTextColor: "#6d7d95"
                    background: Rectangle {
                        color: "#0c1424"
                        radius: 8
                        border.color: "#1c314d"
                    }
                    onTextEdited: controller.processName = text
                }
                NeonButton {
                    implicitWidth: 150
                    implicitHeight: 40
                    text: "开始HOOK"
                    primary: true
                    onClicked: controller.startHook()
                }
                NeonButton {
                    implicitWidth: 150
                    implicitHeight: 40
                    text: "销毁窗口"
                    accent: "#ff4fd8"
                    onClicked: win.close()
                }
                Item { Layout.fillWidth: true }
            }

            Text {
                Layout.fillWidth: true
                visible: controller.notice.length > 0
                color: "#ffb020"
                text: controller.notice
                elide: Text.ElideRight
            }

            // 10 列布局：数据包表占整行；下一行滤镜占 2 列，内存浏览占 8 列
            GridLayout {
                id: workspace
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 10
                columnSpacing: 10
                rowSpacing: 10

                Rectangle {
                    Layout.columnSpan: 10
                    Layout.fillWidth: true
                    Layout.preferredHeight: 250
                    Layout.minimumHeight: 180
                    color: "#09111d"
                    radius: 8
                    border.color: "#1c314d"
                    clip: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 36
                            color: "#0b1626"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 6
                                Text {
                                    text: "数据包列表"
                                    color: "#e7f2ff"
                                }
                                Text {
                                    visible: controller.collectionPaused
                                    text: "已暂停"
                                    color: "#ffb020"
                                    font.pixelSize: 11
                                }
                                Item { Layout.fillWidth: true }
                                CheckBox {
                                    text: "自动滚动"
                                    checked: true
                                    palette.windowText: "#9eb4d6"
                                    onToggled: win.autoScroll = checked
                                }
                                NeonButton {
                                    text: "暂停"
                                    enabled: !controller.collectionPaused
                                    implicitHeight: 28
                                    implicitWidth: 68
                                    onClicked: controller.pauseCollection()
                                }
                                NeonButton {
                                    text: "恢复"
                                    enabled: controller.collectionPaused
                                    implicitHeight: 28
                                    implicitWidth: 68
                                    onClicked: controller.resumeCollection()
                                }
                                NeonButton {
                                    text: "清空列表"
                                    implicitHeight: 28
                                    implicitWidth: 88
                                    onClicked: {
                                        win.selectedPacketRow = -1
                                        controller.clearPackets()
                                    }
                                }
                            }
                        }

                        TableHeader {
                            Layout.fillWidth: true
                            titles: ["序号", "时间", "缓冲区指针"]
                            widths: [80, 180]
                        }

                        ListView {
                            id: messageView
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: controller.messages
                            boundsBehavior: Flickable.StopAtBounds
                            onCountChanged: {
                                if (win.autoScroll && count > 0)
                                    positionViewAtEnd()
                            }
                            delegate: Rectangle {
                                required property string seq
                                required property string time
                                required property string pointer
                                required property bool highlighted
                                required property int index
                                width: ListView.view.width
                                height: 28
                                color: {
                                    if (win.selectedPacketRow === index)
                                        return "#214767"
                                    if (highlighted)
                                        return "#59481d"
                                    return index % 2 === 0 ? "#0c1424" : "#101828"
                                }
                                Row {
                                    anchors.fill: parent
                                    Text {
                                        width: 80
                                        height: 28
                                        leftPadding: 8
                                        text: seq
                                        color: "#e7f2ff"
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 12
                                    }
                                    Text {
                                        width: 180
                                        height: 28
                                        leftPadding: 8
                                        text: time
                                        color: "#e7f2ff"
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                    }
                                    Text {
                                        width: Math.max(40, parent.width - 260)
                                        height: 28
                                        leftPadding: 8
                                        rightPadding: 4
                                        text: pointer
                                        color: "#3ee0ff"
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                        font.family: "Consolas"
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    onClicked: {
                                        win.selectedPacketRow = index
                                        controller.selectPacket(index)
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    id: filterPanel
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 200
                    Layout.horizontalStretchFactor: 2
                    Layout.minimumHeight: 260
                    color: "#09111d"
                    radius: 8
                    border.color: "#1c314d"
                    clip: true

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onClicked: function(mouse) {
                            win.contextFilterRow = -1
                            filterMenu.popup()
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 36
                            color: "#0b1626"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 8
                                Text {
                                    text: "滤镜"
                                    color: "#e7f2ff"
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: controller.filterCount + " 项"
                                    color: "#7f93b0"
                                    font.pixelSize: 11
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 32
                            color: "#122033"
                            Row {
                                anchors.fill: parent
                                CheckBox {
                                    width: 36
                                    height: 32
                                    enabled: controller.filterCount > 0
                                    checked: controller.allFiltersActive
                                    onClicked: controller.setAllFiltersActive(checked)
                                }
                                Text {
                                    width: Math.max(70, parent.width - 156)
                                    height: 32
                                    leftPadding: 4
                                    text: "名称"
                                    color: "#9eb4d6"
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: 11
                                }
                                Text {
                                    width: 64
                                    height: 32
                                    text: "行为"
                                    color: "#9eb4d6"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: 11
                                }
                                Text {
                                    width: 56
                                    height: 32
                                    text: "格数"
                                    color: "#9eb4d6"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: 11
                                }
                            }
                        }

                        ListView {
                            id: filterView
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: controller.filters
                            boundsBehavior: Flickable.StopAtBounds
                            delegate: Rectangle {
                                required property bool active
                                required property string name
                                required property string behaviorText
                                required property int filledCount
                                required property int index
                                width: ListView.view.width
                                height: 32
                                color: win.contextFilterRow === index
                                    ? "#214767"
                                    : (index % 2 === 0 ? "#0c1424" : "#101828")

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onClicked: function(mouse) {
                                        if (mouse.button === Qt.RightButton) {
                                            win.contextFilterRow = index
                                            filterMenu.popup()
                                        }
                                    }
                                    onDoubleClicked: function(mouse) {
                                        if (mouse.button === Qt.LeftButton)
                                            filterEditor.openFor(index)
                                    }
                                }

                                Row {
                                    anchors.fill: parent
                                    CheckBox {
                                        width: 36
                                        height: 32
                                        checked: active
                                        onClicked: controller.setFilterActive(index, checked)
                                    }
                                    Text {
                                        width: Math.max(70, parent.width - 156)
                                        height: 32
                                        leftPadding: 4
                                        text: name
                                        color: "#e7f2ff"
                                        elide: Text.ElideRight
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 11
                                    }
                                    Text {
                                        width: 64
                                        height: 32
                                        text: behaviorText
                                        color: behaviorText === "过滤" ? "#ff8ca8" : "#ffcf5a"
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 11
                                    }
                                    Text {
                                        width: 56
                                        height: 32
                                        text: filledCount
                                        color: "#7f93b0"
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 11
                                    }
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: filterView.count === 0
                                text: "右键新增滤镜"
                                color: "#6d7d95"
                            }
                        }
                    }
                }

                Rectangle {
                    id: memoryPanel
                    Layout.columnSpan: 8
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 620
                    Layout.horizontalStretchFactor: 8
                    Layout.minimumHeight: 260
                    color: "#09111d"
                    radius: 8
                    border.color: "#1c314d"
                    clip: true

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: memoryMenu.popup()
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 36
                            color: "#0b1626"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                Text {
                                    text: "内存浏览"
                                    color: "#e7f2ff"
                                }
                                Text {
                                    text: controller.selectedPacketAddress
                                    color: "#3ee0ff"
                                    font.family: "Consolas"
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: "右键切换数据类型与编码"
                                    color: "#7f93b0"
                                    font.pixelSize: 11
                                }
                            }
                        }

                        ScrollView {
                            id: memoryScroll
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            contentWidth: Math.max(availableWidth, memoryContent.width)
                            contentHeight: memoryContent.implicitHeight

                            Column {
                                id: memoryContent
                                readonly property real addressWidth: 154
                                readonly property real dataWidth:
                                    Math.max(420, controller.memoryHeader.length * 8 + 24)
                                readonly property real textWidth: 180
                                width: addressWidth + dataWidth + textWidth

                                Row {
                                    width: memoryContent.width
                                    height: 32
                                    Rectangle {
                                        width: memoryContent.addressWidth
                                        height: 32
                                        color: "#122033"
                                        border.color: "#1c314d"
                                        Text {
                                            anchors.fill: parent
                                            leftPadding: 8
                                            text: "地址"
                                            color: "#9eb4d6"
                                            verticalAlignment: Text.AlignVCenter
                                            font.pixelSize: 11
                                        }
                                    }
                                    Rectangle {
                                        width: memoryContent.dataWidth
                                        height: 32
                                        color: "#122033"
                                        border.color: "#1c314d"
                                        Text {
                                            anchors.fill: parent
                                            leftPadding: 10
                                            text: controller.memoryHeader
                                            color: "#9eb4d6"
                                            verticalAlignment: Text.AlignVCenter
                                            font.family: "Consolas"
                                            font.pixelSize: 11
                                        }
                                    }
                                    Rectangle {
                                        width: memoryContent.textWidth
                                        height: 32
                                        color: "#122033"
                                        border.color: "#1c314d"
                                        Text {
                                            anchors.fill: parent
                                            leftPadding: 10
                                            text: win.memoryTextEncoding === 0 ? "ASCII" : "UNICODE (UTF-16)"
                                            color: "#9eb4d6"
                                            verticalAlignment: Text.AlignVCenter
                                            font.pixelSize: 11
                                        }
                                    }
                                }

                                Repeater {
                                    model: controller.memoryDataRows
                                    delegate: Row {
                                        required property int index
                                        required property string modelData
                                        width: memoryContent.width
                                        height: 26
                                        Rectangle {
                                            width: memoryContent.addressWidth
                                            height: 26
                                            color: index % 2 === 0 ? "#0c1424" : "#101828"
                                            Text {
                                                anchors.fill: parent
                                                leftPadding: 8
                                                text: controller.memoryAddresses[index]
                                                color: "#3ee0ff"
                                                verticalAlignment: Text.AlignVCenter
                                                font.family: "Consolas"
                                                font.pixelSize: 11
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                acceptedButtons: Qt.LeftButton
                                                cursorShape: Qt.PointingHandCursor
                                                onDoubleClicked: controller.setMemoryAddressBase(index)
                                            }
                                        }
                                        Rectangle {
                                            width: memoryContent.dataWidth
                                            height: 26
                                            color: index % 2 === 0 ? "#0c1424" : "#101828"
                                            Text {
                                                anchors.fill: parent
                                                leftPadding: 10
                                                text: modelData
                                                color: "#e7f2ff"
                                                verticalAlignment: Text.AlignVCenter
                                                font.family: "Consolas"
                                                font.pixelSize: 11
                                            }
                                        }
                                        Rectangle {
                                            width: memoryContent.textWidth
                                            height: 26
                                            color: index % 2 === 0 ? "#0c1424" : "#101828"
                                            Text {
                                                anchors.fill: parent
                                                leftPadding: 10
                                                text: controller.memoryTextRows[index]
                                                color: "#b9c9df"
                                                verticalAlignment: Text.AlignVCenter
                                                font.family: "Consolas"
                                                font.pixelSize: 11
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: controller.memoryDataRows.length === 0
                        text: "点击数据包行查看 1024 字节内存"
                        color: "#6d7d95"
                    }
                }
            }
        }
    }
}
