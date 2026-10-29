import QtQuick
import QtQuick.Shapes

Shape {
    id: root
    property real size: 24
    property color color: "#C08030"
    property real strokeWidth: 0
    width: size; height: size
    clip: false
    preferredRendererType: Shape.CurveRenderer

    ShapePath {
        strokeColor: "transparent"; strokeWidth: 0
        fillColor: root.color
        scale: Qt.size(root.size / 24, root.size / 24)
        PathSvg { path: "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20z" }
    }
}
