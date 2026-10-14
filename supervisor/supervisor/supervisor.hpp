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
 * revives a daemon that stops cleanly (ADR 0007). It implements
 * IUnitMonitorEventSink to receive the classified failures its UnitMonitors detect.
 */
class Supervisor : public IUnitMonitorEventSink
{
public:  // Construction

    /**
     * @brief Constructs the Supervisor bound to an existing main loop.
     * @param pLoop     The GMainLoop the asynchronous flow runs on. Not owned.
     * @param services  The set of unit names to supervise.
     */
    explicit Supervisor(GMainLoop *pLoop, std::set<std::string> services);

    /** @brief Destroys the Supervisor. */
    ~Supervisor();

    Supervisor(const Supervisor &)            = delete;
    Supervisor &operator=(const Supervisor &) = delete;

public:  // Flow

    /**
     * @brief Begins the asynchronous flow. Returns immediately; run the loop after.
     */
    void run();

    /** @brief Stops all tracked units, cancels pending removal timers, and quits the loop. */
    void terminate();

    /**
     * @brief Process exit code reflecting the outcome of the flow.
     * @return 0 on success, non-zero if the flow failed.
     */
    int exitCode() const { return m_ExitCode; }

protected:  // Interface IUnitMonitorEventSink (docs live on the interface)

    virtual void onSignalFailure(const std::string unitName, const int restarts);
    virtual void onCoreDumpFailure(const std::string unitName, const int restarts);
    virtual void onWatchdogFailure(const std::string unitName, const int restarts);
    virtual void onExitCodeFailure(const std::string unitName, const int restarts, const int exitCode);
    virtual void onStartLimitHitFailure(const std::string unitName, const int restarts);
    virtual void onTimeoutFailure(const std::string unitName, const int restarts);
    virtual void onResourcesFailure(const std::string unitName, const int restarts);
    virtual void onOomKillFailure(const std::string unitName, const int restarts);

protected:  // Internal methods

    /**
     * @brief Completes the session-bus connection and requests the Manager proxy.
     * @param source_object  The async source object (unused).
     * @param res            The async result to finish.
     * @param userData       The Supervisor instance.
     */
    static void onGDbusConnectionReady(GObject *source_object, GAsyncResult *res, gpointer userData);

    /**
     * @brief Completes the Manager proxy and calls Subscribe().
     * @param source    The async source object (unused).
     * @param res       The async result to finish.
     * @param userData  The Supervisor instance.
     */
    static void onDbusProxyReady(GObject *source, GAsyncResult *res, gpointer userData);

    /**
     * @brief Connects UnitNew/UnitRemoved and starts the supervised units on launch.
     * @param source    The async source object (unused).
     * @param res       The async result to finish.
     * @param userData  The Supervisor instance.
     */
    static void onSubscribeDone(GObject *source, GAsyncResult *res, gpointer userData);

    /**
     * @brief Completes a StartUnit call and logs the enqueued job.
     * @param source    The async source object (unused).
     * @param res       The async result to finish.
     * @param userData  The Supervisor instance.
     */
    static void onStartUnit(GObject *source, GAsyncResult *res, gpointer userData);

    /**
     * @brief Attaches a UnitMonitor for a tracked unit, or cancels a pending removal
     * if the unit reappeared within the debounce window (ADR 0007).
     * @param object    The emitting Manager proxy (unused).
     * @param id        The unit name.
     * @param unitName  The unit's current object path.
     * @param userData  The Supervisor instance.
     */
    static void onUnitNew(GDbusSystemd1Manager * pManager, const gchar *unit, const gchar *objectPath, gpointer userData);

    /**
     * @brief Arms the removal debounce for a tracked unit (ADR 0007).
     * @param object    The emitting Manager proxy (unused).
     * @param id        The unit name.
     * @param unitName  The unit's object path (unused).
     * @param userData  The Supervisor instance.
     */
    static void onUnitRemoved(GDbusSystemd1Manager * pManager, const gchar *unit, const gchar *objectPath, gpointer userData);

    /**
     * @brief Fires when a tracked unit's removal debounce elapses un-cancelled.
     *
     * Tears down the monitor and revives the unit (unless shutting down). One-shot:
     * returns G_SOURCE_REMOVE. See ADR 0007.
     */
    static gboolean onRemovalSettled(gpointer userData);

    /** @brief GDestroyNotify that frees a RemovalCtx when its timer fires or is cancelled. */
    static void freeRemovalCtx(gpointer userData);

protected:  // Internal methods

    /**
     * @brief Reports a fatal error, sets a failure exit code, and quits the loop.
     * @param context  Human-readable description of the failing step.
     * @param error    The GError to report and free; may be nullptr.
     */
    void terminateOnError(const std::string &context, GError *error);

    /**
     * @brief Writes a timestamped, prefixed line to stdout.
     * @param pFormat  printf-style format string for the message.
     */
    static void log(const char *pFormat, ...) __attribute__((format(printf, 1, 2)));

protected:  // Attributes

    /**
     * @brief Per-tracked-unit state: the monitor and its pending removal timer.
     *
     * RemovalTimerId distinguishes the two live states: 0 means ACTIVE (no
     * pending removal); non-zero means PENDING_REMOVAL (debounce timer armed).
     */
    struct TrackedUnit
    {
        std::shared_ptr<UnitMonitor> pMonitor;        ///< The Unit+Service proxies.
        guint                        RemovalTimerId;  ///< 0 = ACTIVE; non-zero = PENDING_REMOVAL.
    };

    typedef std::map<std::string, std::shared_ptr<TrackedUnit>> unit_monitor_map_t;

    GMainLoop            *m_pLoop;        ///< Not owned.
    GDBusConnection      *m_pConnection;  ///< Shared session-bus connection; not owned.
    GDbusSystemd1Manager *m_pManager;     ///< Owned; unref in dtor.

    std::set<std::string> m_Services;              ///< The unit names to supervise.
    unit_monitor_map_t    m_UnitsByService;        ///< Tracked units keyed by name (identity survives GC).
    bool                  m_ShuttingDown = false;  ///< Suppresses revival during teardown.
    int                   m_ExitCode;              ///< Final process exit code.
};

#endif  // POC_SUPERVISOR_HPP
