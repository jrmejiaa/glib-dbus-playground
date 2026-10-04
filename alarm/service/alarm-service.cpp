#include "alarm-service.hpp"

/*
 * The Alarm interface is published on the session (user) bus. See ADR 0001 for
 * why the PoC targets the user instance rather than the system bus.
 */
constexpr GBusType BUS_TYPE = G_BUS_TYPE_SESSION;
/** @brief Well-known name the service claims and exports the Alarm interface under. */
constexpr const char *BUS_NAME = "es.aleksander.Alarm";
/** @brief Object path the Alarm skeleton is exported at. */
constexpr const char *OBJECT_PATH = "/es/aleksander/Alarm";

AlarmDbusServer::AlarmDbusServer(GMainLoop *pLoop)
    : m_pLoop(pLoop),
      m_pAlarm(nullptr),
      m_ExitCode(0)
{
    std::fprintf(stdout, "  Constructing AlarmDBusServer object with GMainLoop %p\n", pLoop);
}

AlarmDbusServer::~AlarmDbusServer()
{
    std::fprintf(stdout, "  Destructing AlarmDBusServer object with Alarm skeleton %p\n", m_pAlarm);
    if (m_pAlarm)
    {
        g_object_unref(m_pAlarm);
    }
}

void AlarmDbusServer::run()
{
    std::fprintf(stdout, "    Connecting to %s on the %s bus...\n", BUS_NAME, BUS_TYPE == G_BUS_TYPE_SESSION ? "session (user)" : "system");

    /*
     * Asynchronously claim the well-known name. Once the bus grants it,
     * onNameAcquired runs on this thread's main context and creates/exports the
     * skeleton. `this` travels as the user-data pointer.
     */
    g_bus_own_name(G_BUS_TYPE_SESSION, BUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE, nullptr, &AlarmDbusServer::onNameAcquired, nullptr, this, nullptr);
}

void AlarmDbusServer::terminate()
{
    std::fprintf(stdout, "    Shutting down alarm server, getting signal to close\n");
    g_main_loop_quit(m_pLoop);
}

/* static */
void AlarmDbusServer::onNameAcquired(GDBusConnection *connection, const gchar *name, gpointer userData)
{
    std::fprintf(stdout, "    Named %s acquired. Getting skeleton of Alarm\n", name);
    auto *self = static_cast<AlarmDbusServer *>(userData);

    GDbusAlarm *skeleton = nullptr;

    skeleton       = gdbus_alarm_skeleton_new();
    self->m_pAlarm = skeleton;

    g_signal_connect(skeleton, "handle-configure", G_CALLBACK(&AlarmDbusServer::onHandleConfigured), self);
    g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(skeleton), connection, OBJECT_PATH, nullptr);
}

/* static */
int AlarmDbusServer::onHandleConfigured(GDbusAlarm *skeleton, GDBusMethodInvocation *invocation, guint seconds, gpointer userData)
{
    std::fprintf(stdout, "    Configure method fired! Answering request at service: %p\n", userData);
    auto *self = static_cast<AlarmDbusServer *>(userData);

    if (gdbus_alarm_get_activated(self->m_pAlarm))
    {
        g_dbus_method_invocation_return_error_literal(invocation, G_IO_ERROR, G_IO_ERROR_EXISTS, "Exists");
        return G_DBUS_METHOD_INVOCATION_HANDLED;
    }
    gdbus_alarm_set_activated(self->m_pAlarm, true);
    g_timeout_add_seconds(seconds, &AlarmDbusServer::emitAlarmCallback, self);
    gdbus_alarm_complete_configure(self->m_pAlarm, invocation);
    return G_DBUS_METHOD_INVOCATION_HANDLED;
}

/* static */
int AlarmDbusServer::emitAlarmCallback(gpointer userData)
{
    std::fprintf(stdout, "    Timeout on Alarm, emitting signal at service: %p\n", userData);
    auto *self = static_cast<AlarmDbusServer *>(userData);

    gdbus_alarm_emit_beeeeeeeeep(GDBUS_ALARM(self->m_pAlarm));
    gdbus_alarm_set_activated(GDBUS_ALARM(self->m_pAlarm), false);

    return G_SOURCE_REMOVE;
}