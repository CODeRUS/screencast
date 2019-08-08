TEMPLATE = aux

settingsjson.files = screencast.json
settingsjson.path = /usr/share/jolla-settings/entries

INSTALLS += settingsjson

settingsqml.files = \
    mainpage.qml
settingsqml.path = /usr/share/jolla-settings/pages/screencast

INSTALLS += settingsqml
