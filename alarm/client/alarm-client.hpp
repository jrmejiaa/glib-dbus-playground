#ifndef __ALARM_CLIENT_HEADER__
#define __ALARM_CLIENT_HEADER__

#include <string>

#include <gdbus-alarm.h>

/**
 * @brief Owns the Alarm proxy and drives the client-side method/signal flow.
 */
class AlarmDbusClient
{
public:

    /**
     * @brief Constructs the client bound to a running main loop.
     * @param pLoop  The GMainLoop the client runs on; not owned.
     */
    AlarmDbusClient(GMainLoop *pLoop);

    /** @brief Releases the Alarm proxy if one was created. */
    ~AlarmDbusClient();

    AlarmDbusClient(const AlarmDbusClient &)  = delete;
    AlarmDbusClient(const AlarmDbusClient &&) = delete;

    AlarmDbusClient &operator=(const AlarmDbusClient &)  = delete;
    AlarmDbusClient &operator=(const AlarmDbusClient &&) = delete;

    /** @brief Connects to the bus; proxy creation and the Configure call follow once connected. */
    void run();

    /** @brief Stops the main loop so the process can exit cleanly. */
    void terminate();

    /** @brief Process exit code, meaningful after the loop returns. */
    int exitCode() const { return m_ExitCode; };

protected:  // Static callbacks

    /**
     * @brief Bus connection ready: stores the connection and requests the Alarm proxy, or terminates on failure.
     * @param source_object  Unused GObject source of the async result.
     * @param res  Completed async result carried to g_bus_get_finish().
     * @param userData  The AlarmDbusClient instance.
     */
    static void onGDbusConnectionReady(GObject *source_object, GAsyncResult *res, gpointer userData);

    /**
     * @brief Proxy ready: connects the Beeeeeeeeep signal and issues the Configure call, or terminates on failure.
     * @param source_object  Unused GObject source of the async result.
     * @param res  Completed async result carried to gdbus_alarm_proxy_new_finish().
     * @param userData  The AlarmDbusClient instance.
     */
    static void onDbusProxyReady(GObject *source_object, GAsyncResult *res, gpointer userData);

    /**
     * @brief Configure reply received: logs success and waits for the beep, or terminates on failure.
     * @param source_object  Unused GObject source of the async result.
     * @param res  Completed async result carried to gdbus_alarm_call_configure_finish().
     * @param userData  The AlarmDbusClient instance.
     */
    static void onConfiguredReady(GObject *source_object, GAsyncResult *res, gpointer userData);

    /**
     * @brief Beeeeeeeeep signal received: stops the loop so the client exits.
     * @param alarm  The Alarm proxy that emitted the signal.
     * @param userData  The AlarmDbusClient instance.
     */
    static void onBeep(GDbusAlarm *alarm, gpointer userData);

protected:

    GMainLoop       *m_pLoop;        ///< Main loop the client runs on; not owned.
    GDBusConnection *m_pConnection;  ///< Bus connection obtained via g_bus_get; owned by GDBus.
    GDbusAlarm      *m_pAlarm;       ///< Generated Alarm proxy; owned (unref'd in dtor).
    int              m_ExitCode;     ///< Process exit code.
};

#endif  // __ALARM_CLIENT_HEADER__