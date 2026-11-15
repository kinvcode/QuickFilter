import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 720
    height: 560
    minimumWidth: 560
    minimumHeight: 480
    visible: true
    title: "变色龙"
    color: "#07101c"
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowSystemMenuHint | Qt.WindowMinimizeButtonHint
    font.family: "Microsoft YaHei UI"
    font.pixelSize: 13

    property string notice: ""
    property var windowSlots: ({})

    // key 对应一种窗口。已有实例只激活，不重复创建
    function showBlankWindow(key, title) {
        const existing = windowSlots[key]
        if (existing) {
            existing.show()
            existing.raise()
            existing.requestActivate()
            return
        }

        const component = Qt.createComponent("BlankWindow.qml")
        if (component.status !== Component.Ready) {
            notice = component.errorString()
            return
        }

        const window = component.createObject(null, { title: title })
        if (!window) {
            notice = component.errorString()
            return
        }

        const slots = windowSlots
        slots[key] = window
        windowSlots = slots
        window.show()
        window.raise()
        window.requestActivate()
        notice = ""
    }

    function destroyBlankWindows() {
        const slots = windowSlots
        for (const key in slots) {
            if (slots[key])
                slots[key].destroy()
        }
        windowSlots = ({})
    }

    onClosing: function(close) {
        destroyBlankWindows()
        close.accepted = true
    }

    Rectangle {
        anchors.fill: parent
        color: "#07101c"

        Rectangle {
            id: titleBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 42
            color: "#0b1626"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 8
                Text { text: "变色龙"; color: "#3ee0ff"; font.pixelSize: 16 }
                Item { Layout.fillWidth: true }
                NeonButton { text: "—"; implicitWidth: 36; onClicked: root.showMinimized() }
                NeonButton { text: "×"; accent: "#ff4fd8"; implicitWidth: 36; onClicked: root.close() }
            }
            MouseArea {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.rightMargin: 96
                onPressed: function(mouse) {
                    if (mouse.button === Qt.LeftButton)
                        root.startSystemMove()
                }
            }
        }

        ColumnLayout {
            anchors.centerIn: parent
            width: 360
            spacing: 18
            NeonButton {
                Layout.fillWidth: true
                implicitHeight: 64
                text: "按钮测试"
                primary: true
                font.pixelSize: 18
                onClicked: root.showBlankWindow("test1", text)
            }
            NeonButton {
                Layout.fillWidth: true
                implicitHeight: 64
                text: "测试按钮2"
                primary: true
                accent: "#b388ff"
                font.pixelSize: 18
                onClicked: root.showBlankWindow("test2", text)
            }
            NeonButton {
                Layout.fillWidth: true
                implicitHeight: 64
                text: "测试按钮3"
                primary: true
                accent: "#5ee0a0"
                font.pixelSize: 18
                onClicked: root.showBlankWindow("test3", text)
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: root.notice.length > 0 || driverMessage.length > 0 ? "#ffb020" : "#7f93b0"
                text: root.notice.length > 0 ? root.notice : (driverMessage.length > 0 ? driverMessage : "驱动已就绪")
            }
        }
    }
}
