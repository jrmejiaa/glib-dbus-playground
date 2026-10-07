#include "unit-monitor.hpp"

#include <cstdio>
#include <cstdarg>

#define LOG_ENTER() log(__PRETTY_FUNCTION__)

UnitMonitor::UnitMonitor(GDbusSystemd1Unit *pUnit, GDbusSystemd1Service *pService, const std::string &rUnitName, const std::string &rObjectPath,
                         IUnitMonitorEventSink *pSink)
    : m_pUnit(pUnit),
      m_pService(pService),
      m_pSink(pSink),
      m_UnitName(rUnitName),
      m_ObjectPath(rObjectPath),
      m_LastActiveState(active_state_t::unknown)
{
    log("Constructing object with unit-proxy=%p and service-proxy=%p. Attaching to signals.", pUnit, pService);
    g_signal_connect(pUnit, "notify::active-state", G_CALLBACK(&UnitMonitor::onActiveStateChanged), this);
}

UnitMonitor::~UnitMonitor()
{
    log("Destructing object and freeing proxys");
    g_object_unref(m_pUnit);
    g_object_unref(m_pService);
}

void UnitMonitor::log(const char *pFormat, ...)
{
    std::fprintf(stdout, "    [UnitMonitor] ");

    std::va_list args;
    va_start(args, pFormat);
    std::vfprintf(stdout, pFormat, args);
    va_end(args);

    std::fprintf(stdout, "\n");
    std::fflush(stdout);
}

/* static */
void UnitMonitor::onActiveStateChanged(GDbusSystemd1Unit *pUnit, GParamSpec *pspec, gpointer userData)
{
    LOG_ENTER();
    auto *self = static_cast<UnitMonitor *>(userData);

    const std::string    stateWire = gdbus_systemd1_unit_get_active_state(pUnit);
    const active_state_t state     = toActiveState(stateWire);

    if (state == self->m_LastActiveState)
    {
        /* Skip signal callbacks that do not carry new information */
        return;
    }

    const std::string lastActiveStateStr = activeStatetoStr(self->m_LastActiveState);
    log("  - ActiveState of unit='%s' changed from '%s' to '%s'", self->m_UnitName.c_str(), lastActiveStateStr.c_str(), stateWire.c_str());

    if (state == active_state_t::failed)
    {
        log("  - The unit='%s' is currently on failing state. Getting information to find the reason", self->m_UnitName.c_str());
        const std::string resultWire = gdbus_systemd1_service_get_result(self->m_pService);
        const int         exitCode   = gdbus_systemd1_service_get_exec_main_status(self->m_pService);
        const int         restarts   = gdbus_systemd1_service_get_nrestarts(self->m_pService);

        const failed_cause_t cause = toFailedCause(resultWire);
        log("  - unit='%s' failed with cause='%s', restarts='%d', exit-code='%d'", self->m_UnitName.c_str(), resultWire.c_str(), restarts, exitCode);

        switch (cause)
        {
            case failed_cause_t::signal:
                self->m_pSink->onSignalFailure(self->m_UnitName, restarts);
                break;
            case failed_cause_t::core_dump:
                self->m_pSink->onCoreDumpFailure(self->m_UnitName, restarts);
                break;
            case failed_cause_t::watchdog:
                self->m_pSink->onWatchdogFailure(self->m_UnitName, restarts);
                break;
            case failed_cause_t::exit_code:
                self->m_pSink->onExitCodeFailure(self->m_UnitName, restarts, exitCode);
                break;
            case failed_cause_t::start_limit_hit:
                self->m_pSink->onStartLimitHitFailure(self->m_UnitName, restarts);
                break;
            case failed_cause_t::resources:
                self->m_pSink->onResourcesFailure(self->m_UnitName, restarts);
                break;
            case failed_cause_t::oom_kill:
                self->m_pSink->onOomKillFailure(self->m_UnitName, restarts);
                break;
            default:
                break;
        }
    }

    /* Saving new state as last */
    self->m_LastActiveState = state;
}
