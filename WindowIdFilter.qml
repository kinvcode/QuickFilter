import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1080
    height: 700
    minimumWidth: 1000
    minimumHeight: 640
    visible: false
    title: "窗口ID过滤"
    color: "#07101c"
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowSystemMenuHint | Qt.WindowMinimizeButtonHint
    font.family: "Microsoft YaHei UI"
    font.pixelSize: 13

    WindowIdFilterController {
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
            if (index < widths.length)
                return widths[index]
            let used = 0
            for (let i = 0; i < widths.length; ++i)
                used += widths[i]
            return Math.max(40, width - used)
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
                Text { text: "窗口ID过滤"; color: "#e7f2ff"; font.pixelSize: 14 }
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

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 620
                    color: "#09111d"
                    radius: 8
                    border.color: "#1c314d"
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0
                        TableHeader {
                            Layout.fillWidth: true
                            titles: ["时间", "窗口类型", "窗口名称"]
                            widths: [140, 140]
                        }
                        ListView {
                            id: messageView
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: controller.messages
                            boundsBehavior: Flickable.StopAtBounds
                            // 新消息到达时跟随到底部，只在新行数增加时触发
                            onCountChanged: positionViewAtEnd()
                            delegate: Rectangle {
                                required property string time
                                required property string kind
                                required property string title
                                required property int index
                                width: ListView.view.width
                                height: 28
                                color: index % 2 === 0 ? "#0c1424" : "#101828"
                                Row {
                                    anchors.fill: parent
                                    Text {
                                        width: 140
                                        height: 28
                                        leftPadding: 8
                                        text: time
                                        color: "#e7f2ff"
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                    }
                                    Text {
                                        width: 140
                                        height: 28
                                        leftPadding: 8
                                        text: kind
                                        color: "#e7f2ff"
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                    }
                                    Text {
                                        width: parent.width - 280
                                        height: 28
                                        leftPadding: 8
                                        text: title
                                        color: "#e7f2ff"
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 340
                    Layout.fillHeight: true
                    color: "#09111d"
                    radius: 8
                    border.color: "#1c314d"
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8
                        Text {
                            text: "过滤器"
                            color: "#e7f2ff"
                            font.pixelSize: 14
                        }
                        TableHeader {
                            Layout.fillWidth: true
                            titles: ["ID过滤", "ID", "窗口名称"]
                            widths: [72, 88]
                        }
                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: controller.filters
                            boundsBehavior: Flickable.StopAtBounds
                            delegate: Rectangle {
                                required property bool active
                                required property string idText
                                required property string name
                                required property int index
                                width: ListView.view.width
                                height: 34
                                color: index % 2 === 0 ? "#0c1424" : "#101828"
                                Row {
                                    anchors.fill: parent
                                    Item {
                                        width: 72
                                        height: 34
                                        CheckBox {
                                            id: filterBox
                                            anchors.centerIn: parent
                                            checked: active
                                            padding: 0
                                            onToggled: controller.setFilterActive(index, checked)
                                            indicator: Rectangle {
                                                implicitWidth: 16
                                                implicitHeight: 16
                                                x: filterBox.leftPadding
                                                y: filterBox.height / 2 - height / 2
                                                radius: 3
                                                border.width: 1
                                                border.color: filterBox.checked ? "#3ee0ff" : "#3d5270"
                                                color: filterBox.checked ? "#3ee0ff" : "transparent"
                                                Rectangle {
                                                    visible: filterBox.checked
                                                    anchors.centerIn: parent
                                                    width: 8
                                                    height: 8
                                                    radius: 2
                                                    color: "#061018"
                                                }
                                            }
                                        }
                                    }
                                    Text {
                                        width: 88
                                        height: 34
                                        leftPadding: 8
                                        text: idText
                                        color: "#e7f2ff"
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 12
                                    }
                                    Text {
                                        width: parent.width - 160
                                        height: 34
                                        leftPadding: 8
                                        rightPadding: 8
                                        text: name
                                        color: "#e7f2ff"
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
