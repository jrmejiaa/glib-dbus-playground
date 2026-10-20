#include "daemon-state-watcher.hpp"

#include <cstdio>
#include <cstdarg>

/** @brief Object path of the Daemon PoC. */
constexpr const char *OBJECT_PATH = "/org/poc/Daemon";

/** @brief Well-known name prefix each daemon owns; the template instance id is appended (e.g. org.poc.Daemon1). */
constexpr const char *DAEMON_NAME_PREFIX = "org.poc.Daemon";

/** @brief Separator introducing the template instance id in a unit name (e.g. the '@' in poc-dummy@1.service). */
constexpr char INSTANCE_SEPARATOR = '@';

/** @brief Suffix every supervised unit name ends with, following the instance id. */
constexpr const char *UNIT_SUFFIX = ".service";

#define LOG_ENTER() log(__PRETTY_FUNCTION__)

DaemonStateWatcher::DaemonStateWatcher(GDBusConnection *pConnection, const std::string &unit, IDaemonStateEventSink *pSink)
    : m_pConnection(pConnection),
      m_pSink(pSink),
      m_pDbusDaemon(nullptr),
      m_UnitName(unit),
      m_WellKnownName(getWellKnownNameOf(unit)),
      m_State(state_t::unknown)
{
    LOG_ENTER();
    log("  - Calling proxy for unit %s with well-known-name %s", unit.c_str(), m_WellKnownName.c_str());
    gdbus_poc_daemon_proxy_new(
        m_pConnection, G_DBUS_PROXY_FLAGS_NONE, m_WellKnownName.c_str(), OBJECT_PATH, nullptr, &DaemonStateWatcher::onDbusProxyReady, this);
}

DaemonStateWatcher::~DaemonStateWatcher()
{
    LOG_ENTER();
    g_object_unref(m_pDbusDaemon);
}

void DaemonStateWatcher::log(const char *pFormat, ...)
{
    std::fprintf(stdout, "    [DaemonStateWatcher] ");

    std::va_list args;
    va_start(args, pFormat);
    std::vfprintf(stdout, pFormat, args);
    va_end(args);

    std::fprintf(stdout, "\n");
    std::fflush(stdout);
}

void DaemonStateWatcher::onError(const std::string &context, GError *pError)
{
    LOG_ENTER();
    if (pError)
    {
        std::fprintf(stderr, "ERROR during %s: pError: (code=%i, message=%s)\n", context.c_str(), pError->code, pError->message);
        g_error_free(pError);
    }
    else
    {
        std::fprintf(stderr, "ERROR during %s\n", context.c_str());
    }
}

/* static */
std::string DaemonStateWatcher::getWellKnownNameOf(const std::string unit)
{
    const std::size_t atPos  = unit.find(INSTANCE_SEPARATOR);
    const std::size_t dotPos = unit.rfind(UNIT_SUFFIX);
    if (atPos == std::string::npos || dotPos == std::string::npos || dotPos <= atPos)
    {
        /* This should never happen: only supervised "<template>@<id>.service" units reach here. */
        log("  - Cannot derive a well-known name from malformed unit '%s'", unit.c_str());
        return std::string();
    }

    const std::string instanceId = unit.substr(atPos + 1, dotPos - atPos - 1);
    return DAEMON_NAME_PREFIX + instanceId;
}

/* static */
void DaemonStateWatcher::onDbusProxyReady(GObject *source, GAsyncResult *res, gpointer userData)
{
    LOG_ENTER();
    auto   *self   = static_cast<DaemonStateWatcher *>(userData);
    GError *pError = nullptr;

    self->m_pDbusDaemon = gdbus_poc_daemon_proxy_new_finish(res, &pError);
    if (!self->m_pDbusDaemon)
    {
        onError("onDbusProxyReady", pError);
        return;
    }

    g_signal_connect(self->m_pDbusDaemon, "notify::state", G_CALLBACK(&DaemonStateWatcher::onStateChanged), self);

    /* An unowned name yields a live proxy with an empty cache, where get_state()
       would report 0 (initializing) and mask the daemon's absence or a name mismatch.
       get_name_owner() is NULL exactly in that case, so make it loud instead. */
    gchar *pOwner = g_dbus_proxy_get_name_owner(G_DBUS_PROXY(self->m_pDbusDaemon));
    if (!pOwner)
    {
        self->m_State = state_t::unknown;
        log("  - WARNING: well-known name '%s' has no owner; Waiting for it to appear", self->m_WellKnownName.c_str());
        return;
    }
    self->m_State = readState(self->m_pDbusDaemon);

    const std::string state = getStateName(self->m_State);
    log("  - Proxy ready for unit %s (owner=%s). Attached to 'state' signal, current state '%s'", self->m_UnitName.c_str(), pOwner, state.c_str());
    g_free(pOwner);
}

/* static */
void DaemonStateWatcher::onStateChanged(GDbusPocDaemon *pDaemon, GParamSpec *pspec, gpointer userData)
{
    LOG_ENTER();
    auto   *self     = static_cast<DaemonStateWatcher *>(userData);
    state_t newState = readState(pDaemon);

    const std::string oldStateName = getStateName(self->m_State);
    const std::string newStateName = getStateName(newState);
    log("  - Getting state changed of unit %s. From '%s' -> '%s'", self->m_UnitName.c_str(), oldStateName.c_str(), newStateName.c_str());

    self->m_State = newState;
    if (self->m_State == state_t::degraded)
    {
        self->m_pSink->onDegradedState(self->m_UnitName);
    }
}

/* static */
state_t DaemonStateWatcher::readState(GDbusPocDaemon *pDaemon)
{
    /* get_state() returns 0 (initializing) for an empty cache; probe the cached
       property directly so an unpopulated proxy reads as unknown, not initializing. */
    GVariant *pCached = g_dbus_proxy_get_cached_property(G_DBUS_PROXY(pDaemon), "State");
    if (pCached == nullptr)
    {
        return state_t::unknown;
    }

    g_variant_unref(pCached);
    return static_cast<state_t>(gdbus_poc_daemon_get_state(pDaemon));
}

/* static */
std::string DaemonStateWatcher::getStateName(state_t state)
{
    std::string name = "unknown";
    switch (state)
    {
        case state_t::initializing:
            name = "initializing";
            break;
        case state_t::ready:
            name = "ready";
            break;
        case state_t::degraded:
            name = "degraded";
            break;
        default:
            break;
    }
    return name;
}