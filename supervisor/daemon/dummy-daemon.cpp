#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <glib-unix.h>
#include <gio/gio.h>

#include "gdbus-poc-daemon.h"

/** @brief Prefix tagging every log line emitted by this daemon. */
constexpr const char *LOG_PREFIX = "[poc-daemon]";

/** @brief Simulated initialization delay, in seconds, before readiness. */
constexpr int INIT_DELAY_SECONDS = 2;

/** @brief Exit code used for the simulated clear-application-error fault. */
constexpr int FAULT_EXIT_CODE = 42;

/** @brief Fixed object path the interface is exported at (identity is the name). */
constexpr const char *OBJECT_PATH = "/org/poc/Daemon";

/**
 * @brief Application-level readiness the daemon reports via its State property.
 *
 * Encoded on the wire as the uint32 values declared in org.poc.Daemon.xml.
 */
enum class state_t : guint
{
    initializing = 0,  ///< Process up, not yet ready.
    ready        = 1,  ///< Initialized and operating.
    degraded     = 2,  ///< Clear application error; set just before exit(N).
};

/**
 * @brief RAII owner for a file descriptor; closes it on destruction.
 */
class UniqueFd
{
public:  // Construction and access

    /** @brief Takes ownership of an open (or invalid, < 0) descriptor. */
    explicit UniqueFd(int fd)
        : m_Fd(fd)
    {}

    /** @brief Closes the descriptor if it is valid. */
    ~UniqueFd()
    {
        if (m_Fd >= 0)
        {
            close(m_Fd);
        }
    }

    UniqueFd(const UniqueFd &)            = delete;
    UniqueFd &operator=(const UniqueFd &) = delete;

    /** @brief The underlying descriptor, for passing to syscalls. */
    int get() const { return m_Fd; }

    /** @brief Whether the descriptor is valid (>= 0). */
    bool valid() const { return m_Fd >= 0; }

private:  // Attributes

    int m_Fd;  ///< The owned descriptor, or < 0 when invalid.
};

/**
 * @brief The supervised process: exports State, announces readiness, pings the
 * watchdog, and reacts to the fault/stop signals.
 *
 * GLib invokes callbacks through a C function-pointer ABI, so the callback entry
 * points are static member functions with the exact signatures GLib expects; each
 * recovers the instance from the user-data pointer and operates on it directly.
 */
class DummyDaemon
{
public:  // Construction and flow

    /**
     * @brief Constructs the daemon for a given template instance id.
     * @param pLoop        The GMainLoop the daemon runs on. Not owned.
     * @param rInstanceId  The systemd template instance id (%i), selecting the
     *                     well-known name org.poc.Daemon<id>.
     */
    DummyDaemon(GMainLoop *pLoop, const std::string &rInstanceId);

    /** @brief Removes pending timers, releases the bus name, and unrefs the skeleton. */
    ~DummyDaemon();

    DummyDaemon(const DummyDaemon &)            = delete;
    DummyDaemon &operator=(const DummyDaemon &) = delete;

    /**
     * @brief Begins owning the bus name; export and readiness follow via callbacks.
     */
    void run();

    /** @brief Process exit code, meaningful after the loop returns. */
    int exitCode() const { return m_ExitCode; }

private:  // Internal methods

    /** @brief Writes a timestamped line to stderr (captured by the journal). */
    static void logLine(const std::string &rMsg);

    /** @brief Sets the State property, which emits PropertiesChanged to the Supervisor. */
    void setState(state_t state);

    /**
     * @brief Sends a "KEY=value" assignment to NOTIFY_SOCKET (hand-rolled sd_notify).
     * @param rPayload  The assignment to send (e.g. "READY=1", "WATCHDOG=1").
     * @return True if the datagram was sent; false if not under notify or on error.
     */
    bool notify(const std::string &rPayload);

    /** @brief Starts the watchdog pinger if WATCHDOG_USEC is set by systemd. */
    void startWatchdog();

    /**
     * @brief Exports the skeleton once the bus connection is acquired.
     * @param pConnection  The acquired connection.
     * @param pName        The acquired bus name (unused).
     * @param pUserData    The DummyDaemon instance.
     */
    static void onBusAcquired(GDBusConnection *pConnection, const gchar *pName, gpointer pUserData);

    /**
     * @brief Schedules the init delay once the well-known name is acquired.
     * @param pConnection  The connection (unused).
     * @param pName        The acquired well-known name.
     * @param pUserData    The DummyDaemon instance.
     */
    static void onNameAcquired(GDBusConnection *pConnection, const gchar *pName, gpointer pUserData);

    /**
     * @brief Handles failure to connect to the bus or to own the well-known name.
     * @param pConnection  The connection, or nullptr if the bus could not be reached.
     * @param pName        The lost name.
     * @param pUserData    The DummyDaemon instance.
     */
    static void onNameLost(GDBusConnection *pConnection, const gchar *pName, gpointer pUserData);

    /**
     * @brief Marks the daemon ready after the init delay: State=ready, READY=1, watchdog.
     * @param pUserData  The DummyDaemon instance.
     * @return G_SOURCE_REMOVE; the init timer is one-shot.
     */
    static gboolean onInitComplete(gpointer pUserData);

    /**
     * @brief Sends one watchdog ping.
     * @param pUserData  The DummyDaemon instance.
     * @return G_SOURCE_CONTINUE; the pinger keeps firing.
     */
    static gboolean onWatchdogPing(gpointer pUserData);

    /**
     * @brief Handles SIGTERM: notifies STOPPING and quits the loop for a clean stop.
     * @param pUserData  The DummyDaemon instance.
     * @return G_SOURCE_REMOVE.
     */
    static gboolean onSigterm(gpointer pUserData);

    /**
     * @brief Handles SIGUSR1: sets State=degraded, flushes it to the bus, then exits non-zero.
     * @param pUserData  The DummyDaemon instance.
     * @return G_SOURCE_REMOVE (not reached; the handler calls exit).
     */
    static gboolean onSigusr1(gpointer pUserData);

private:  // Attributes

    GMainLoop       *m_pLoop;          ///< Not owned.
    GDbusPocDaemon  *m_pSkeleton;      ///< Owned; unref in dtor.
    GDBusConnection *m_pConnection;    ///< Not owned; captured on bus acquisition.
    std::string      m_WellKnownName;  ///< org.poc.Daemon<id>.
    guint            m_BusNameId;      ///< g_bus_own_name source id.
    guint            m_WatchdogId;     ///< Watchdog timeout source id; 0 if none.
    int              m_ExitCode;       ///< Final process exit code.
};

DummyDaemon::DummyDaemon(GMainLoop *pLoop, const std::string &rInstanceId)
    : m_pLoop(pLoop),
      m_pSkeleton(gdbus_poc_daemon_skeleton_new()),
      m_pConnection(nullptr),
      m_WellKnownName("org.poc.Daemon" + rInstanceId),
      m_BusNameId(0),
      m_WatchdogId(0),
      m_ExitCode(0)
{
    setState(state_t::initializing);
}

DummyDaemon::~DummyDaemon()
{
    if (m_WatchdogId != 0)
    {
        g_source_remove(m_WatchdogId);
    }
    if (m_BusNameId != 0)
    {
        g_bus_unown_name(m_BusNameId);
    }
    g_object_unref(m_pSkeleton);
}

/* static */
void DummyDaemon::logLine(const std::string &rMsg)
{
    std::time_t now = std::time(nullptr);
    char        timestamp[32];

    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    std::fprintf(stderr, "%s %s pid=%d %s\n", LOG_PREFIX, timestamp, getpid(), rMsg.c_str());
    std::fflush(stderr);
}

void DummyDaemon::setState(state_t state)
{
    gdbus_poc_daemon_set_state(m_pSkeleton, static_cast<guint>(state));
}

bool DummyDaemon::notify(const std::string &rPayload)
{
    const char *pEnv = std::getenv("NOTIFY_SOCKET");
    if (pEnv == nullptr || pEnv[0] == '\0')
    {
        /* Not launched under Type=notify (e.g. run by hand). Not an error. */
        return false;
    }

    const std::string socketPath = pEnv;

    UniqueFd fd(socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0));
    if (!fd.valid())
    {
        logLine("socket() failed for notify");
        return false;
    }

    if (socketPath.size() >= sizeof(sockaddr_un::sun_path))
    {
        logLine("NOTIFY_SOCKET path too long");
        return false;
    }

    struct sockaddr_un addr{};

    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, socketPath.data(), socketPath.size());

    socklen_t addrLen;
    if (addr.sun_path[0] == '@')
    {
        /* Abstract namespace: leading '@' becomes a NUL byte in sun_path. */
        addr.sun_path[0] = '\0';
        addrLen          = static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) + socketPath.size());
    }
    else
    {
        addrLen = static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) + socketPath.size() + 1);
    }

    ssize_t sent = sendto(fd.get(), rPayload.data(), rPayload.size(), MSG_NOSIGNAL, reinterpret_cast<struct sockaddr *>(&addr), addrLen);
    if (sent < 0)
    {
        logLine("sendto() to NOTIFY_SOCKET failed");
        return false;
    }
    return true;
}

void DummyDaemon::startWatchdog()
{
    const char *pEnv = std::getenv("WATCHDOG_USEC");
    if (pEnv == nullptr || pEnv[0] == '\0')
    {
        logLine("WATCHDOG_USEC unset; running without watchdog pings");
        return;
    }

    /* Ping at half the configured interval, as the watchdog protocol expects. */
    guint64 usec       = g_ascii_strtoull(pEnv, nullptr, 10);
    guint   intervalMs = static_cast<guint>((usec / 2) / 1000);
    if (intervalMs == 0)
    {
        intervalMs = 1;
    }
    m_WatchdogId = g_timeout_add(intervalMs, onWatchdogPing, this);
    logLine("watchdog pinger started");
}

/* static */
void DummyDaemon::onBusAcquired(GDBusConnection *pConnection, const gchar * /*pName*/, gpointer pUserData)
{
    auto *self          = static_cast<DummyDaemon *>(pUserData);
    self->m_pConnection = pConnection;

    GError *pError = nullptr;
    if (!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(self->m_pSkeleton), pConnection, OBJECT_PATH, &pError))
    {
        self->logLine(std::string("failed to export interface: ") + (pError ? pError->message : "unknown"));
        g_clear_error(&pError);
        self->m_ExitCode = 1;
        g_main_loop_quit(self->m_pLoop);
        return;
    }
    self->logLine("interface exported; State=initializing");
}

/* static */
void DummyDaemon::onNameAcquired(GDBusConnection * /*pConnection*/, const gchar *pName, gpointer pUserData)
{
    auto *self = static_cast<DummyDaemon *>(pUserData);
    self->logLine(std::string("acquired name ") + pName + "; initializing...");

    /* Simulate init work; under Type=notify the start job stays pending until the
       READY=1 that follows this delay, so job completion coincides with readiness. */
    g_timeout_add_seconds(INIT_DELAY_SECONDS, onInitComplete, self);
}

/* static */
void DummyDaemon::onNameLost(GDBusConnection *pConnection, const gchar *pName, gpointer pUserData)
{
    auto *self = static_cast<DummyDaemon *>(pUserData);
    if (pConnection == nullptr)
    {
        self->logLine("could not connect to the session bus");
    }
    else
    {
        self->logLine(std::string("lost name ") + (pName ? pName : "") + " (already owned?)");
    }
    self->m_ExitCode = 1;
    g_main_loop_quit(self->m_pLoop);
}

/* static */
gboolean DummyDaemon::onInitComplete(gpointer pUserData)
{
    auto *self = static_cast<DummyDaemon *>(pUserData);
    self->setState(state_t::ready);
    if (self->notify("READY=1"))
    {
        self->logLine("sent READY=1; State=ready");
    }
    else
    {
        self->logLine("running without notify (started manually?); State=ready");
    }
    self->startWatchdog();
    return G_SOURCE_REMOVE;
}

/* static */
gboolean DummyDaemon::onWatchdogPing(gpointer pUserData)
{
    auto *self = static_cast<DummyDaemon *>(pUserData);
    self->notify("WATCHDOG=1");
    return G_SOURCE_CONTINUE;
}

/* static */
gboolean DummyDaemon::onSigterm(gpointer pUserData)
{
    auto *self = static_cast<DummyDaemon *>(pUserData);
    self->notify("STOPPING=1");
    self->logLine("SIGTERM received; clean stop");
    g_main_loop_quit(self->m_pLoop);
    return G_SOURCE_REMOVE;
}

/* static */
gboolean DummyDaemon::onSigusr1(gpointer pUserData)
{
    auto *self = static_cast<DummyDaemon *>(pUserData);

    /* Clear application error: announce degraded, force the PropertiesChanged out
       to the bus so the Supervisor sees it, then exit non-zero so systemd records
       Result=exit-code. The two channels (State=degraded + exit-code) corroborate. */
    self->setState(state_t::degraded);
    g_dbus_interface_skeleton_flush(G_DBUS_INTERFACE_SKELETON(self->m_pSkeleton));
    if (self->m_pConnection != nullptr)
    {
        g_dbus_connection_flush_sync(self->m_pConnection, nullptr, nullptr);
    }
    self->logLine("SIGUSR1 received; State=degraded, exiting with error");
    std::exit(FAULT_EXIT_CODE);
    return G_SOURCE_REMOVE; /* not reached */
}

void DummyDaemon::run()
{
    g_unix_signal_add(SIGTERM, onSigterm, this);
    g_unix_signal_add(SIGUSR1, onSigusr1, this);

    m_BusNameId = g_bus_own_name(
        G_BUS_TYPE_SESSION, m_WellKnownName.c_str(), G_BUS_NAME_OWNER_FLAGS_NONE, onBusAcquired, onNameAcquired, onNameLost, this, nullptr);
    logLine(std::string("owning name ") + m_WellKnownName);
}

/**
 * @brief Runs one daemon instance for the template id in argv[1].
 * @param argc  Argument count.
 * @param argv  Arguments; argv[1] is the systemd template instance id (%i).
 * @return The daemon's exit code.
 */
int main(int argc, char *argv[])
{
    /* The template unit passes %i as argv[1], selecting the well-known name. */
    const std::string instanceId = (argc > 1) ? argv[1] : "0";

    GMainLoop  *pLoop = g_main_loop_new(nullptr, FALSE);
    DummyDaemon daemon(pLoop, instanceId);
    daemon.run();
    g_main_loop_run(pLoop);

    const int exitCode = daemon.exitCode();
    g_main_loop_unref(pLoop);
    return exitCode;
}
