import QtQuick
import QtQuick.Controls

Button {
    id: btn
    property color accent: "#3ee0ff"
    property bool primary: false
    implicitHeight: 36
    leftPadding: 16
    rightPadding: 16
    font.pixelSize: 13
    hoverEnabled: true
    contentItem: Text {
        text: btn.text
        color: btn.enabled ? (btn.primary ? "#061018" : (btn.down ? "#ffffff" : btn.accent)) : "#6d7d95"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font: btn.font
    }
    background: Rectangle {
        radius: 9
        color: {
            if (!btn.enabled)
                return "#101826"
            if (btn.primary)
                return btn.down ? Qt.darker(btn.accent, 1.15) : (btn.hovered ? Qt.lighter(btn.accent, 1.12) : btn.accent)
            return btn.hovered ? Qt.rgba(btn.accent.r, btn.accent.g, btn.accent.b, 0.22) : Qt.rgba(btn.accent.r, btn.accent.g, btn.accent.b, 0.08)
        }
        border.color: btn.enabled ? btn.accent : "#ffffff22"
        border.width: 1
    }
}
