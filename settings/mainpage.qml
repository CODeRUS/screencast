import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0
import org.nemomobile.systemsettings 1.0

Page {
    id: page

    ConfigurationGroup {
        id: conf
        path: "/org/coderus/screencast"
        property int buffers: 60
        property real scale: 0.5
        property int quality: 90
        property bool smooth: true
    }

    DeveloperModeSettings {
        id: developerModeSettings
    }

    SilicaFlickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Screencast")
            }

            Slider {
                id: buffersSlider
                width: parent.width
                minimumValue: 2
                maximumValue: 60
                stepSize: 1
                value: conf.buffers
                label: qsTr("Buffers to store frames")
                valueText: qsTr("%1").arg(value)
                onReleased: {
                    conf.buffers = value
                }
            }

            Slider {
                id: qualitySlider
                width: parent.width
                minimumValue: 10
                maximumValue: 100
                stepSize: 1
                value: conf.quality
                label: qsTr("JPEG frames quality")
                valueText: qsTr("%1%").arg(value)
                onReleased: {
                    conf.quality = value
                }
            }

            Slider {
                id: scaleSlider
                width: parent.width
                minimumValue: 1
                maximumValue: 100
                stepSize: 1
                value: conf.scale * 100
                label: qsTr("Frames scale")
                valueText: qsTr("%1%").arg(value)
                onReleased: {
                    conf.scale = value / 100
                }
            }

            TextSwitch {
                id: smoothSwitch
                visible: conf.scale < 1.0
                text: qsTr("Apply smooth transformation")
                checked: conf.smooth
                automaticCheck: false
                onClicked: {
                    conf.smooth = !checked
                }
            }

            Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: Theme.horizontalPageMargin
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                text: qsTr("You can connect to:")
                      + (developerModeSettings.wlanIpAddress ? qsTr("\n%1:554").arg(developerModeSettings.wlanIpAddress) : "")
                      + (developerModeSettings.usbIpAddress ? qsTr("\n%1:554").arg(developerModeSettings.usbIpAddress) : "")
            }
        }

        VerticalScrollDecorator {}
    }
}
