#include "supervisor.hpp"

#include <cstdio>
#include <cstdarg>

#include <algorithm>

#define LOG_ENTER() log(__PRETTY_FUNCTION__)

/** @brief D-Bus type: the PoC targets the user/session instance (ADR 0001). */
constexpr GBusType BUS_TYPE = G_BUS_TYPE_SESSION;
/** @brief Well-known bus name of the systemd Manager. */
constexpr const char *BUS_NAME = "org.freedesktop.systemd1";
/** @brief Object path of the systemd Manager. */
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

/**
 * @brief Debounce window (ms) before acting on a tracked unit's removal (ADR 0007).
 *
 * Any positive value is correct: what breaks the resurrection loop is not tearing
 * down/rebuilding while a removal is pending, not the particular delay. This only
 * tunes revival latency; the measured systemctl burst is sub-millisecond.
 */
constexpr guint REMOVAL_DEBOUNCE_MS = 50;

/** @brief Sentinel GLib source id meaning "no timer pending" (g_timeout_add never returns 0). */
constexpr guint INVALID_TIMER_ID = 0;

/** @brief Heap context carried by a removal debounce timer: which unit, and the Supervisor. */
struct RemovalTimerCtx
{
    Supervisor *pSelf;  ///< The Supervisor that armed the timer; not owned.
    std::string Unit;   ///< The tracked unit name this timer acts on.
};

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
    m_ShuttingDown = true;

    /* Cancel any pending removal timers so none fire into a tearing-down Supervisor.
       g_source_remove also triggers freeRemovalCtx, releasing each timer context. */
    for (auto &entry : m_UnitsByService)
    {
        if (entry.second->RemovalTimerId != INVALID_TIMER_ID)
        {
            g_source_remove(entry.second->RemovalTimerId);
            entry.second->RemovalTimerId = INVALID_TIMER_ID;
        }

        GError *pError = nullptr;
        gdbus_systemd1_manager_call_stop_unit_sync(m_pManager, entry.first.c_str(), JOB_MODE, nullptr, nullptr, &pError);
        if (pError)
        {
            std::fprintf(stderr, "ERROR during stop sync of '%s': (code=%i, message=%s)\n", entry.first.c_str(), pError->code, pError->message);
            g_error_free(pError);
            pError = nullptr;
        }
    }

    log("  - Shutting down, getting signal to close");
    g_main_loop_quit(m_pLoop);
}

void Supervisor::terminateOnError(const std::string &context, GError *pError)
{
    LOG_ENTER();
    if (pError)
    {
        std::fprintf(stderr, "ERROR during %s: pError: (code=%i, message=%s)\n", context.c_str(), pError->code, pError->message);
        g_error_free(pError);
    }

    std::fprintf(stderr, "ERROR during %s\n", context.c_str());
    m_ExitCode = 1;
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

/* ######################################## Interface IUnitMonitorEventSink ######################################### */

void Supervisor::onSignalFailure(const std::string unitName, const int restarts)
{
    LOG_ENTER();
}

void Supervisor::onCoreDumpFailure(const std::string unitName, const int restarts)
{
    LOG_ENTER();
}

void Supervisor::onWatchdogFailure(const std::string unitName, const int restarts)
{
    LOG_ENTER();
}

void Supervisor::onExitCodeFailure(const std::string unitName, const int restarts, const int exitCode)
{
    LOG_ENTER();
}

void Supervisor::onStartLimitHitFailure(const std::string unitName, const int restarts)
{
    LOG_ENTER();
}

void Supervisor::onTimeoutFailure(const std::string unitName, const int restarts)
{
    LOG_ENTER();
}

void Supervisor::onResourcesFailure(const std::string unitName, const int restarts)
{
    LOG_ENTER();
}

void Supervisor::onOomKillFailure(const std::string unitName, const int restarts)
{
    LOG_ENTER();
}

/* ############################################ Internal async callbacks ############################################ */

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
        self->terminateOnError("onGDbusConnectionReady", pError);
    }
}

/* static */
void Supervisor::onDbusProxyReady(GObject * /*source*/, GAsyncResult *res, gpointer userData)
{
    LOG_ENTER();
    auto   *self   = static_cast<Supervisor *>(userData);
    GError *pError = nullptr;

    self->m_pManager = gdbus_systemd1_manager_proxy_new_finish(res, &pError);
    if (self->m_pManager == nullptr)
    {
        self->terminateOnError("onDbusProxyReady", pError);
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
        self->terminateOnError("onSubscribeDone", pError);
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
        self->terminateOnError("onStartUnit, job has not been made for startUnit", pError);
    }
}

/* static */
void Supervisor::onUnitNew(GDbusSystemd1Manager * /*object*/, const gchar *unit, const gchar *objectPath, gpointer userData)
{
    LOG_ENTER();
    auto *self = static_cast<Supervisor *>(userData);
    if (!self->m_Services.contains(unit))
    {
        log("  - Ignoring unit '%s' not in services to supervised", unit);
        return;
    }

    auto it = self->m_UnitsByService.find(unit);
    if (it != self->m_UnitsByService.end())
    {
        /* PENDING_REMOVAL: the unit reappeared within the debounce window (a
           systemctl burst, or it never really left) -> cancel the pending teardown. */
        if (it->second->RemovalTimerId != INVALID_TIMER_ID)
        {
            g_source_remove(it->second->RemovalTimerId);
            it->second->RemovalTimerId = INVALID_TIMER_ID;
            log("  - UnitNew cancelled the pending removal of '%s'", unit);
            return;
        }

        /* ACTIVE: already monitored. Do NOT rebuild. The object path is stable, and
           a GetAll on a not-live unit would resurrect it (lesson 009). */
        log("  - Ignoring UnitNew for '%s', already monitored", unit);
        return;
    }

    /* ABSENT: a genuine appearance of a live unit -> build the monitor. The sync
       proxy GetAll is safe here because the unit is present. */
    GError *pError = nullptr;
    log("  - Unit='%s' detected. Building Unit and Service proxys for a UnitMonitor", unit);
    GDbusSystemd1Unit *pUnit =
        gdbus_systemd1_unit_proxy_new_sync(self->m_pConnection, G_DBUS_PROXY_FLAGS_NONE, BUS_NAME, objectPath, nullptr, &pError);
    if (!pUnit)
    {
        self->terminateOnError("onUnitNew: Unit proxy fails", pError);
        return;
    }

    GDbusSystemd1Service *pService =
        gdbus_systemd1_service_proxy_new_sync(self->m_pConnection, G_DBUS_PROXY_FLAGS_NONE, BUS_NAME, objectPath, nullptr, &pError);
    if (!pService)
    {
        g_object_unref(pUnit);
        self->terminateOnError("onUnitNew: Service proxy fails", pError);
        return;
    }

    auto pUnitMonitor = std::make_shared<UnitMonitor>(pUnit, pService, unit, objectPath, self);
    self->m_UnitsByService.insert(std::make_pair(unit, std::make_shared<TrackedUnit>(std::move(pUnitMonitor), 0)));
}

/* static */
void Supervisor::onUnitRemoved(GDbusSystemd1Manager * /*object*/, const gchar *unit, const gchar * /*objectPath*/, gpointer userData)
{
    LOG_ENTER();
    auto *self = static_cast<Supervisor *>(userData);

    auto it = self->m_UnitsByService.find(unit);
    if (it == self->m_UnitsByService.end())
    {
        log("  - Ignoring UnitRemoved for '%s', not monitored", unit);
        return;
    }

    auto pTrackUnit = it->second;
    if (pTrackUnit->RemovalTimerId != INVALID_TIMER_ID)
    {
        log("  - Already PENDING_REMOVAL, leave the running timer (no-op) for '%s'", unit);
        return;
    }

    /* ACTIVE -> PENDING_REMOVAL: debounce. Do not tear down or build anything now;
       a proxy GetAll on a departing unit would resurrect it and loop (lesson 009).
       freeRemovalCtx releases pCtx whether the timer fires or is cancelled. */
    log("  - UnitRemoved for '%s'; arming %u ms debounce before teardown/revive", unit, REMOVAL_DEBOUNCE_MS);
    auto *pCtx = new RemovalTimerCtx{self, unit};
    pTrackUnit->RemovalTimerId =
        g_timeout_add_full(G_PRIORITY_DEFAULT, REMOVAL_DEBOUNCE_MS, &Supervisor::onRemovalSettled, pCtx, &Supervisor::freeRemovalCtx);
}

/* static */
gboolean Supervisor::onRemovalSettled(gpointer userData)
{
    LOG_ENTER();
    auto *pCtx = static_cast<RemovalTimerCtx *>(userData);
    auto *self = pCtx->pSelf;

    auto it = self->m_UnitsByService.find(pCtx->Unit);
    if (it != self->m_UnitsByService.end())
    {
        /* Clear the id before erasing: returning G_SOURCE_REMOVE destroys this
           source, so it must never be g_source_remove()d again. */
        it->second->RemovalTimerId = INVALID_TIMER_ID;
        log("  - '%s' settled gone; tearing down monitor and reviving", pCtx->Unit.c_str());
        self->m_UnitsByService.erase(it);

        if (!self->m_ShuttingDown)
        {
            gdbus_systemd1_manager_call_start_unit(self->m_pManager, pCtx->Unit.c_str(), JOB_MODE, nullptr, &Supervisor::onStartUnit, self);
        }
    }
    else
    {
        self->terminateOnError("  - We were fired by a unit that do not longer exists on the map, why!?", nullptr);
    }

    return G_SOURCE_REMOVE;
}

/* static */
void Supervisor::freeRemovalCtx(gpointer userData)
{
    delete static_cast<RemovalTimerCtx *>(userData); /* runs on both fire and cancel */
}