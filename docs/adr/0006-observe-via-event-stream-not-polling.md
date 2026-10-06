# 0006 - Observe unit state via the D-Bus event stream, not property polling

The Supervisor learns what happened to each unit by consuming systemd's **signals**
— the Manager's `UnitNew`/`UnitRemoved` and each unit's `PropertiesChanged` — and
reads every value **out of the signal payload or the proxy cache**. It never reacts
to a signal by polling the property back with a fresh `Get`.

The spine is the value-gated **`ActiveState`** transition (via the per-unit
`UnitMonitor`), enriched by `Result`/`ExecMainStatus`/`NRestarts`. `UnitNew`/
`UnitRemoved` drive proxy attach/detach and the clean-stop revive (ADR 0007).

We record the no-polling rule because polling looks adequate and is the obvious
first instinct, so a reader will wonder why the code never re-reads a property it
just heard about. The reason: `Result`/`ExecMainStatus` are last-run snapshots.
With auto-restart on, a crashed unit is restarted within milliseconds and the fresh
run resets those properties, so a poll that lands after the restart reads
`success`/`0` and the failure cause is lost. A subscribed signal instead carries
the value *as it was at emission time*, frozen in the message, and the later
overwrite cannot reach into an already-queued message.

## Considered options

- **Poll properties after a change notification.** Rejected: race-prone exactly at
  the moment that matters (a fast failure+restart), as observed on systemd 252.
- **Force a stable `failed` window** (e.g. no auto-restart) so a poll has time to
  land. Rejected as unnecessary — the event model is lossless — and it would distort
  the lifecycle we want to observe.
- **Use `JobRemoved` as the spine.** Rejected for step-3: the Supervisor observes
  systemd's *autonomous* behavior and issues no job it needs to correlate (the one
  lifecycle call, the clean-stop revive, is fire-and-forget). `JobRemoved` is a
  step-2 concept, intuitive where the client issues a job and awaits it; forcing it
  into an observer role adds a systemd-internal concept with no benefit here.

## Consequences

- D-Bus queues signals on the connection socket and delivers them in order; a busy
  or briefly blocked event loop *delays* processing but does not *drop* messages. A
  probe that blocked its loop for 1s across a 100ms-restart crash still received the
  full `failed` → `activating` → `active` sequence with `Result=signal`,
  `ExecMainStatus=9`, and `NRestarts` incrementing — intact and in order.
- `ActiveState` transitions are a sufficient spine on their own: a tight crash loop
  (`RestartSec=0`, 15 consecutive `SIGKILL`s) produced 15 restarts and 15 visible
  `failed` edges — no gap. `PropertiesChanged` can coalesce several changes within
  one emit cycle, but it never coalesced away a `failed` transition even at the
  fastest restart systemd allows.
- `ActiveState`/`Result` must be **value-gated**: systemd re-announces an unchanged
  `ActiveState` several times per restart, and neither the `PropertiesChanged`
  payload nor a typed proxy's `notify::` is edge-triggered on value (see lesson 006).
  The Supervisor keeps the last value per unit and acts only on a real transition.
- The Supervisor must call the Manager's `Subscribe()` or systemd emits no signals
  at all (see ADR 0004); an eavesdropper such as `dbus-monitor` never triggers them.
- Discipline for implementers: read `Result`/`ExecMainStatus`/`NRestarts` from the
  `PropertiesChanged` payload or the proxy cache inside the ordered change callback;
  never re-`Get` them after being woken.
