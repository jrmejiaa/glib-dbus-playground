#ifndef POC_UNIT_MONITOR_HPP
#define POC_UNIT_MONITOR_HPP

#include <string>
#include <glib.h>

#include <gdbus-systemd-unit.h>

/**
 * @brief Watches one systemd unit's Unit and Service interfaces for the Supervisor.
 *
 * Owns one Unit proxy and one Service proxy on the Supervisor's shared connection,
 * value-gates ActiveState, and on a real transition reads Result/ExecMainStatus/
 * NRestarts from the proxy cache and classifies the cause. Skeleton stub: the
 * Supervisor track (issue #1) fills in the proxies, the g-properties-changed
 * handler, the value-gating, and the cause classification.
 */
class UnitMonitor
{
public:  // Construction

    UnitMonitor(GDbusSystemd1Unit *pUnit, GDbusSystemd1Service *pService, const std::string &rUnitName, const std::string &rObjectPath);

    ~UnitMonitor();

    UnitMonitor(const UnitMonitor &)            = delete;
    UnitMonitor &operator=(const UnitMonitor &) = delete;

public:  // Identity

    /**
     * @brief The unit name this monitor is keyed by.
     * @return The unit name passed at construction.
     */
    const std::string &unitName() const { return m_UnitName; }

protected:

    static void log(const char *pFormat, ...) __attribute__((format(printf, 1, 2)));

private:  // Attributes

    GDbusSystemd1Unit    *m_pUnit;
    GDbusSystemd1Service *m_pService;
    const std::string     m_UnitName;  ///< Monitor identity (survives GC; the path does not).
    const std::string     m_ObjectPath;
};

#endif  // POC_UNIT_MONITOR_HPP
