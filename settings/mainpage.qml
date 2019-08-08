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
        property string username
        property string password
        property var clients
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

            SectionHeader {
                text: qsTr("Main options")
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

            SectionHeader {
               text: qsTr("Authorization")
            }

            TextField {
                id: usernameField
                width: parent.width
                label: qsTr("Username")
                placeholderText: qsTr("Username")
                inputMethodHints: Qt.ImhNoAutoUppercase
                text: conf.username
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: {
                    conf.username = text
                    passwordField.focus = true
                }
            }

            PasswordField {
                id: passwordField
                width: parent.width
                label: qsTr("Password")
                placeholderText: qsTr("Password")
                text: conf.password
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: {
                    conf.password = text
                    focus = false
                }
            }

            SectionHeader {
                text: qsTr("Connection")
            }

            Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: Theme.horizontalPageMargin
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                property string authParams: (conf.username.length > 0 && conf.password.length > 0) ? "%1:%2@".arg(usernameField.text).arg(passwordField.text) : ""
                text: qsTr("You can connect to:")
                      + (developerModeSettings.wlanIpAddress ? qsTr("\nhttp://%2%1:554").arg(developerModeSettings.wlanIpAddress).arg(authParams) : "")
                      + (developerModeSettings.usbIpAddress ? qsTr("\nhttp://%2%1:554").arg(developerModeSettings.usbIpAddress).arg(authParams) : "")
            }

            SectionHeader {
                text: qsTr("%n connected clients", "0", clientsRepeater.count)
                visible: clientsRepeater.count > 0
            }

            Repeater {
                id: clientsRepeater
                model: conf.clients
                delegate: Label {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: Theme.horizontalPageMargin
                    text: modelData
                }
            }

            Item {
                height: Theme.paddingLarge
                width: 1
            }
        }

        VerticalScrollDecorator {}
    }
}
