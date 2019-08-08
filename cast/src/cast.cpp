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
#include <QBuffer>
#include <QTcpServer>
#include <QTcpSocket>

#include <systemd/sd-daemon.h>

#include "wayland-lipstick-recorder-client-protocol.h"
#include "cast.h"

Q_LOGGING_CATEGORY(logcast, "screencast.cast", QtDebugMsg)
Q_LOGGING_CATEGORY(logbuffer, "screencast.cast.buffer", QtDebugMsg)

static const QByteArray c_contentType = QByteArrayLiteral(
    "HTTP/1.0 200 OK\r\n"
    "Cache-Control: no-cache\r\n"
    "Cache-Control: private\r\n"
    "Cache-Control: no-store\r\n"
    "Pragma: no-cache\r\n"
    "Content-Type: multipart/x-mixed-replace;boundary=--boundary\r\n\r\n");

static const QByteArray c_boundaryStringBegin = QByteArrayLiteral("--boundary\r\n" \
                             "Content-Type: image/jpeg\r\n" \
                             "Content-Length: ");
static const QByteArray c_boundaryStringEnd = QByteArrayLiteral("\r\n\r\n");

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
    , m_options(options)
{
    qCDebug(logcast) << "Buffers:" << options.buffers;
    qCDebug(logcast) << "Scale:" << options.scale;
    qCDebug(logcast) << "Smooth:" << options.smooth;
    qCDebug(logcast) << "Quality:" << options.quality;

    m_screen = QGuiApplication::screens().first();

    s_instance = this;

    QThread *serverThread = new QThread;

    Sender *sender = new Sender(554);
    connect(serverThread, &QThread::started, sender, &Sender::initialize);
    connect(this, &Cast::sendFrame, sender, &Sender::sendFrame);
    connect(sender, &Sender::lastClientDisconnected, [this]() {
        if (!m_options.daemonize) {
            return;
        }
        handleShutDown();
    });
    sender->moveToThread(serverThread);
    serverThread->start();
}

Cast *Cast::instance()
{
    return s_instance;
}

Cast::~Cast()
{
}

Cast::Options Cast::readOptions()
{
    MDConfGroup dconf(QStringLiteral("/org/coderus/screencast"));
    return {
        dconf.value(QStringLiteral("buffers"), 48).toInt(),
        dconf.value(QStringLiteral("scale"), 0.5f).toDouble(),
        dconf.value(QStringLiteral("quality"), 90).toInt(),
        dconf.value(QStringLiteral("smooth"), true).toBool(),
        false,
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

    m_size = m_screen->size();
    m_size.setWidth(qRound(m_size.width() * m_options.scale));
    m_size.setHeight(qRound(m_size.height() * m_options.scale));

    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    wl_output *output = static_cast<wl_output *>(native->nativeResourceForScreen(QByteArrayLiteral("output"), m_screen));
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
    buf->pixmap = QPixmap::fromImage(img);;
    QPixmap pix = buf->pixmap;
    int quality = rec->m_options.quality;

    emit rec->sendFrame(pix, quality);
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

Sender::Sender(int port)
    : m_port(port)
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
            }
        }
    }

    if (m_server->socketDescriptor() < 0 && !m_server->listen(QHostAddress::Any, m_port)) {
        qCWarning(logcast) << Q_FUNC_INFO << m_server->errorString();
        emit lastClientDisconnected();
    }
}

void Sender::sendFrame(const QPixmap &image, int quality)
{
    if (m_clients.isEmpty()) {
        return;
    }

    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPG", quality);

    for (QTcpSocket *client : m_clients) {
        if (!client->isOpen() || !client->isWritable()) {
            qCWarning(logcast) << Q_FUNC_INFO << client << client->peerAddress()
                               << client->isOpen() << client->isWritable();
        }

        client->write(c_boundaryStringBegin);
        client->write(QByteArray::number(ba.length()));
        client->write(c_boundaryStringEnd);
        client->write(ba);
    }
}

void Sender::handleConnection()
{
    QTcpSocket *client = m_server->nextPendingConnection();
    qCDebug(logcast) << Q_FUNC_INFO << client << client->peerAddress() << client->peerPort();
    qCDebug(logcast) << Q_FUNC_INFO << client->isOpen() << client->isWritable();
    connect(client, &QTcpSocket::readyRead, this, &Sender::connectionReadyRead);
    connect(client, &QTcpSocket::disconnected, this, &Sender::connectionClosed);
    connect(client, &QTcpSocket::stateChanged, [client](QAbstractSocket::SocketState socketState) {
        qCWarning(logcast) << Q_FUNC_INFO << client << client->peerAddress() << socketState;
    });
    connect(client, static_cast<void (QTcpSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error), [client](QAbstractSocket::SocketError socketError) {
        qCWarning(logcast) << Q_FUNC_INFO << client << client->peerAddress() << socketError;
    });
    m_clients.append(client);
}

void Sender::connectionReadyRead()
{
    QTcpSocket *client = qobject_cast<QTcpSocket*>(sender());
    qCDebug(logcast) << Q_FUNC_INFO << client << client->peerAddress();
    qCDebug(logcast).noquote() << client->readAll();
    client->write(c_contentType);
}

void Sender::connectionClosed()
{
    QTcpSocket *client = qobject_cast<QTcpSocket*>(sender());
    qCDebug(logcast) << Q_FUNC_INFO << client << client->peerAddress()
                     << client->errorString() << m_server->errorString();
    m_clients.removeAll(client);

    if (m_clients.isEmpty()) {
        emit lastClientDisconnected();
    }
}
