#ifndef POC_SUPERVISOR_HPP
#define POC_SUPERVISOR_HPP

#include <string>
#include <set>
#include <map>
#include <memory>
#include <glib.h>

#include <gdbus-systemd-manager.h>

#include "unit-monitor.hpp"

/**
 * @brief Observes a fleet of dummy daemons over D-Bus and narrates their truth.
 *
 * Owns the systemd Manager proxy and the single shared connection, starts the
 * daemon instances on launch, attaches a UnitMonitor per tracked unit, and
 * revives a daemon that stops cleanly (ADR 0007). Skeleton stub: the Supervisor
 * track (issue #1) fills in the proxy wiring, the name->UnitMonitor map, the
 * UnitNew/UnitRemoved handlers, and the clean-stop revive.
 */
class Supervisor
{
public:  // Construction

    /**
     * @brief Construct the Supervisor bound to an existing main loop.
     * @param pLoop  The GMainLoop the asynchronous flow runs on. Not owned.
     */
    explicit Supervisor(GMainLoop *pLoop, std::set<std::string> services);

    ~Supervisor();

    Supervisor(const Supervisor &)            = delete;
    Supervisor &operator=(const Supervisor &) = delete;

public:  // Flow

    /**
     * @brief Begin the asynchronous flow. Returns immediately; run the loop after.
     */
    void run();

    void terminate();

    /**
     * @brief Process exit code reflecting the outcome of the flow.
     * @return 0 on success, non-zero if the flow failed.
     */
    int exitCode() const { return m_ExitCode; }

protected:  // async static callback methods

    static void onGDbusConnectionReady(GObject *source_object, GAsyncResult *res, gpointer userData);
    static void onDbusProxyReady(GObject *source, GAsyncResult *res, gpointer userData);
    static void onSubscribeDone(GObject *source, GAsyncResult *res, gpointer userData);
    static void onStartUnit(GObject *source, GAsyncResult *res, gpointer userData);
    static void onUnitNew(GDbusSystemd1Manager *object, const gchar *id, const gchar *unitName, gpointer userData);
    static void onUnitRemoved(GDbusSystemd1Manager *object, const gchar *id, const gchar *unitName, gpointer userData);

protected:  // Intern methods

    /**
     * @brief Report a fatal error, set a failure exit code, and quit the loop.
     * @param context Human-readable description of the failing step.
     * @param error   The GError to report and free; may be nullptr.
     */
    void finish(const std::string &context, GError *error);

    static void log(const char *pFormat, ...) __attribute__((format(printf, 1, 2)));

protected:  // Attributes

    typedef std::map<std::string, std::shared_ptr<UnitMonitor>> unit_monitor_map_t;

    GMainLoop            *m_pLoop;  ///< Not owned.
    GDBusConnection      *m_pConnection;
    GDbusSystemd1Manager *m_pManager;  ///< Owned; unref in dtor.

    std::set<std::string> m_Services;
    unit_monitor_map_t    m_UnitsByService;
    int                   m_ExitCode;  ///< Final process exit code.
};

#endif  // POC_SUPERVISOR_HPP
