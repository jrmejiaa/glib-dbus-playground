#include "supervisor.hpp"

#include <cstdio>
#include <cstdarg>

#include <algorithm>

#define LOG_ENTER() log(__PRETTY_FUNCTION__)

/*
 * The Manager is published under the same well-known name and object path on
 * both the system and the user (session) bus; only the bus type and the objectPath
 * install location differ. See ADR 0001 for why the PoC targets the user
 * instance.
 */
constexpr GBusType    BUS_TYPE    = G_BUS_TYPE_SESSION;
constexpr const char *BUS_NAME    = "org.freedesktop.systemd1";
constexpr const char *OBJECT_PATH = "/org/freedesktop/systemd1";

/**
 * @brief Job mode: enqueue the job, displacing any conflicting queued job.
 *
 * The valid modes are a systemd documentation contract, not something the D-Bus
 * introspection XML encodes.
 */
constexpr const char *JOB_MODE = "replace";

/** @brief JobRemoved result string denoting success. */
constexpr const char *RESULT_DONE = "done";

Supervisor::Supervisor(GMainLoop *pLoop, std::set<std::string> services)
    : m_pLoop(pLoop),
      m_Services(services),
      m_ExitCode(0)
{
    LOG_ENTER();
}

Supervisor::~Supervisor()
{
    LOG_ENTER();
};

void Supervisor::run()
{
    LOG_ENTER();
    log("  - Getting DBusConnection async on the %s bus...", BUS_TYPE == G_BUS_TYPE_SESSION ? "session (user)" : "system");
    g_bus_get(BUS_TYPE, nullptr, &Supervisor::onGDbusConnectionReady, this);
}

void Supervisor::terminate()
{
    LOG_ENTER();
    log("  - Shutting down alarm server, getting signal to close");
    g_main_loop_quit(m_pLoop);
}

void Supervisor::finish(const std::string &context, GError *pError)
{
    LOG_ENTER();
    if (pError)
    {
        std::fprintf(stderr, "ERROR during %s: pError: (code=%i, message=%s)\n", context.c_str(), pError->code, pError->message);
        g_error_free(pError);
        m_ExitCode = 1;
    }
    terminate();
}

void Supervisor::log(const char *pFormat, ...)
{
    std::fprintf(stdout, "  [supervisor] ");

    std::va_list args;
    va_start(args, pFormat);
    std::vfprintf(stdout, pFormat, args);
    va_end(args);

    std::fprintf(stdout, "\n");
    std::fflush(stdout);
}

/* static */
void Supervisor::onGDbusConnectionReady(GObject *source_object, GAsyncResult *res, gpointer userData)
{
    LOG_ENTER();
    log("  - Getting connection callback with pointer %p", userData);
    auto *self = static_cast<Supervisor *>(userData);

    GError *pError      = nullptr;
    self->m_pConnection = g_bus_get_finish(res, &pError);

    if (self->m_pConnection)
    {
        const char *pBusStr = BUS_TYPE == G_BUS_TYPE_SESSION ? "session (user)" : "system";
        log("  - Connection to %s successful, asking for proxy obj", pBusStr);
        gdbus_systemd1_manager_proxy_new(
            self->m_pConnection, G_DBUS_PROXY_FLAGS_NONE, BUS_NAME, OBJECT_PATH, nullptr, &Supervisor::onDbusProxyReady, self);
    }
    else
    {
        self->finish("onGDbusConnectionReady", pError);
    }
}

/* static */
void Supervisor::onDbusProxyReady(GObject * /*source*/, GAsyncResult *res, gpointer userData)
{
    LOG_ENTER();
    auto   *self   = static_cast<Supervisor *>(userData);
    GError *pError = nullptr;

    // finish() returns the interface-typed pointer to the concrete proxy GObject.
    self->m_pManager = gdbus_systemd1_manager_proxy_new_finish(res, &pError);
    if (self->m_pManager == nullptr)
    {
        self->finish("onDbusProxyReady", pError);
        return;
    }

    log("  - Proxy ready. Subscribing to signals...");
    gdbus_systemd1_manager_call_subscribe(self->m_pManager, nullptr, &Supervisor::onSubscribeDone, self);
}

/* static */
void Supervisor::onSubscribeDone(GObject * /*source*/, GAsyncResult *res, gpointer userData)
{
    LOG_ENTER();
    auto   *self   = static_cast<Supervisor *>(userData);
    GError *pError = nullptr;

    if (!gdbus_systemd1_manager_call_subscribe_finish(self->m_pManager, res, &pError))
    {
        self->finish("onSubscribeDone", pError);
        return;
    }

    log("  - Attaching signals to UnitNew/UnitRemoved");
    g_signal_connect(self->m_pManager, "unit-new", G_CALLBACK(&Supervisor::onUnitNew), self);
    g_signal_connect(self->m_pManager, "unit-removed", G_CALLBACK(&Supervisor::onUnitRemoved), self);

    log("  - Starting services of PoC");
    for (const auto service : self->m_Services)
    {
        /* Calling and forget, waiting callback once the Unit new is reach. Not the best idea on production code
           I just want to avoid the traceability made with the Jobs (made already on step-2) */
        gdbus_systemd1_manager_call_start_unit(self->m_pManager, service.c_str(), JOB_MODE, nullptr, &Supervisor::onStartUnit, self);
    }
}

/* static */
void Supervisor::onStartUnit(GObject *source, GAsyncResult *res, gpointer userData)
{
    LOG_ENTER();
    auto   *self    = static_cast<Supervisor *>(userData);
    gchar  *jobPath = nullptr;
    GError *pError  = nullptr;

    bool success = gdbus_systemd1_manager_call_start_unit_finish(self->m_pManager, &jobPath, res, &pError);
    if (success)
    {
        log("  - a job has been enqueued to start the unit on '%s'", jobPath);
    }
    else
    {
        self->finish("onStartUnit, job has not been made for startUnit", pError);
    }
}

/* static */
void Supervisor::onUnitNew(GDbusSystemd1Manager *object, const gchar *unit, const gchar *objectPath, gpointer userData)
{
    LOG_ENTER();
    auto   *self   = static_cast<Supervisor *>(userData);
    GError *pError = nullptr;
    if (!self->m_Services.contains(unit))
    {
        log("  - Ignoring unit '%s' not in services to supervised", unit);
        return;
    }

    if (self->m_UnitsByService.contains(unit))
    {
        log("  - Ignoring unit '%s', already exists on the map (likely burst systemdctl operation)", unit);
        return;
    }

    log("  - Unit='%s' detected on callback. Getting Unit and Service proxys to create UnitMonitor object", unit);
    GDbusSystemd1Unit *pUnit =
        gdbus_systemd1_unit_proxy_new_sync(self->m_pConnection, G_DBUS_PROXY_FLAGS_NONE, BUS_NAME, objectPath, nullptr, &pError);
    if (!pUnit)
    {
        self->finish("onUnitNew: Unit proxy fails", pError);
        return;
    }

    GDbusSystemd1Service *pService =
        gdbus_systemd1_service_proxy_new_sync(self->m_pConnection, G_DBUS_PROXY_FLAGS_NONE, BUS_NAME, objectPath, nullptr, &pError);
    if (!pService)
    {
        self->finish("onUnitNew: Service proxy fails", pError);
        return;
    }

    log("  - Creating UnitMonitor object, after sync proxys succeeded", unit);
    std::shared_ptr<UnitMonitor> pUnitMonitor = std::make_shared<UnitMonitor>(pUnit, pService, unit, objectPath);
    self->m_UnitsByService.insert(std::make_pair(unit, std::move(pUnitMonitor)));
}

/* static */
void Supervisor::onUnitRemoved(GDbusSystemd1Manager *object, const gchar *unit, const gchar *objectPath, gpointer userData)
{
    LOG_ENTER();
    auto *self = static_cast<Supervisor *>(userData);

    auto it = self->m_UnitsByService.find(unit);
    if (it != self->m_UnitsByService.end())
    {
        log("  - UnitMonitor object for unit '%s' present, erasing object", unit, objectPath);
        self->m_UnitsByService.erase(it);
    }
    else
    {
        log("  - UnitMonitor object is not present for unit '%s', ignoring callback", unit, objectPath);
    }
}