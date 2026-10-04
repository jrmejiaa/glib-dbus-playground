/**
 * @file dummy-daemon.cpp
 * @brief Minimal long-running process for the systemd D-Bus PoC.
 *
 * The daemon stays alive so systemd can supervise it, letting us observe
 * start / stop / restart (and, later, liveness) events driven over D-Bus by the
 * client.
 *
 * Type=notify: systemd does NOT consider the service "started" when the binary
 * is exec'd. Instead it waits until this process sends "READY=1" over the notify
 * socket. Only then does the start job complete and ActiveState become "active".
 * That makes "job done" == "unit active" == "the app is actually ready", which
 * is the honest readiness semantics the PoC wants.
 *
 * The notify protocol is implemented by hand (no libsystemd dependency): systemd
 * puts an AF_UNIX datagram socket address in the NOTIFY_SOCKET environment
 * variable, and the service sendto()s newline-separated "KEY=value" assignments
 * to it. "READY=1" is the readiness signal.
 * 
 */

#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/** @brief Prefix tagging every log line emitted by this daemon. */
constexpr const char *LOG_PREFIX = "[dummy-daemon]";

/** @brief Size of the stack buffer holding a formatted timestamp. */
constexpr std::size_t TIMESTAMP_BUFFER_SIZE = 32;

/** @brief Simulated initialization delay, in seconds, before sending READY=1. */
constexpr int INIT_DELAY_SECONDS = 2;

/**
 * @brief The two readiness-protocol states this daemon ever sends.
 *
 * Mapped to the on-the-wire "KEY=value" string inside DummyDaemon::notify.
 */
enum class notify_state_t
{
    ready,
    stopping
};

/*
 * Set from the signal handler only. volatile sig_atomic_t is the one type the C
 * and C++ standards guarantee is safe to touch from an async signal handler, and
 * such a handler cannot carry an instance pointer, so this must live at file
 * scope rather than inside DummyDaemon.
 */
volatile sig_atomic_t g_StopRequested = 0;

/**
 * @brief Record a stop request. Installed for SIGTERM and SIGINT.
 * @param signo Unused; the same handler serves both signals.
 */
void handleSignal(int /*signo*/)
{
    g_StopRequested = 1;
}

/**
 * @brief RAII owner for a file descriptor; closes it on destruction.
 *
 * Confines the raw socket fd so every error path releases it without a manual
 * close(). Non-copyable: a fd has single ownership.
 */
class UniqueFd
{
public:

    /** @brief Take ownership of an open (or invalid, < 0) descriptor. */
    explicit UniqueFd(int fd)
        : m_Fd(fd)
    {}

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

private:

    int m_Fd;
};

/**
 * @brief The supervised process: announces readiness, waits, notifies shutdown.
 */
class DummyDaemon
{
public:

    DummyDaemon()
        : m_ExitCode(0)
    {}

    /**
     * @brief Run the full lifecycle: init delay, READY=1, wait, STOPPING=1.
     *
     * Blocks until a stop signal is observed via g_StopRequested.
     */
    void run();

    /** @brief Process exit code, meaningful after run() returns. */
    int exitCode() const { return m_ExitCode; }

private:

    /**
     * @brief Write a timestamped line to stderr.
     *
     * systemd captures stdout/stderr into the journal, so every call shows up
     * under `journalctl --user -u <unit>`.
     *
     * @param msg Message appended after the timestamp and pid.
     */
    static void logLine(const std::string &msg);

    /**
     * @brief Hand-rolled equivalent of sd_notify(0, state).
     *
     * Reads NOTIFY_SOCKET, opens an AF_UNIX datagram socket, and sendto()s the
     * state string. Handles both path sockets and the abstract namespace (an
     * address beginning with '@', which maps to a leading NUL byte in sun_path).
     *
     * @param state The readiness state to send.
     * @return true if the datagram was sent; false if not running under
     *         Type=notify or on any socket error.
     */
    bool notify(notify_state_t state);

    int m_ExitCode;
};

/* static */
void DummyDaemon::logLine(const std::string &msg)
{
    std::time_t now = std::time(nullptr);
    char        timestamp[TIMESTAMP_BUFFER_SIZE];

    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    std::fprintf(stderr, "%s %s pid=%d %s\n", LOG_PREFIX, timestamp, getpid(), msg.c_str());
    std::fflush(stderr);
}

bool DummyDaemon::notify(notify_state_t state)
{
    const char *envValue = std::getenv("NOTIFY_SOCKET");
    if (envValue == nullptr || envValue[0] == '\0')
    {
        // Not launched under Type=notify (e.g. run by hand). Not an error.
        logLine("NOTIFY_SOCKET unset; skipping readiness notification");
        return false;
    }

    /*
     * Convert the C strings to std::string once, here at the boundary, and work
     * in std::string afterwards.
     */
    const std::string socketPath = envValue;
    const std::string payload    = (state == notify_state_t::ready) ? "READY=1" : "STOPPING=1";

    UniqueFd fd(socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0));
    if (!fd.valid())
    {
        logLine("socket() failed for notify");
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;

    if (socketPath.size() >= sizeof(addr.sun_path))
    {
        logLine("NOTIFY_SOCKET path too long");
        return false;
    }

    /*
     * systemd uses either a filesystem path or the abstract namespace. In the
     * abstract case the address starts with '@', which must become a leading
     * NUL byte in sun_path (the rest is a non-NUL-terminated name). Copying into
     * the C sockaddr_un is the boundary; the length logic is std::string above.
     */
    std::memcpy(addr.sun_path, socketPath.data(), socketPath.size());
    socklen_t addrLen;
    if (addr.sun_path[0] == '@')
    {
        addr.sun_path[0] = '\0';  // abstract namespace marker
        addrLen          = static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) + socketPath.size());
    }
    else
    {
        addrLen = static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) + socketPath.size() + 1);
    }

    // MSG_NOSIGNAL: don't let a closed socket raise SIGPIPE and kill us.
    ssize_t sent = sendto(fd.get(), payload.data(), payload.size(), MSG_NOSIGNAL, reinterpret_cast<struct sockaddr *>(&addr), addrLen);

    if (sent < 0)
    {
        logLine("sendto() to NOTIFY_SOCKET failed");
        return false;
    }
    return true;
}

void DummyDaemon::run()
{
    logLine("process exec'd, initializing...");

    /*
     * Simulate real initialization work (opening resources, loading config...).
     * Under Type=notify, systemd keeps the start job pending during this window,
     * so the client's StartUnit will not complete until after this delay. This
     * is what makes the readiness semantics observable instead of instantaneous.
     */
    sleep(INIT_DELAY_SECONDS);

    if (notify(notify_state_t::ready))
    {
        logLine("sent READY=1; systemd now considers us active");
    }
    else
    {
        logLine("running without notify (started manually?)");
    }

    /*
     * Sleep until a signal arrives. pause() returns (with EINTR) on any caught
     * signal; we then re-check the flag. This burns no CPU while idle.
     */
    while (!g_StopRequested)
    {
        pause();
    }

    /*
     * Best-effort: tell systemd we are going down. Optional but tidy; systemd
     * already knows via SIGTERM, but STOPPING=1 is the symmetric notification.
     */
    notify(notify_state_t::stopping);
    logLine("SIGTERM received, shutting down cleanly");
}

int main()
{
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    DummyDaemon daemon;
    daemon.run();
    return daemon.exitCode();
}
