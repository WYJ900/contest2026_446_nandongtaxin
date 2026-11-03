package com.velaride.companion;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public final class WatchHandshakeTest {
    private static final class Watch implements WatchHandshake.Commands {
        final List<String> sent = new ArrayList<>();
        final Exception failure;
        final String failAt;
        Watch(String failAt, Exception failure) { this.failAt = failAt; this.failure = failure; }
        public void send(String command, String expected) throws Exception {
            sent.add(command);
            if (!expected.equals(command.equals("ping") ? "pong" : command))
                throw new AssertionError("Incorrect event matching");
            if (command.equals(failAt)) throw failure;
        }
    }

    public static void main(String[] args) throws Exception {
        Watch legacy = new Watch("phone_clear", new WatchHandshake.CommandError("unknown_cmd"));
        if (WatchHandshake.prepare(legacy)) throw new AssertionError("Legacy firmware enabled GPS");
        legacy.send("time_sync", "time_sync");
        legacy.send("ride_status", "ride_status");
        legacy.send("ride_latest", "ride_latest");
        if (!legacy.sent.equals(Arrays.asList("ping", "phone_clear", "time_sync", "ride_status", "ride_latest")))
            throw new AssertionError("Legacy basic synchronization stopped");

        Watch modern = new Watch(null, null);
        if (!WatchHandshake.prepare(modern)) throw new AssertionError("GPS firmware not recognized");
        if (!modern.sent.equals(Arrays.asList("ping", "phone_clear"))) throw new AssertionError();

        for (Exception failure : new Exception[]{new IOException("timeout"),
                new WatchHandshake.CommandError("busy"), new WatchHandshake.CommandError("bad_json")}) {
            Watch broken = new Watch("phone_clear", failure);
            try { WatchHandshake.prepare(broken); throw new AssertionError("Failure swallowed"); }
            catch (Exception actual) { if (actual != failure) throw new AssertionError("Failure changed", actual); }
        }
        Watch noPing = new Watch("ping", new WatchHandshake.CommandError("unknown_cmd"));
        try { WatchHandshake.prepare(noPing); throw new AssertionError("Ping error swallowed"); }
        catch (WatchHandshake.CommandError expected) { }
        if (!noPing.sent.equals(Arrays.asList("ping"))) throw new AssertionError("Continued after failed ping");
        System.out.println("PASS: legacy/modern handshake, basic sync after unknown_cmd, transport and other errors preserved");
    }
}
