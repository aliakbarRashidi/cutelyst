/*
 * Copyright (C) 2013-2014 Daniel Nicoletti <dantti12@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB. If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "engineuwsgi.h"
#include "plugin.h"

#include <QCoreApplication>

using namespace Cutelyst;

static EngineUwsgi *engine;

void cuteOutput(QtMsgType, const QMessageLogContext &, const QString &);

/**
 * This function is called as soon as
 * the plugin is loaded
 */
extern "C" void uwsgi_cutelyst_on_load() {
    // This allows for some stuff to run event loops
    (void) new QCoreApplication(uwsgi.argc, uwsgi.argv);

    qInstallMessageHandler(cuteOutput);
}

extern "C" int uwsgi_cutelyst_init()
{
    qCDebug(CUTELYST_UWSGI) << "Initializing Cutelyst plugin";

    return 0;
}

extern "C" void uwsgi_cutelyst_post_fork()
{
    if (!engine->postFork()) {
        qCCritical(CUTELYST_UWSGI) << "Could not setup application on post fork";

#ifdef UWSGI_GO_CHEAP_CODE
        // We need to tell the master process that the
        // application failed to setup and that it shouldn't
        // try to respawn the worker
        exit(UWSGI_GO_CHEAP_CODE);
#endif // UWSGI_GO_CHEAP_CODE
    }
}

extern "C" int uwsgi_cutelyst_request(struct wsgi_request *wsgi_req)
{
    // empty request ?
    if (!wsgi_req->uh->pktsize) {
        qCDebug(CUTELYST_UWSGI) << "Invalid request. skip.";
        goto clear;
    }

    // get uwsgi variables
    if (uwsgi_parse_vars(wsgi_req)) {
        qCDebug(CUTELYST_UWSGI) << "Invalid request. skip.";
        goto clear;
    }

    engine->processRequest(wsgi_req);

clear:
    return UWSGI_OK;
}

#ifdef UWSGI_GO_CHEAP_CODE // Actually we only need uwsgi 2.0.1
static void fsmon_reload(struct uwsgi_fsmon *fs)
{
    qCDebug(CUTELYST_UWSGI) << "Reloading application due to file change";
    uwsgi_reload(uwsgi.argv);
}
#endif // UWSGI_GO_CHEAP_CODE

/**
 * This function is called when the master process is exiting
 */
extern "C" void uwsgi_cutelyst_master_cleanup()
{
    qCDebug(CUTELYST_UWSGI) << "Master process finishing" << QCoreApplication::applicationPid();
    delete qApp;
    qCDebug(CUTELYST_UWSGI) << "Master process finished" << QCoreApplication::applicationPid();
}

/**
 * This function is called when the child process is exiting
 */
extern "C" void uwsgi_cutelyst_atexit()
{
    qCDebug(CUTELYST_UWSGI) << "Child process finishing" << QCoreApplication::applicationPid();
    delete engine;
    qCDebug(CUTELYST_UWSGI) << "Child process finished" << QCoreApplication::applicationPid();
}

extern "C" void uwsgi_cutelyst_init_apps()
{
    qCDebug(CUTELYST_UWSGI) << "Cutelyst Init App";

    QString path(options.app);
    if (path.isEmpty()) {
        qCCritical(CUTELYST_UWSGI) << "Cutelyst Application name or path was not set";
        return;
    }

#ifdef UWSGI_GO_CHEAP_CODE
    if (options.reload) {
        // Register application auto reload
        char *file = qstrdup(path.toUtf8().constData());
        uwsgi_register_fsmon(file, fsmon_reload, NULL);
    }
#endif // UWSGI_GO_CHEAP_CODE

    QString config(options.config);
    if (!config.isNull()) {
        qputenv("CUTELYST_CONFIG", config.toUtf8());
    }

    engine = new EngineUwsgi(qApp);

    qCDebug(CUTELYST_UWSGI) << "Loading" << path;
    if (!engine->loadApplication(path)) {
        qCCritical(CUTELYST_UWSGI) << "Could not load application:" << path;
        return;
    }

    // register a new app under a specific "mountpoint"
    uwsgi_add_app(1, CUTELYST_MODIFIER1, NULL, 0, NULL, NULL);
}

void cuteOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QByteArray localMsg = msg.toLocal8Bit();
    switch (type) {
    case QtDebugMsg:
        uwsgi_log("%s[debug] %s\n", context.category, localMsg.constData());
        break;
    case QtWarningMsg:
        uwsgi_log("%s[warn] %s\n", context.category, localMsg.constData());
        break;
    case QtCriticalMsg:
        uwsgi_log("%s[crit] %s\n", context.category, localMsg.constData());
        break;
    case QtFatalMsg:
        uwsgi_log("%s[fatal] %s\n", context.category, localMsg.constData());
        abort();
    }
}
