#ifndef DAEMON_STATE_WATCHER
#define DAEMON_STATE_WATCHER

#include <string>
#include <glib.h>

#include "../generated/gdbus-poc-daemon.h"

/**
 * @brief Copy-paste from dummy-daemon to decode the wire Dbus-signal
 * 
 */
enum class state_t : guint
{
    initializing = 0,  ///< Process up, not yet ready.
    ready        = 1,  ///< Initialized and operating.
    degraded     = 2,  ///< Clear application error; set just before exit(N).
    unknown      = 3   ///< Not state for initialization of class
};

class IDaemonStateEventSink
{
public:

    virtual void onDegradedState(const std::string unit) = 0;
};

class DaemonStateWatcher
{
public:

    DaemonStateWatcher(GDBusConnection *pConnection, const std::string &unit, IDaemonStateEventSink *pSink);
    ~DaemonStateWatcher();

protected:  // async callbacks

    static void onDbusProxyReady(GObject *source, GAsyncResult *res, gpointer userData);
    static void onStateChanged(GDbusPocDaemon *pDaemon, GParamSpec *pspec, gpointer userData);
    static void onError(const std::string &context, GError *pError);
    /**
     * @brief Writes a timestamped, prefixed line to stdout.
     * @param pFormat  printf-style format string for the message.
     */
    static void log(const char *pFormat, ...) __attribute__((format(printf, 1, 2)));
    /**
     * @brief Derives a daemon's well-known bus name from its systemd unit name.
     *
     * Extracts the template instance id between '@' and ".service" and appends it to
     * the daemon name prefix (e.g. "poc-dummy@1.service" yields "org.poc.Daemon1").
     * @param unit  The template unit instance name (e.g. "poc-dummy@1.service").
     * @return The well-known name the matching daemon owns on the session bus.
     */
    static std::string getWellKnownNameOf(const std::string unit);

    static std::string getStateName(state_t state);

    /**
     * @brief Reads the daemon's current State, distinguishing an unpopulated cache.
     *
     * The generated getter returns 0 (initializing) for an empty cache, which would
     * mask an unowned well-known name as a plausible state. This probes the cache
     * directly so that case is reported as unknown instead.
     * @param pDaemon  The daemon proxy to read from.
     * @return The decoded state, or state_t::unknown when the proxy has no cached State.
     */
    static state_t readState(GDbusPocDaemon *pDaemon);

protected:

    GDBusConnection       *m_pConnection;
    IDaemonStateEventSink *m_pSink;

    GDbusPocDaemon   *m_pDbusDaemon;
    const std::string m_UnitName;
    const std::string m_WellKnownName;
    state_t           m_State;
};

#endif  // DAEMON_STATE_WATCHER
