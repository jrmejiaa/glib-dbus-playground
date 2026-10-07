/**
 * @mainpage Supervisor PoC (step-3)
 *
 * @section overview Overview
 *
 * A long-running Supervisor that observes a fleet of dummy daemons over D-Bus and
 * narrates the complete truth of each one. It starts the daemons on launch, then
 * watches systemd's per-unit Unit/Service state and classifies every failure.
 *
 * @section revival Revival ownership
 *
 * systemd owns failure-revival (@c Restart=on-failure plus the watchdog); the
 * Supervisor owns clean-stop revival, reacting to a debounced @c UnitRemoved and
 * re-issuing @c StartUnit (ADR 0007).
 *
 * @section flow Startup flow
 *
 * @code
 *   g_bus_get -> Manager proxy -> Subscribe()
 *     -> connect UnitNew/UnitRemoved -> StartUnit each supervised unit
 *   UnitNew(tracked)     -> build a UnitMonitor (Unit+Service proxies)
 *   ActiveState=failed   -> classify Result -> IUnitMonitorEventSink callback
 *   UnitRemoved(tracked) -> debounce -> settled gone -> revive
 * @endcode
 */

#include <csignal>
#include <memory>
#include <glib.h>

#include "supervisor.hpp"

/*
 * Signals/Signal handlers are a pretty sucky concept:
 *
 * - no way to pass user data
 * - no sane way to map it to a class method of a non singleton
 *
 * The signal handler needs the stop the execution of the event loop,
 * so it needs an instance pointer, so the instance pointer needs to
 * be accessible globally, so we need a global pointer. So here we go...
*/
Supervisor *gp_Supervisor = nullptr;

/**
 * @brief POSIX handler for SIGINT/SIGTERM: asks the Supervisor to stop its loop.
 * @param sig_num  The delivered signal number (unused).
 */
void signalHandler(int sig_num)
{
    gp_Supervisor->terminate();
}

/**
 * @brief Builds the Supervisor, installs the signal handlers, and runs the loop.
 * @return The Supervisor's exit code: 0 on success, non-zero on failure.
 */
int main()
{
    std::fprintf(stdout, "Starting supervisor daemon\n");
    GMainLoop *loop = g_main_loop_new(nullptr, false);

    /* This could be later a parameter of the executable */
    gp_Supervisor = new Supervisor(loop, {"poc-dummy@1.service", "poc-dummy@2.service", "poc-dummy@3.service"});
    gp_Supervisor->run();

    /* it's safe to install signal handlers now, since the global alarm instance is created */
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    g_main_loop_run(loop);

    const int exitCode = gp_Supervisor->exitCode();
    delete gp_Supervisor;
    g_main_loop_unref(loop);

    std::fprintf(stdout, "Shutting supervisor daemon, exit-code=%d\n", exitCode);
    return exitCode;
}
