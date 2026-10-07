#ifndef POC_UNIT_MONITOR_HPP
#define POC_UNIT_MONITOR_HPP

#include <string>
#include <glib.h>

#include <gdbus-systemd-unit.h>

#include "systemd-enums.hpp"

/**
 * @brief Receives classified unit-failure events from a UnitMonitor.
 *
 * One callback per failure cause; the implementer decides how to narrate or react.
 * All callbacks carry the unit name and the cumulative restart count (NRestarts).
 */
class IUnitMonitorEventSink
{
public:  // Failure events

    /**
     * @brief Called when the unit's main process was killed by a signal.
     * @param unitName  The failing unit's name.
     * @param restarts  The unit's cumulative restart count.
     */
    virtual void onSignalFailure(const std::string unitName, const int restarts) = 0;

    /**
     * @brief Called when the unit's main process dumped core.
     * @param unitName  The failing unit's name.
     * @param restarts  The unit's cumulative restart count.
     */
    virtual void onCoreDumpFailure(const std::string unitName, const int restarts) = 0;

    /**
     * @brief Called when the unit missed its watchdog deadline.
     * @param unitName  The failing unit's name.
     * @param restarts  The unit's cumulative restart count.
     */
    virtual void onWatchdogFailure(const std::string unitName, const int restarts) = 0;

    /**
     * @brief Called when the unit's main process exited with a non-zero code.
     * @param unitName  The failing unit's name.
     * @param restarts  The unit's cumulative restart count.
     * @param exitCode  The main process exit status (ExecMainStatus).
     */
    virtual void onExitCodeFailure(const std::string unitName, const int restarts, const int exitCode) = 0;

    /**
     * @brief Called when the unit exhausted its start-limit burst and systemd gave up.
     * @param unitName  The failing unit's name.
     * @param restarts  The unit's cumulative restart count.
     */
    virtual void onStartLimitHitFailure(const std::string unitName, const int restarts) = 0;

    /**
     * @brief Called when a start or stop operation for the unit timed out.
     * @param unitName  The failing unit's name.
     * @param restarts  The unit's cumulative restart count.
     */
    virtual void onTimeoutFailure(const std::string unitName, const int restarts) = 0;

    /**
     * @brief Called when the unit failed for lack of resources.
     * @param unitName  The failing unit's name.
     * @param restarts  The unit's cumulative restart count.
     */
    virtual void onResourcesFailure(const std::string unitName, const int restarts) = 0;

    /**
     * @brief Called when the unit was killed by the kernel out-of-memory killer.
     * @param unitName  The failing unit's name.
     * @param restarts  The unit's cumulative restart count.
     */
    virtual void onOomKillFailure(const std::string unitName, const int restarts) = 0;
};

/**
 * @brief Watches one systemd unit's Unit and Service interfaces and reports failures.
 *
 * Owns a Unit proxy and a Service proxy on the Supervisor's shared connection and
 * value-gates ActiveState. On a transition into failed it reads Result,
 * ExecMainStatus, and NRestarts from the proxy cache, classifies the cause, and
 * dispatches to the IUnitMonitorEventSink. GLib invokes the property-change callback
 * through a C function pointer, so it is a static member that recovers the instance
 * from the user-data pointer.
 */
class UnitMonitor
{
public:  // Construction

    /**
     * @brief Constructs a monitor for a loaded unit and watches its ActiveState.
     * @param pUnit        The Unit interface proxy; ownership is taken (unref in dtor).
     * @param pService     The Service interface proxy; ownership is taken (unref in dtor).
     * @param rUnitName    The unit name that is this monitor's identity.
     * @param rObjectPath  The unit's current object path.
     * @param pSink        The sink notified of classified failures; not owned.
     */
    UnitMonitor(GDbusSystemd1Unit *pUnit, GDbusSystemd1Service *pService, const std::string &rUnitName, const std::string &rObjectPath,
                IUnitMonitorEventSink *pSink);

    /** @brief Unrefs the owned Unit and Service proxies. */
    ~UnitMonitor();

    UnitMonitor(const UnitMonitor &)            = delete;
    UnitMonitor &operator=(const UnitMonitor &) = delete;

public:  // Identity

    /**
     * @brief The unit name this monitor is keyed by.
     * @return The unit name passed at construction.
     */
    const std::string &unitName() const { return m_UnitName; }

protected:  // Internal methods

    /**
     * @brief Reacts to an ActiveState change: value-gates and, on a transition into
     * failed, classifies the cause and notifies the sink.
     * @param pUnit     The Unit proxy that emitted the change.
     * @param pspec     The changed property's GParamSpec (unused).
     * @param userData  The UnitMonitor instance.
     */
    static void onActiveStateChanged(GDbusSystemd1Unit *pUnit, GParamSpec *pspec, gpointer userData);

    /**
     * @brief Writes a timestamped, prefixed line to stdout.
     * @param pFormat  printf-style format string for the message.
     */
    static void log(const char *pFormat, ...) __attribute__((format(printf, 1, 2)));

private:  // Attributes

    GDbusSystemd1Unit     *m_pUnit;            ///< Owned Unit proxy; unref in dtor.
    GDbusSystemd1Service  *m_pService;         ///< Owned Service proxy; unref in dtor.
    IUnitMonitorEventSink *m_pSink;            ///< Not owned; notified of classified failures.
    const std::string      m_UnitName;         ///< Monitor identity (survives GC; the path does not).
    const std::string      m_ObjectPath;       ///< The unit's current object path.
    active_state_t         m_LastActiveState;  ///< Last observed ActiveState, for value-gating.
};

#endif  // POC_UNIT_MONITOR_HPP
