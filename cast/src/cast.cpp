/***************************************************************************
**
** Copyright (C) 2014 Jolla Ltd.
** Contact: Giulio Camuffo <giulio.camuffo@jollamobile.com>
**
** This file is part of lipstick-recorder.
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file LICENSE.LGPL included in the packaging
** of this file.
**
****************************************************************************/

#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>

#include <QGuiApplication>
#include <QScreen>
#include <qpa/qplatformnativeinterface.h>
#include <QDebug>
#include <QThread>
#include <QMutexLocker>
#include <QTimer>
#include <QLoggingCategory>

#include <MDConfGroup>
#include <MGConfItem>
#include <QBuffer>
#include <QCryptographicHash>
#include <QOrientationSensor>
#include <QTcpServer>
#include <QTcpSocket>

#include <systemd/sd-daemon.h>

#include "wayland-lipstick-recorder-client-protocol.h"
#include "cast.h"

Q_LOGGING_CATEGORY(logcast, "screencast.cast", QtDebugMsg)
Q_LOGGING_CATEGORY(logbuffer, "screencast.cast.buffer", QtDebugMsg)

static const QByteArray c_notFoundResponse = QByteArrayLiteral(
    "HTTP/1.1 404 NOT-FOUND\r\n"
    "Content-Length: 0\r\n\r\n");

static const QByteArray c_authResponse = QByteArrayLiteral(
    "HTTP/1.1 401 UNAUTHORIZED\r\n"
    "Access-Control-Allow-Credentials: true\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Date: Thu, 08 Aug 2019 10:50:12 GMT\r\n"
    "Referrer-Policy: no-referrer-when-downgrade\r\n"
    "Server: Sailfish OS\r\n"
    "WWW-Authenticate: Basic realm=\"Sailfish OS Screencast\"\r\n"
    "X-Content-Type-Options: nosniff\r\n"
    "X-Frame-Options: DENY\r\n"
    "X-XSS-Protection: 1; mode=block\r\n"
    "Content-Length: 0\r\n\r\n");

static const QByteArray c_contentType = QByteArrayLiteral(
    "HTTP/1.0 200 OK\r\n"
    "Cache-Control: no-cache\r\n"
    "Cache-Control: private\r\n"
    "Cache-Control: no-store\r\n"
    "Pragma: no-cache\r\n"
    "Content-Type: multipart/x-mixed-replace;boundary=--boundary\r\n\r\n");

static const QByteArray c_htmlPage = QByteArrayLiteral(
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/html\r\n" \
    "Content-Length: ");

static const QByteArray c_singleImage = QByteArrayLiteral(
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: image/jpeg\r\n" \
    "Content-Length: ");

static const QByteArray c_playerPage = QByteArrayLiteral(
    "<canvas id=\"player\" style=\"background: #000;\" width=\"#WIDTH#\" height=\"#HEIGHT#\">\n"
    "  NO JS.\n"
    "</canvas>\n"
    "<script>\n"
    "  Stream = function(args) {\n"
    "    var self = this;\n"
    "    var autoStart = args.autoStart || false;\n"
    "    self.url = args.url;\n"
    "    self.refreshRate = args.refreshRate || 500;\n"
    "    self.onStart = args.onStart || null;\n"
    "    self.onFrame = args.onFrame || null;\n"
    "    self.onStop = args.onStop || null;\n"
    "    self.callbacks = {};\n"
    "    self.running = false;\n"
    "    self.frameTimer = 0;\n"
    "    self.img = new Image();\n"
    "    if (autoStart) {\n"
    "      self.img.onload = self.start;\n"
    "    }\n"
    "    self.img.src = self.url;\n"
    "    function setRunning(running) {\n"
    "      self.running = running;\n"
    "      if (self.running) {\n"
    "        self.img.src = self.url;\n"
    "        self.frameTimer = setInterval(function() {\n"
    "          if (self.onFrame) {\n"
    "            self.onFrame(self.img);\n"
    "          }\n"
    "        }, self.refreshRate);\n"
    "        if (self.onStart) {\n"
    "          self.onStart();\n"
    "        }\n"
    "      } else {\n"
    "        self.img.src = \"#\";\n"
    "        clearInterval(self.frameTimer);\n"
    "        if (self.onStop) {\n"
    "          self.onStop();\n"
    "        }\n"
    "      }\n"
    "    }\n"
    "    self.start = function() { setRunning(true); }\n"
    "    self.stop = function() { setRunning(false); }\n"
    "  };\n"
    "  Player = function(canvas, url, options) {\n"
    "    var self = this;\n"
    "    if (typeof canvas === \"string\" || canvas instanceof String) {\n"
    "      canvas = document.getElementById(canvas);\n"
    "    }\n"
    "    var context = canvas.getContext(\"2d\");\n"
    "    if (! options) {\n"
    "      options = {};\n"
    "    }\n"
    "    options.url = url;\n"
    "    options.onFrame = updateFrame;\n"
    "    options.onStart = function() { console.log(\"started\"); }\n"
    "    options.onStop = function() { console.log(\"stopped\"); }\n"
    "    self.stream = new Stream(options);\n"
    "    canvas.addEventListener(\"click\", function() {\n"
    "      if (self.stream.running) { self.stop(); }\n"
    "      else { self.start(); }\n"
    "    }, false);\n"
    "    function scaleRect(srcSize, dstSize) {\n"
    "      var ratio = Math.min(dstSize.width / srcSize.width,\n"
    "                           dstSize.height / srcSize.height);\n"
    "      var newRect = {\n"
    "        x: 0, y: 0,\n"
    "        width: srcSize.width * ratio,\n"
    "        height: srcSize.height * ratio\n"
    "      };\n"
    "      newRect.x = (dstSize.width/2) - (newRect.width/2);\n"
    "      newRect.y = (dstSize.height/2) - (newRect.height/2);\n"
    "      return newRect;\n"
    "    }\n"
    "    function updateFrame(img) {\n"
    "      var srcRect = {\n"
    "        x: 0, y: 0,\n"
    "        width: img.naturalWidth,\n"
    "        height: img.naturalHeight\n"
    "      };\n"
    "      var dstRect = scaleRect(srcRect, {\n"
    "        width: canvas.width,\n"
    "        height: canvas.height\n"
    "      });\n"
    "      try {\n"
    "        context.drawImage(img,\n"
    "                          srcRect.x,\n"
    "                          srcRect.y,\n"
    "                          srcRect.width,\n"
    "                          srcRect.height,\n"
    "                          dstRect.x,\n"
    "                          dstRect.y,\n"
    "                          dstRect.width,\n"
    "                          dstRect.height\n"
    "                         );\n"
    "        console.log(\".\");\n"
    "      } catch (e) {\n"
    "        // if we can't draw, don't bother updating anymore\n"
    "        self.stop();\n"
    "        console.log(\"!\");\n"
    "        throw e;\n"
    "      }\n"
    "    }\n"
    "    self.start = function() { self.stream.start(); }\n"
    "    self.stop = function() { self.stream.stop(); }\n"
    "  };\n"
    "  var player = new Player(\"player\", \"/\");\n"
    "  player.start();\n"
    "</script>");

static const QByteArray c_boundaryStringBegin = QByteArrayLiteral(
    "--boundary\r\n" \
    "Content-Type: image/jpeg\r\n" \
    "Content-Length: ");
static const QByteArray c_dataEnd = QByteArrayLiteral("\r\n\r\n");

static const QString c_dconfPath = QStringLiteral("/org/coderus/screencast");

static Cast *s_instance = nullptr;

class Buffer
{
public:
    static Buffer *create(wl_shm *shm, int width, int height, int stride, int format)
    {
        int size = stride * height;

        char filename[] = "/tmp/lipstick-recorder-shm-XXXXXX";
        int fd = mkstemp(filename);
        if (fd < 0) {
            qCWarning(logbuffer) << "creating a buffer file for" << size << "B failed";
            return nullptr;
        }
        int flags = fcntl(fd, F_GETFD);
        if (flags != -1)
            fcntl(fd, F_SETFD, flags | FD_CLOEXEC);

        if (ftruncate(fd, size) < 0) {
            qCWarning(logbuffer) << "ftruncate failed:" << strerror(errno);
            close(fd);
            return nullptr;
        }

        uchar *data = (uchar *)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        unlink(filename);
        if (data == (uchar *)MAP_FAILED) {
            qCWarning(logbuffer) << "mmap failed";
            close(fd);
            return nullptr;
        }

        Buffer *buf = new Buffer;

        wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
        buf->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
        wl_buffer_set_user_data(buf->buffer, buf);
        wl_shm_pool_destroy(pool);
        buf->data = data;
        buf->image = QImage(data, width, height, stride, QImage::Format_RGBA8888);
        close(fd);
        return buf;
    }

    wl_buffer *buffer;
    uchar *data;
    QImage image;
    QPixmap pixmap;
    bool busy = false;
};

Cast::Cast(const Options &options, QObject *parent)
    : QObject(parent)
    , m_orientation(new QOrientationSensor(this))
    , m_options(options)
{
    m_orientation->setActive(true);

    qCDebug(logcast) << "Buffers:" << options.buffers;
    qCDebug(logcast) << "Scale:" << options.scale;
    qCDebug(logcast) << "Smooth:" << options.smooth;
    qCDebug(logcast) << "Quality:" << options.quality;

    s_instance = this;

    MGConfItem *dconf = new MGConfItem(c_dconfPath + QLatin1String("/clients"));
    dconf->set(QStringList());

    QThread *serverThread = new QThread;

    Sender *sender = new Sender(554, m_options);
    connect(serverThread, &QThread::started, sender, &Sender::initialize);
    connect(this, &Cast::sendFrame, sender, &Sender::sendFrame);
    connect(sender, &Sender::lastClientDisconnected, [this]() {
        if (!m_options.daemonize) {
            return;
        }
        handleShutDown();
    });
    connect(sender, &Sender::clientConnected, [dconf](const QString &address) {
        QStringList clients = dconf->value(QStringList()).toStringList();
        clients.append(address);
        dconf->set(clients);
    });
    connect(sender, &Sender::clientDisconnected, [dconf](const QString &address) {
        QStringList clients = dconf->value(QStringList()).toStringList();
        clients.removeAll(address);
        dconf->set(clients);
    });
    sender->moveToThread(serverThread);
    QTimer::singleShot(0, this, [serverThread]() {
        serverThread->start();
    });
}

Cast *Cast::instance()
{
    return s_instance;
}

Cast::~Cast()
{
}

Options Cast::readOptions()
{
    MDConfGroup dconf(c_dconfPath);
    return {
        dconf.value(QStringLiteral("buffers"), 48).toInt(),
        dconf.value(QStringLiteral("scale"), 0.5f).toDouble(),
        dconf.value(QStringLiteral("quality"), 90).toInt(),
        dconf.value(QStringLiteral("smooth"), true).toBool(),
        dconf.value(QStringLiteral("username"), QString()).toString(),
        dconf.value(QStringLiteral("password"), QString()).toString(),
        false, // daemonize
        dconf.value(QStringLiteral("flush"), false).toBool(),
    };
}

void Cast::init()
{
    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    m_display = static_cast<wl_display *>(native->nativeResourceForIntegration("display"));
    m_registry = wl_display_get_registry(m_display);

    static const wl_registry_listener registryListener = {
        global,
        globalRemove
    };
    wl_registry_add_listener(m_registry, &registryListener, this);

    wl_callback *cb = wl_display_sync(m_display);
    static const wl_callback_listener callbackListener = {
        callback
    };
    wl_callback_add_listener(cb, &callbackListener, this);
}

void Cast::start()
{
    if (!m_manager) {
        qFatal("The lipstick_recorder_manager global is not available.");
    }

    QScreen *screen = QGuiApplication::screens().first();
    m_size = screen->size();
    m_size.setWidth(qRound(m_size.width() * m_options.scale));
    m_size.setHeight(qRound(m_size.height() * m_options.scale));

    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    wl_output *output = static_cast<wl_output *>(native->nativeResourceForScreen(QByteArrayLiteral("output"), screen));
    m_recorder = lipstick_recorder_manager_create_recorder(m_manager, output);
    static const lipstick_recorder_listener recorderListener = {
        setup,
        frame,
        failed,
        cancel
    };
    lipstick_recorder_add_listener(m_recorder, &recorderListener, this);
}

void Cast::stop()
{
    lipstick_recorder_destroy(m_recorder);
}

void Cast::handleShutDown()
{
    if (m_shutdown) {
        return;
    }
    m_shutdown = true;
    qCDebug(logcast) << Q_FUNC_INFO;

    stop();
    qGuiApp->sendEvent(qGuiApp, new QEvent(QEvent::Quit));
}

void Cast::recordFrame()
{
    Buffer *buf = nullptr;
    for (Buffer *b : m_buffers) {
        if (!b->busy) {
            buf = b;
            break;
        }
    }
    if (buf) {
        lipstick_recorder_record_frame(m_recorder, buf->buffer);
        wl_display_flush(m_display);
        buf->busy = true;
        m_starving = false;
    } else {
        qCWarning(logcast) << "No free buffers.";
        m_starving = true;
    }
}

void Cast::callback(void *data, wl_callback *cb, uint32_t time)
{
    Q_UNUSED(time)
    wl_callback_destroy(cb);

    Cast *rec = static_cast<Cast *>(data);
    QMutexLocker lock(&rec->m_mutex);

    QTimer::singleShot(0, rec, &Cast::start);
}

void Cast::setup(void *data, lipstick_recorder *recorder, int width, int height, int stride, int format)
{
    Cast *rec = static_cast<Cast *>(data);
    QMutexLocker lock(&rec->m_mutex);

    for (int i = 0; i < rec->m_options.buffers; ++i) {
        Buffer *buffer = Buffer::create(rec->m_shm, width, height, stride, format);
        if (!buffer)
            qFatal("Failed to create a buffer.");
        rec->m_buffers << buffer;
    }
    rec->recordFrame();
}

void Cast::frame(void *data, lipstick_recorder *recorder, wl_buffer *buffer, uint32_t timestamp, int transform)
{
    Q_UNUSED(recorder)

    Cast *rec = static_cast<Cast *>(data);
    if (rec->m_shutdown) {
        return;
    }

    rec->recordFrame();
    static uint32_t time = 0;

    QMutexLocker lock(&rec->m_mutex);
    Buffer *buf = static_cast<Buffer *>(wl_buffer_get_user_data(buffer));
    QImage img = transform == LIPSTICK_RECORDER_TRANSFORM_Y_INVERTED ? buf->image.mirrored(false, true) : buf->image;
    if (rec->m_options.scale != 1.0f) {
        img = img.scaled(rec->m_size, Qt::KeepAspectRatio, rec->m_options.smooth ? Qt::SmoothTransformation : Qt::FastTransformation).convertToFormat(QImage::Format_RGBA8888);
    }
    int rotation = 0;
    switch (rec->m_orientation->reading()->orientation()) {
    case QOrientationReading::TopDown:
        rotation = 180;
        break;
    case QOrientationReading::LeftUp:
        rotation = 90;
        break;
    case QOrientationReading::RightUp:
        rotation = 270;
        break;
    default:
        break;
    }
    buf->pixmap = QPixmap::fromImage(img);;
    QPixmap pix = buf->pixmap;
    int quality = rec->m_options.quality;

    emit rec->sendFrame(pix, quality, rotation);
    buf->busy = false;

    time = timestamp;
}

void Cast::failed(void *data, lipstick_recorder *recorder, int result, wl_buffer *buffer)
{
    Q_UNUSED(data)
    Q_UNUSED(recorder)
    Q_UNUSED(buffer)

    qFatal("Failed to record a frame, result %d.", result);
}

void Cast::cancel(void *data, lipstick_recorder *recorder, wl_buffer *buffer)
{
    Q_UNUSED(recorder)

    Cast *rec = static_cast<Cast *>(data);

    QMutexLocker lock(&rec->m_mutex);
    Buffer *buf = static_cast<Buffer *>(wl_buffer_get_user_data(buffer));
    buf->busy = false;
}

void Cast::global(void *data, wl_registry *registry, uint32_t id, const char *interface, uint32_t version)
{
    Q_UNUSED(registry)

    Cast *rec = static_cast<Cast *>(data);
    if (strcmp(interface, "lipstick_recorder_manager") == 0) {
        rec->m_manager = static_cast<lipstick_recorder_manager *>(wl_registry_bind(registry, id, &lipstick_recorder_manager_interface, qMin(version, 1u)));
    } else if (strcmp(interface, "wl_shm") == 0) {
        rec->m_shm = static_cast<wl_shm *>(wl_registry_bind(registry, id, &wl_shm_interface, qMin(version, 1u)));
    }
}

void Cast::globalRemove(void *data, wl_registry *registry, uint32_t id)
{
    Q_UNUSED(data)
    Q_UNUSED(registry)
    Q_UNUSED(id)
}

Sender::Sender(int port, Options options)
    : m_port(port)
    , m_options(options)
{

}

void Sender::initialize()
{
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &Sender::handleConnection);
    connect(m_server, &QTcpServer::acceptError, [](QAbstractSocket::SocketError socketError) {
        qCDebug(logcast) << Q_FUNC_INFO << socketError;
    });

    int sd_fds = sd_listen_fds(1);
    if (sd_fds){
        for (int i = SD_LISTEN_FDS_START; i <= (SD_LISTEN_FDS_START + sd_fds - 1); i++){
            if (sd_is_socket(i, AF_INET6, SOCK_STREAM, 1)
                || sd_is_socket(i, AF_INET, SOCK_STREAM, 1)){
                qCDebug(logcast) << Q_FUNC_INFO << "using given socket at FD:" << i;
                m_server->setSocketDescriptor(i);
                if (m_server->waitForNewConnection(1000)) {
                    break;
                } else {
                    m_server->close();
                }
            }
        }
    }

    if (!m_server->isListening()) {
        emit lastClientDisconnected();
    }

    if (m_server->socketDescriptor() < 0 && !m_server->listen(QHostAddress::Any, m_port)) {
        qCWarning(logcast) << Q_FUNC_INFO << m_server->errorString();
        emit lastClientDisconnected();
    }
}

void Sender::sendLastFrame(QTcpSocket *client)
{
    qDebug() << Q_FUNC_INFO << m_lastFrame.length();
    if (m_lastFrame.length() == 0) {
        return;
    }
    client->write(c_boundaryStringBegin);
    client->write(QByteArray::number(m_lastFrame.length()));
    client->write(c_dataEnd);
    client->write(m_lastFrame);
    client->flush();
}

void Sender::sendFrame(const QPixmap &image, int quality, int rotation)
{
    if (m_clients.isEmpty()) {
        return;
    }

    QPixmap transformed = image;
    if (rotation != 0) {
        const QPoint center = image.rect().center();
        QMatrix matrix;
        matrix.translate(center.x(), center.y());
        matrix.rotate(rotation);
        transformed = image.transformed(matrix);
    }

    m_lastFrame.resize(0);
    QBuffer buffer(&m_lastFrame);
    buffer.open(QIODevice::WriteOnly);
    transformed.save(&buffer, "JPG", quality);

    for (QTcpSocket *client : m_clients) {
        if (!client->isOpen() || !client->isWritable()) {
            qCWarning(logcast) << Q_FUNC_INFO << client << client->peerAddress()
                               << "client isOpen:" << client->isOpen()
                               << "client isWritable:" << client->isWritable();
        }

        client->write(c_boundaryStringBegin);
        client->write(QByteArray::number(m_lastFrame.length()));
        client->write(c_dataEnd);
        client->write(m_lastFrame);
        if (m_options.flush) {
            client->flush();
        }
    }
}

void Sender::handleConnection()
{
    QTcpSocket *client = m_server->nextPendingConnection();
    qCDebug(logcast) << Q_FUNC_INFO << client << client->peerAddress() << client->peerPort();
    qCDebug(logcast) << Q_FUNC_INFO
                     << "client isOpen:" << client->isOpen()
                     << "client isWritable:" << client->isWritable();
    connect(client, &QTcpSocket::readyRead, this, &Sender::connectionReadyRead);
    connect(client, &QTcpSocket::disconnected, this, &Sender::connectionClosed);
    connect(client, &QTcpSocket::stateChanged, [client](QAbstractSocket::SocketState socketState) {
        qCWarning(logcast) << Q_FUNC_INFO << client << client->peerAddress() << socketState;
    });
    connect(client, static_cast<void (QTcpSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error), [client](QAbstractSocket::SocketError socketError) {
        qCWarning(logcast) << Q_FUNC_INFO << client << client->peerAddress() << socketError;
    });
}

void Sender::connectionReadyRead()
{
    QTcpSocket *client = qobject_cast<QTcpSocket*>(sender());
    qCDebug(logcast) << Q_FUNC_INFO << client << client->peerAddress();

    const QByteArray data = client->readAll();
    qCDebug(logcast).noquote() << data;
    const QList<QByteArray> requestLines = data.split('\n');

    const QString httpLine = QString::fromLatin1(requestLines.first()).trimmed();

    const QStringList request = httpLine.split(QChar(u' '));
    const QString method = request.first();
    if (method != QLatin1String("GET")) {
        client->write(c_notFoundResponse);
        return;
    }
    if (request.size() != 3) {
        client->write(c_notFoundResponse);
        return;
    }
    const QString url = request.at(1);
    if (url == QLatin1String("/player")) {
        const QSize screenSize = QGuiApplication::screens().first()->size();

        client->write(c_htmlPage);
        QByteArray data = c_playerPage;
        data = data.replace("#WIDTH#", QByteArray::number(screenSize.width() * m_options.scale));
        data = data.replace("#HEIGHT#", QByteArray::number(screenSize.height() * m_options.scale));
        client->write(QByteArray::number(data.size()));
        client->write(c_dataEnd);
        client->write(data);
        return;
    }

    if (url == QLatin1String("/screenshot")) {
        client->write(c_singleImage);
        client->write(QByteArray::number(m_lastFrame.size()));
        client->write(c_dataEnd);
        client->write(m_lastFrame);
        return;
    }

    if (url != QLatin1String("/")) {
        client->write(c_notFoundResponse);
        return;
    }

    QString authorization;
    for (const QByteArray &requestLine : requestLines) {
        const QString &token = QString::fromLatin1(requestLine).trimmed();

        if (token.startsWith(QLatin1String("Authorization:"))) {
            const QStringList auth = token.split(QChar(u' '));
            if (auth.count() != 3) {
                continue;
            }
            if (auth.at(1) != QLatin1String("Basic")) {
                continue;
            }

            authorization = auth.last();
        }
    }

    if (!m_options.username.isEmpty() && !m_options.password.isEmpty()) {
        if (authorization.isEmpty()) {
            client->write(c_authResponse);
            return;
        } else {
            const QByteArray expectedAuth =
                    QStringLiteral("%1:%2")
                    .arg(m_options.username, m_options.password)
                    .toLatin1()
                    .toBase64(QByteArray::Base64Encoding | QByteArray::KeepTrailingEquals);
            if (expectedAuth != authorization.toLatin1()) {
                qDebug() << expectedAuth;
                client->write(c_authResponse);
                return;
            } else {
                qDebug() << "Successful auth";
            }
        }
    }

    client->write(c_contentType);
    m_clients.append(client);
    sendLastFrame(client);
    sendLastFrame(client);
    emit clientConnected(QStringLiteral("%1:%2")
                         .arg(client->peerAddress().toString())
                         .arg(client->peerPort()));
}

void Sender::connectionClosed()
{
    QTcpSocket *client = qobject_cast<QTcpSocket*>(sender());
    qCDebug(logcast) << Q_FUNC_INFO << client << client->peerAddress()
                     << client->errorString() << m_server->errorString();
    m_clients.removeAll(client);
    emit clientDisconnected(QStringLiteral("%1:%2")
                            .arg(client->peerAddress().toString())
                            .arg(client->peerPort()));

    if (m_clients.isEmpty()) {
        emit lastClientDisconnected();
    }
}
