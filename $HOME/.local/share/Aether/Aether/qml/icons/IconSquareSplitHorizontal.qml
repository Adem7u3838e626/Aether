import QtQuick
import QtQuick.Shapes

Shape {
    id: root
    property real size: 24
    property color color: "#ffffff"
    property real strokeWidth: Math.max(1, size / 12)
    width: size; height: size
    clip: false
    preferredRendererType: Shape.CurveRenderer

    ShapePath {
        strokeColor: root.color; strokeWidth: root.strokeWidth
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
        scale: Qt.size(root.size / 24, root.size / 24)
        PathSvg { path: "M8 19H5c-1 0-2-1-2-2V7c0-1 1-2 2-2h3" }
    }
    ShapePath {
        strokeColor: root.color; strokeWidth: root.strokeWidth
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
        scale: Qt.size(root.size / 24, root.size / 24)
        PathSvg { path: "M16 5h3c1 0 2 1 2 2v10c0 1-1 2-2 2h-3" }
    }
    ShapePath {
        strokeColor: root.color; strokeWidth: root.strokeWidth
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
        scale: Qt.size(root.size / 24, root.size / 24)
        PathSvg { path: "M12 4v16" }
    }
}
