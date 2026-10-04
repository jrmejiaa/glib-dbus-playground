#ifndef __ALARM_SERVICE_HEADER__
#define __ALARM_SERVICE_HEADER__

#include <string>

#include <gdbus-alarm.h>

/**
 * @brief Owns the Alarm skeleton and the service-side method/signal flow.
 */
class AlarmDbusServer
{
public:

    /**
     * @brief Construct the server bound to a running main loop.
     * @param pLoop The GMainLoop the service runs on; not owned.
     */
    AlarmDbusServer(GMainLoop *pLoop);

    /** @brief Release the Alarm skeleton if one was created. */
    ~AlarmDbusServer();

    AlarmDbusServer(const AlarmDbusServer &)  = delete;
    AlarmDbusServer(const AlarmDbusServer &&) = delete;

    AlarmDbusServer &operator=(const AlarmDbusServer &)  = delete;
    AlarmDbusServer &operator=(const AlarmDbusServer &&) = delete;

    /** @brief Claim the well-known bus name; export happens once acquired. */
    void run();

    /** @brief Stop the main loop so the process can exit cleanly. */
    void terminate();

    /** @brief Process exit code, meaningful after the loop returns. */
    int exitCode() const { return m_ExitCode; };

protected:  // Static callbacks

    /**
     * @brief Bus-name acquired: create the skeleton, connect handlers, export it.
     * @param connection The connection the name was acquired on.
     * @param name The acquired well-known name.
     * @param userData The AlarmDbusServer instance.
     */
    static void onNameAcquired(GDBusConnection *connection, const gchar *name, gpointer userData);

    /**
     * @brief Handle the Configure method: arm the alarm timer.
     *
     * Returns G_DBUS_METHOD_INVOCATION_HANDLED on every path that completes the
     * invocation; returning void here would make the generated dispatcher
     * double-complete the invocation (see lessons-learned/004).
     *
     * @param object The Alarm skeleton the call arrived on.
     * @param invocation The in-flight method invocation to complete.
     * @param seconds Delay, in seconds, before Beeeeeeeeep is emitted.
     * @param userData The AlarmDbusServer instance.
     * @return G_DBUS_METHOD_INVOCATION_HANDLED (TRUE).
     */
    static int onHandleConfigured(GDbusAlarm *object, GDBusMethodInvocation *invocation, guint seconds, gpointer userData);

    /**
     * @brief Timer expiry: emit Beeeeeeeeep and clear the Activated guard.
     * @param userData The AlarmDbusServer instance.
     * @return G_SOURCE_REMOVE so the one-shot timer does not repeat.
     */
    static int emitAlarmCallback(gpointer userData);

protected:

    GMainLoop  *m_pLoop;     ///< Main loop the service runs on; not owned.
    GDbusAlarm *m_pAlarm;    ///< Generated Alarm skeleton; owned (unref'd in dtor).
    int         m_ExitCode;  ///< Process exit code.
};

#endif  // __ALARM_SERVICE_HEADER__