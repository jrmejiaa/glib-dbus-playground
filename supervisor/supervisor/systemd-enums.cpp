#include "systemd-enums.hpp"

active_state_t toActiveState(const std::string &rState)
{
    if (rState == "active")
    {
        return active_state_t::active;
    }
    if (rState == "reloading")
    {
        return active_state_t::reloading;
    }
    if (rState == "inactive")
    {
        return active_state_t::inactive;
    }
    if (rState == "failed")
    {
        return active_state_t::failed;
    }
    if (rState == "activating")
    {
        return active_state_t::activating;
    }
    if (rState == "deactivating")
    {
        return active_state_t::deactivating;
    }
    if (rState == "maintenance")
    {
        return active_state_t::maintenance;
    }
    return active_state_t::unknown; /* never UB on an unexpected string */
}

std::string activeStatetoStr(const active_state_t &rState)
{
    if (rState == active_state_t::active)
    {
        return "active";
    }
    if (rState == active_state_t::reloading)
    {
        return "reloading";
    }
    if (rState == active_state_t::inactive)
    {
        return "inactive";
    }
    if (rState == active_state_t::failed)
    {
        return "failed";
    }
    if (rState == active_state_t::activating)
    {
        return "activating";
    }
    if (rState == active_state_t::deactivating)
    {
        return "deactivating";
    }
    if (rState == active_state_t::maintenance)
    {
        return "maintenance";
    }
    return "unknown"; /* never UB on an unexpected enum value */
}

failed_cause_t toFailedCause(const std::string &rResult)
{
    if (rResult == "signal")
    {
        return failed_cause_t::signal;
    }
    if (rResult == "core-dump")
    {
        return failed_cause_t::core_dump;
    }
    if (rResult == "watchdog")
    {
        return failed_cause_t::watchdog;
    }
    if (rResult == "exit-code")
    {
        return failed_cause_t::exit_code;
    }
    if (rResult == "start-limit-hit")
    {
        return failed_cause_t::start_limit_hit;
    }
    if (rResult == "resources")
    {
        return failed_cause_t::resources;
    }
    if (rResult == "timeout")
    {
        return failed_cause_t::timeout;
    }
    if (rResult == "oom-kill")
    {
        return failed_cause_t::oom_kill;
    }
    return failed_cause_t::unknown; /* success/protocol or any unmapped value */
}

std::string failedCauseToStr(const failed_cause_t &rCause)
{
    if (rCause == failed_cause_t::signal)
    {
        return "signal";
    }
    if (rCause == failed_cause_t::core_dump)
    {
        return "core-dump";
    }
    if (rCause == failed_cause_t::watchdog)
    {
        return "watchdog";
    }
    if (rCause == failed_cause_t::exit_code)
    {
        return "exit-code";
    }
    if (rCause == failed_cause_t::start_limit_hit)
    {
        return "start-limit-hit";
    }
    if (rCause == failed_cause_t::resources)
    {
        return "resources";
    }
    if (rCause == failed_cause_t::timeout)
    {
        return "timeout";
    }
    if (rCause == failed_cause_t::oom_kill)
    {
        return "oom-kill";
    }
    return "unknown"; /* never UB on an unexpected enum value */
}
