#include "systemd-manager.hpp"

#include <cstdio>

/*
 * The Manager is published under the same well-known name and object path on
 * both the system and the user (session) bus; only the bus type and the unit
 * install location differ. See ADR 0001 for why the PoC targets the user
 * instance.
 */
constexpr GBusType    BUS_TYPE    = G_BUS_TYPE_SESSION;
constexpr const char *BUS_NAME    = "org.freedesktop.systemd1";
constexpr const char *OBJECT_PATH = "/org/freedesktop/systemd1";

/** @brief Unit the PoC starts, restarts, and stops. */
constexpr const char *UNIT_NAME = "poc-dummy.service";

/**
 * @brief Job mode: enqueue the job, displacing any conflicting queued job.
 *
 * The valid modes are a systemd documentation contract, not something the D-Bus
 * introspection XML encodes.
 */
constexpr const char *JOB_MODE = "replace";

/** @brief JobRemoved result string denoting success. */
constexpr const char *RESULT_DONE = "done";

SystemdManager::SystemdManager(GMainLoop *loop)
    : m_pLoop(loop),
      m_pManager(nullptr),
      m_Phase(phase_t::starting),
      m_ExitCode(0)
{}

SystemdManager::~SystemdManager()
{
    if (m_pManager)
    {
        g_object_unref(m_pManager);
    }
}

void SystemdManager::run()
{
    std::fprintf(stdout, "Connecting to %s on the %s bus...\n", BUS_NAME, BUS_TYPE == G_BUS_TYPE_SESSION ? "session (user)" : "system");

    /*
     * Asynchronous proxy construction: resolves the bus connection, the
     * well-known name, and the object path, then invokes onProxyReady on this
     * thread's main context. `this` travels as the user-data pointer.
     */
    gdbus_systemd1_manager_proxy_new_for_bus(BUS_TYPE, G_DBUS_PROXY_FLAGS_NONE, BUS_NAME, OBJECT_PATH, nullptr, &SystemdManager::onProxyReady, this);
}

void SystemdManager::finish(const std::string &context, GError *error)
{
    if (error != nullptr)
    {
        std::fprintf(stderr, "ERROR during %s: %s\n", context.c_str(), error->message);
        g_error_free(error);
        m_ExitCode = 1;
    }
    g_main_loop_quit(m_pLoop);
}

/* ---- Static methods used directly as GLib C callbacks ---- */

void SystemdManager::onProxyReady(GObject * /*source*/, GAsyncResult *res, gpointer userData)
{
    auto   *self  = static_cast<SystemdManager *>(userData);
    GError *error = nullptr;

    // finish() returns the interface-typed pointer to the concrete proxy GObject.
    self->m_pManager = gdbus_systemd1_manager_proxy_new_finish(res, &error);
    if (self->m_pManager == nullptr)
    {
        self->finish("proxy_new", error);
        return;
    }

    /*
     * Attach the JobRemoved handler before calling Subscribe or issuing any job,
     * so a fast-completing job can never fire the signal before the handler is
     * connected. The generated proxy is a GObject; the signal is "job-removed".
     */
    g_signal_connect(self->m_pManager, "job-removed", G_CALLBACK(&SystemdManager::onJobRemoved), self);

    std::fprintf(stdout, "Proxy ready. Subscribing to signals...\n");
    gdbus_systemd1_manager_call_subscribe(self->m_pManager, nullptr, &SystemdManager::onSubscribeDone, self);
}

void SystemdManager::onSubscribeDone(GObject * /*source*/, GAsyncResult *res, gpointer userData)
{
    auto   *self  = static_cast<SystemdManager *>(userData);
    GError *error = nullptr;

    if (!gdbus_systemd1_manager_call_subscribe_finish(self->m_pManager, res, &error))
    {
        self->finish("Subscribe", error);
        return;
    }

    std::fprintf(stdout, "Subscribed. Starting lifecycle...\n");
    self->m_Phase = phase_t::starting;
    self->issueCurrentPhase();
}

void SystemdManager::onJobEnqueued(GObject * /*source*/, GAsyncResult *res, gpointer userData)
{
    auto   *self    = static_cast<SystemdManager *>(userData);
    GError *error   = nullptr;
    gchar  *jobPath = nullptr;  // GLib-allocated out-param; copied then freed below.
    bool    ok      = false;

    /*
     * The three lifecycle methods share the same reply shape (an object-path
     * out-param plus a gboolean). Pick the finish() matching the phase that
     * issued the call.
     */
    switch (self->m_Phase)
    {
        case phase_t::starting:
            ok = gdbus_systemd1_manager_call_start_unit_finish(self->m_pManager, &jobPath, res, &error);
            break;
        case phase_t::restarting:
            ok = gdbus_systemd1_manager_call_restart_unit_finish(self->m_pManager, &jobPath, res, &error);
            break;
        case phase_t::stopping:
            ok = gdbus_systemd1_manager_call_stop_unit_finish(self->m_pManager, &jobPath, res, &error);
            break;
        case phase_t::done:
            return;
    }

    if (!ok)
    {
        self->finish("lifecycle call", error);
        return;
    }

    // Take ownership of the job path as a std::string, then release GLib's copy.
    self->m_PendingJob = (jobPath != nullptr) ? jobPath : "";
    g_free(jobPath);

    std::fprintf(stdout, "  enqueued, waiting for job %s to complete...\n", self->m_PendingJob.c_str());
}

void SystemdManager::onJobRemoved(GDbusSystemd1Manager * /*mgr*/, guint /*id*/, const gchar *job, const gchar *unit, const gchar *result,
                                  gpointer userData)
{
    auto *self = static_cast<SystemdManager *>(userData);

    // Convert the C boundary strings to std::string once, at the boundary.
    const std::string jobPath   = (job) ? job : "";
    const std::string unitName  = (unit) ? unit : "";
    const std::string jobResult = (result) ? result : "";

    /*
     * JobRemoved fires for every job on the bus; ignore any that is not the one
     * currently awaited.
     */
    if (self->m_PendingJob.empty() || jobPath != self->m_PendingJob)
    {
        return;
    }

    std::fprintf(stdout, "  job %s for %s finished: result=%s\n", jobPath.c_str(), unitName.c_str(), jobResult.c_str());
    self->m_PendingJob.clear();

    // Anything other than "done" (failed/canceled/timeout/...) is a failure.
    if (jobResult != RESULT_DONE)
    {
        std::fprintf(stderr, "  job did not succeed (result=%s); aborting.\n", jobResult.c_str());
        self->m_ExitCode = 1;
        g_main_loop_quit(self->m_pLoop);
        return;
    }

    // Advance to the next phase based on where we are.
    switch (self->m_Phase)
    {
        case phase_t::starting:
            self->m_Phase = phase_t::restarting;
            self->issueCurrentPhase();
            break;
        case phase_t::restarting:
            self->m_Phase = phase_t::stopping;
            self->issueCurrentPhase();
            break;
        case phase_t::stopping:
            self->m_Phase = phase_t::done;
            std::fprintf(stdout, "\nPoC sequence complete (all jobs confirmed done).\n");
            g_main_loop_quit(self->m_pLoop);
            break;
        case phase_t::done:
            break;
    }
}

void SystemdManager::issueCurrentPhase()
{
    switch (m_Phase)
    {
        case phase_t::starting:
            std::fprintf(stdout, "\nStarting %s (mode=%s)...\n", UNIT_NAME, JOB_MODE);
            gdbus_systemd1_manager_call_start_unit(m_pManager, UNIT_NAME, JOB_MODE, nullptr, &SystemdManager::onJobEnqueued, this);
            break;
        case phase_t::restarting:
            std::fprintf(stdout, "\nRestarting %s...\n", UNIT_NAME);
            gdbus_systemd1_manager_call_restart_unit(m_pManager, UNIT_NAME, JOB_MODE, nullptr, &SystemdManager::onJobEnqueued, this);
            break;
        case phase_t::stopping:
            std::fprintf(stdout, "\nStopping %s...\n", UNIT_NAME);
            gdbus_systemd1_manager_call_stop_unit(m_pManager, UNIT_NAME, JOB_MODE, nullptr, &SystemdManager::onJobEnqueued, this);
            break;
        case phase_t::done:
            break;
    }
}
