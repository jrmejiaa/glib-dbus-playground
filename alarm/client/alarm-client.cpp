#include "alarm-client.hpp"

/** @brief Bus the Alarm interface is reached on; the session (user) bus (see ADR 0001). */
constexpr GBusType BUS_TYPE = G_BUS_TYPE_SESSION;
/** @brief Well-known name the Alarm service owns and the client calls. */
constexpr const char *BUS_NAME = "es.aleksander.Alarm";
/** @brief Object path the Alarm interface is exported at. */
constexpr const char *OBJECT_PATH = "/es/aleksander/Alarm";

/** @brief Delay, in seconds, requested in the Configure call before the alarm beeps. */
constexpr const int SECONDS_FOR_ALARM = 5;

AlarmDbusClient::AlarmDbusClient(GMainLoop *pLoop)
    : m_pLoop(pLoop),
      m_pConnection(nullptr),
      m_pAlarm(nullptr),
      m_ExitCode(0)
{
    std::fprintf(stdout, "  Constructing AlarmDBusClient object with GMainLoop %p\n", pLoop);
}

AlarmDbusClient::~AlarmDbusClient()
{
    std::fprintf(stdout, "  Destructing AlarmDBusClient object with Alarm skeleton %p\n", m_pAlarm);
    if (m_pAlarm)
    {
        g_object_unref(m_pAlarm);
    }
}

void AlarmDbusClient::run()
{
    std::fprintf(stdout, "    Connecting to %s on the %s bus...\n", BUS_NAME, BUS_TYPE == G_BUS_TYPE_SESSION ? "session (user)" : "system");
    g_bus_get(BUS_TYPE, nullptr, &AlarmDbusClient::onGDbusConnectionReady, this);
}

void AlarmDbusClient::terminate()
{
    std::fprintf(stdout, "    Shutting down alarm server, getting signal to close\n");
    g_main_loop_quit(m_pLoop);
}

/* static */
void AlarmDbusClient::onGDbusConnectionReady(GObject *source_object, GAsyncResult *res, gpointer userData)
{
    std::fprintf(stdout, "    Getting connection callback with pointer %p\n", userData);
    auto *self = static_cast<AlarmDbusClient *>(userData);

    GError *pError      = nullptr;
    self->m_pConnection = g_bus_get_finish(res, &pError);

    if (self->m_pConnection)
    {
        const char *pBusStr = BUS_TYPE == G_BUS_TYPE_SESSION ? "session (user)" : "system";
        std::fprintf(stdout, "     - Connection to %s successful, asking for proxy obj\n", pBusStr);
        gdbus_alarm_proxy_new(self->m_pConnection, G_DBUS_PROXY_FLAGS_NONE, BUS_NAME, OBJECT_PATH, nullptr, &AlarmDbusClient::onDbusProxyReady, self);
    }
    else
    {
        std::fprintf(stdout, "     - Unable to get dbus-connection error: (code=%i, message=%s)\n", pError->code, pError->message);
        self->terminate();
        g_error_free(pError);
    }
}

/* static */
void AlarmDbusClient::onDbusProxyReady(GObject *source_object, GAsyncResult *res, gpointer userData)
{
    std::fprintf(stdout, "    Getting proxy callback with pointer %p\n", userData);
    auto *self = static_cast<AlarmDbusClient *>(userData);

    GError *pError = nullptr;
    self->m_pAlarm = gdbus_alarm_proxy_new_finish(res, &pError);

    if (self->m_pAlarm)
    {
        std::fprintf(stdout, "     - Proxy object created at %p, connecting to signals and sending configure request\n", self->m_pAlarm);
        g_signal_connect(self->m_pAlarm, "beeeeeeeeep", G_CALLBACK(&AlarmDbusClient::onBeep), self);
        gdbus_alarm_call_configure(self->m_pAlarm, SECONDS_FOR_ALARM, nullptr, &AlarmDbusClient::onConfiguredReady, self);
    }
    else
    {
        std::fprintf(stdout, "     - Unable to get proxy error: (code=%i, message=%s)\n", pError->code, pError->message);
        self->terminate();
        g_error_free(pError);
    }
}

/* static */
void AlarmDbusClient::onConfiguredReady(GObject *source_object, GAsyncResult *res, gpointer userData)
{
    std::fprintf(stdout, "    Getting on ConfiguredReady callback with pointer %p\n", userData);
    auto *self = static_cast<AlarmDbusClient *>(userData);

    GError *pError  = nullptr;
    bool    success = gdbus_alarm_call_configure_finish(self->m_pAlarm, res, &pError);
    if (success)
    {
        std::fprintf(stdout, "     - The operation was successful, waiting for beep signal...\n");
    }
    else
    {
        std::fprintf(stdout, "     - Unable to start configure operation error: (code=%i, message=%s)\n", pError->code, pError->message);
        self->terminate();
        g_error_free(pError);
    }
}

/* static */
void AlarmDbusClient::onBeep(GDbusAlarm *alarm, gpointer userData)
{
    std::fprintf(stdout, "    Getting beeep signal, we made it!\n");
    auto *self = static_cast<AlarmDbusClient *>(userData);
    self->terminate();
}