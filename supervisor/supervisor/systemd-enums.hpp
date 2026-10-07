#ifndef POC_SYSTEMD_ENUMS_HPP
#define POC_SYSTEMD_ENUMS_HPP

#include <string>

/** @brief Unit ActiveState, mapped from the systemd wire string. */
enum class active_state_t
{
    active,        ///< Unit is running.
    reloading,     ///< Unit is active and reloading its configuration.
    inactive,      ///< Unit is stopped.
    failed,        ///< Unit stopped with a failure.
    activating,    ///< Unit is starting up.
    deactivating,  ///< Unit is shutting down.
    maintenance,   ///< Unit is inactive while a maintenance operation runs.
    unknown        ///< Unmapped or unrecognized wire value.
};

/** @brief Service Result failure cause, mapped from the systemd wire string. */
enum class failed_cause_t
{
    signal,           ///< Main process was killed by a signal.
    core_dump,        ///< Main process dumped core.
    watchdog,         ///< Watchdog timeout: a sd_notify(WATCHDOG=1) ping was missed.
    exit_code,        ///< Main process exited with a non-zero code.
    start_limit_hit,  ///< Start-limit burst was exhausted; systemd gave up.
    resources,        ///< Not enough resources to run the unit.
    timeout,          ///< A start or stop operation timed out.
    oom_kill,         ///< Killed by the kernel out-of-memory killer.
    unknown           ///< Unmapped value, including the non-failures "success" and "protocol".
};

/**
 * @brief Maps a Unit ActiveState wire string to its enum.
 * @param rState  The ActiveState string as reported by systemd.
 * @return The matching active_state_t, or active_state_t::unknown if unmapped.
 */
active_state_t toActiveState(const std::string &rState);

/**
 * @brief Maps an ActiveState enum back to its systemd wire string.
 * @param rState  The enum value.
 * @return The wire string, or "unknown" for an unmapped value.
 */
std::string activeStatetoStr(const active_state_t &rState);

/**
 * @brief Maps a Service Result wire string to its failure-cause enum.
 * @param rResult  The Result string as reported by systemd.
 * @return The matching failed_cause_t, or failed_cause_t::unknown if unmapped
 * (including the non-failure values "success" and "protocol").
 */
failed_cause_t toFailedCause(const std::string &rResult);

/**
 * @brief Maps a failure-cause enum back to its systemd Result wire string.
 * @param rCause  The enum value.
 * @return The wire string, or "unknown" for an unmapped value.
 */
std::string failedCauseToStr(const failed_cause_t &rCause);

#endif  // POC_SYSTEMD_ENUMS_HPP
