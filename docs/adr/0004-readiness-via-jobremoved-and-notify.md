# 0004 - Define action completion via JobRemoved, and readiness via Type=notify

The client treats a lifecycle action as complete only when the Manager's
**`JobRemoved`** signal fires for the job path returned by the call (matching the
path, then checking `result == "done"`), not when the D-Bus method reply
arrives. The dummy service is **`Type=notify`** and sends `READY=1` after its
init, so that job completion coincides with the application actually being ready.

We chose this because a lifecycle method reply only confirms the job was
*enqueued*, not that it finished — and for `simple`/`exec` units "active" means
merely "exec'd", which is not real readiness. Making the dummy `Type=notify` and
gating on `JobRemoved=done` yields the honest chain *job done == unit active ==
app ready*.

## Consequences

- The client must subscribe to `JobRemoved` (connect the signal **and** call
  `Subscribe()`) *before* issuing any job, or a fast job can complete before the
  handler is attached.
- `JobRemoved` fires for every job on the bus, so handlers must filter by the
  job object path they are waiting on.
- This covers only *completion of actions the client issued*. Detecting a unit
  dying on its own is a different concern (see ADR 0005).
