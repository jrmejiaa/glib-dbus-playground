#ifndef POC_SYSTEMD_MANAGER_HPP
#define POC_SYSTEMD_MANAGER_HPP

#include <string>

#include <glib.h>

#include "gdbus-systemd-manager.h"

/**
 * @brief Drives the systemd Manager over D-Bus through the GLib-generated proxy.
 *
 * The class owns the full proof-of-concept flow and runs it asynchronously on a
 * caller-provided GMainLoop:
 *
 *     proxy_new -> Subscribe + connect "job-removed"
 *               -> StartUnit  --(JobRemoved: done)-->
 *                  RestartUnit --(JobRemoved: done)-->
 *                  StopUnit    --(JobRemoved: done)--> quit
 *
 * Completion is detected via the Manager's JobRemoved signal (matching the job
 * object path and checking its result), never by assuming the method reply means
 * the work is finished. Because the target unit is Type=notify, job completion
 * coincides with the service actually being ready.
 *
 * GLib invokes callbacks through a C function-pointer ABI, so the callback entry
 * points are @c static member functions with the exact signatures GLib expects.
 * Each recovers the instance from the @c gpointer user-data argument and operates
 * on it directly, keeping all state and logic inside the class.
 */
class SystemdManager
{
public:

    /**
     * @brief Construct the manager bound to an existing main loop.
     * @param loop The GMainLoop the asynchronous flow is driven on. Not owned.
     */
    explicit SystemdManager(GMainLoop *loop);

    ~SystemdManager();

    SystemdManager(const SystemdManager &)            = delete;
    SystemdManager &operator=(const SystemdManager &) = delete;

    /**
     * @brief Begin the asynchronous flow by constructing the proxy.
     *
     * Returns immediately; progress happens through callbacks until the loop is
     * quit. Run the GMainLoop after calling this.
     */
    void run();

    /**
     * @brief Process exit code reflecting the outcome of the flow.
     * @return 0 on success, non-zero if any step or job failed.
     */
    int exitCode() const { return m_ExitCode; }

private:

    /** @brief Lifecycle step the flow is currently waiting on. */
    enum class phase_t
    {
        starting,
        restarting,
        stopping,
        done,
    };

    /* ---- Static methods used directly as GLib C callbacks ---- */

    /*
     * These must be static: a non-static member function carries a hidden
     * `this` and cannot match the C function-pointer signature GLib expects.
     * Each recovers the instance from `userData` and does the work directly;
     * there is no separate delegate layer.
     */
    static void onProxyReady(GObject *source, GAsyncResult *res, gpointer userData);
    static void onSubscribeDone(GObject *source, GAsyncResult *res, gpointer userData);
    static void onJobEnqueued(GObject *source, GAsyncResult *res, gpointer userData);
    static void onJobRemoved(GDbusSystemd1Manager *manager, guint id, const gchar *job, const gchar *unit, const gchar *result, gpointer userData);

    /**
     * @brief Issue the lifecycle call matching the current phase.
     *
     * The returned job object path is captured in onJobEnqueued and matched
     * against the JobRemoved signal to detect completion.
     */
    void issueCurrentPhase();

    /**
     * @brief Report a fatal error, set a failure exit code, and quit the loop.
     * @param context Human-readable description of the failing step.
     * @param error   The GError to report and free; may be nullptr.
     */
    void finish(const std::string &context, GError *error);

    GMainLoop            *m_pLoop;       ///< Not owned.
    GDbusSystemd1Manager *m_pManager;    ///< Owned; unref in dtor.
    phase_t               m_Phase;       ///< Current lifecycle step.
    std::string           m_PendingJob;  ///< Job path we await; empty if none.
    int                   m_ExitCode;    ///< Final process exit code.
};

#endif  // POC_SYSTEMD_MANAGER_HPP
