#include <glib.h>

#include "systemd-manager.hpp"

int main()
{
    GMainLoop *loop = g_main_loop_new(nullptr, false);

    SystemdManager manager(loop);
    manager.run();

    g_main_loop_run(loop);

    const int exitCode = manager.exitCode();
    g_main_loop_unref(loop);
    return exitCode;
}
