#include <csignal>
#include <memory>
#include <glib.h>

#include "alarm-service.hpp"

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
AlarmDbusServer *gp_AlarmServer = nullptr;

/**
 * @brief POSIX handler for SIGINT/SIGTERM: ask the server to stop its loop.
 * @param sig_num The delivered signal number (unused).
 */
void signalHandler(int sig_num)
{
    gp_AlarmServer->terminate();
}

int main()
{
    std::fprintf(stdout, "Starting alarm server daemon\n");
    GMainLoop *loop = g_main_loop_new(nullptr, false);

    gp_AlarmServer = new AlarmDbusServer(loop);
    gp_AlarmServer->run();

    /* it's safe to install signal handlers now, since the global alarm instance is created */
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    g_main_loop_run(loop);

    const int exitCode = gp_AlarmServer->exitCode();
    delete gp_AlarmServer;
    g_main_loop_unref(loop);

    std::fprintf(stdout, "Shutting alarm server daemon, exit-code=%d\n", exitCode);
    return exitCode;
}
