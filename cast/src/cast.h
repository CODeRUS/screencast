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

#ifndef LIPSTICKRECORDER_SCREENCAST_H
#define LIPSTICKRECORDER_SCREENCAST_H

#include <QObject>
#include <wayland-client.h>
#include <QPixmap>
#include <QMutex>

struct Options {
    int buffers;
    double scale;
    int quality;
    bool smooth;
    QString username;
    QString password;
    bool daemonize;
};

class QTcpServer;
class QTcpSocket;
class Sender : public QObject
{
    Q_OBJECT
public:
    Sender(int port, Options options);

public slots:
    void initialize();
    void sendFrame(const QPixmap &image, int quality);

signals:
    void clientConnected(const QString &address);
    void clientDisconnected(const QString &address);
    void lastClientDisconnected();

private slots:
    void handleConnection();
    void connectionReadyRead();
    void connectionClosed();

private:
    QTcpServer *m_server = nullptr;
    QList<QTcpSocket*> m_clients;
    int m_port = 0;
    Options m_options;
};

struct wl_display;
struct wl_registry;
struct lipstick_recorder_manager;
struct lipstick_recorder;

class Buffer;
class Cast : public QObject
{
    Q_OBJECT
public:
    explicit Cast(const Options &options, QObject *parent = nullptr);
    static Cast *instance();
    virtual ~Cast();

    static Options readOptions();

signals:
    void sendFrame(const QPixmap &image, int quality);

public slots:
    void init();
    void start();
    void stop();
    void handleShutDown();

private slots:
    void recordFrame();

private:
    static void global(void *data, wl_registry *registry, uint32_t id, const char *interface, uint32_t version);
    static void globalRemove(void *data, wl_registry *registry, uint32_t id);
    static void callback(void *data, wl_callback *cb, uint32_t time);
    static void setup(void *data, lipstick_recorder *recorder, int width, int height, int stride, int format);
    static void frame(void *data, lipstick_recorder *recorder, wl_buffer *buffer, uint32_t time, int transform);
    static void failed(void *data, lipstick_recorder *recorder, int result, wl_buffer *buffer);
    static void cancel(void *data, lipstick_recorder *recorder, wl_buffer *buffer);

    wl_display *m_display = nullptr;
    wl_registry *m_registry = nullptr;
    wl_shm *m_shm = nullptr;
    lipstick_recorder_manager *m_manager = nullptr;
    lipstick_recorder *m_recorder = nullptr;
    QSize m_size;
    QList<Buffer *> m_buffers;
    Buffer *m_lastFrame = nullptr;
    bool m_starving = false;
    QMutex m_mutex;

    bool m_shutdown = false;

    Options m_options;
};

#endif
