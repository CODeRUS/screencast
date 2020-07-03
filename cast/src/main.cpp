#include "cast.h"

#include <QCommandLineParser>
#include <QGuiApplication>

#include <QDebug>
#include <QLoggingCategory>
#include <QTimer>

#include <signal.h>

Q_LOGGING_CATEGORY(logmain, "screencast.main", QtDebugMsg)

void handleShutDownSignal(int)
{
    Cast::instance()->handleShutDown();
}

void setShutDownSignal(int signalId)
{
    struct sigaction sa;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = handleShutDownSignal;
    if (sigaction(signalId, &sa, NULL) == -1) {
        perror("setting up termination signal");
        exit(1);
    }
}

Q_DECL_EXPORT int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationVersion(QStringLiteral(PROJECT_PACKAGE_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Screen cast"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption buffersOption(
            QStringLiteral("buffers"),
            app.translate("main", "Amount of buffers to store received frames. Default is framerate * 2."),
            app.translate("main", "buffers"));
    parser.addOption(buffersOption);

    QCommandLineOption scaleOption(
            QStringLiteral("scale"),
            app.translate("main", "Scale frames ratio."),
            app.translate("main", "scale"));
    parser.addOption(scaleOption);

    QCommandLineOption smoothOption(
            {QStringLiteral("s"), QStringLiteral("smooth")},
            app.translate("main", "Scale frames using SmoothTransformation."));
    parser.addOption(smoothOption);

    QCommandLineOption qualityOption(
            QStringLiteral("quality"),
            app.translate("main", "Frame JPEG compression quality."),
            app.translate("main", "quality"));
    parser.addOption(qualityOption);

    QCommandLineOption daemonOption(
            {QStringLiteral("d"), QStringLiteral("daemon")},
            app.translate("main", "Daemonize screencast. Will create D-Bus service org.coderus.screencast on system bus."));
    parser.addOption(daemonOption);

    parser.process(app);

    Options options = Cast::readOptions();
    options.daemonize = parser.isSet(daemonOption);
    if (options.daemonize) {
        qCDebug(logmain) << "Daemonize";
    } else {
        if (parser.isSet(buffersOption)) {
            options.buffers = parser.value(buffersOption).toInt();
        }
        if (parser.isSet(scaleOption)) {
            options.scale = parser.value(scaleOption).toDouble();
        }
        if (parser.isSet(qualityOption)) {
            options.quality = parser.value(qualityOption).toInt();
        }
        options.smooth = parser.isSet(smoothOption);

        setShutDownSignal(SIGINT); // shut down on ctrl-c
        setShutDownSignal(SIGTERM); // shut down on killall
    }

    Cast *cast = new Cast(options, qGuiApp);

    QTimer::singleShot(0, cast, &Cast::init);

    return app.exec();
}
