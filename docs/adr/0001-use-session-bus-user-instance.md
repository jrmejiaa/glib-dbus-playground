# 0001 - Use the session (user) bus across the playground, not the system bus

Every step in the playground connects on the **session bus**
(`G_BUS_TYPE_SESSION`). The alarm step publishes `es.aleksander.Alarm` there and
calls it from its client; the systemd step talks to the user instance of the
Manager there and installs its unit under `~/.config/systemd/user/`, rather than
the system bus with a unit in `/etc/systemd/system/`.

We chose this because the session bus needs no root and is not gated by polkit,
so a learner can own a name, export an interface, and start/stop units without
privileges or risk to the real system. The alarm step exercises the decision
first — owning a well-known name on the session bus is the simplest possible
case — and the systemd step then reuses the same bus to stay unprivileged and
isolated from real system services. The generated code is identical for either
bus: only the `GBusType` constant (and, for systemd, the install path) differs,
so retargeting the system instance later is a one-constant change.

## Considered Options

- **System bus**: more faithful to real-world systemd management, but requires
  root to install units and triggers polkit authorization on start/stop, adding
  friction and risk to a throwaway learning project. For the alarm step it would
  also mean a system-wide bus policy file just to own the name.
- **User/session bus** (chosen): zero privilege, zero polkit, isolated, and the
  same choice works for both steps.

## Consequences

- A single bus-type constant per program (`G_BUS_TYPE_SESSION`) is the whole of
  the decision in code; both the alarm service/client and the systemd client
  share it. See ADR 0003 for where that constant lives in each class.
- As the install-side corollary (systemd step only), the systemd build defaults
  `CMAKE_INSTALL_PREFIX` to `$HOME/.local` (binaries in `~/.local/bin`, unit in
  `~/.config/systemd/user`), so `install` needs no root. An explicit
  `-DCMAKE_INSTALL_PREFIX=...` still overrides it.
