#pragma once

class StateAPI;

// Ships rescued prior-session logs (/tmp/performance.log.*.prev) to the
// telemetry ingest endpoint. Fire-and-forget on a background thread so
// startup isn't delayed. On successful ship, the .prev file is deleted;
// on failure it's left for a subsequent startup to retry.
//
// The shipping endpoint and bearer token come from BuildConfig.h, which
// is generated at build time from keys/telemetry.json. If either is
// empty (no keys file on this machine), the shipper is a silent no-op.
namespace Telemetry {
    // Enqueue shipping of any .prev files on a background thread.
    // Respects the "Send diagnostics" toggle in Settings.
    void shipPrevLogs(StateAPI& state);

    // Settings toggle — persisted in state.db config. Default on.
    bool isEnabled(StateAPI& state);
    void setEnabled(StateAPI& state, bool enabled);
}
