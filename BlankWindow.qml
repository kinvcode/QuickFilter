import QtQuick

Window {
    id: win
    width: 640
    height: 480
    visible: false
    title: "空白窗口"
    color: "#07101c"

    // 关闭只隐藏，实例留给主窗口复用，保证同一种窗口同时只有一个
    onClosing: function(close) {
        close.accepted = false
        hide()
    }
}
