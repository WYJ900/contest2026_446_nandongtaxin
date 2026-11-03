package com.velaride.companion;

import java.io.IOException;

/** The basic companion works on the frozen watch firmware, without GPS extensions. */
public final class WatchHandshake {
    public interface Commands {
        void send(String command, String expected) throws Exception;
    }

    public static class CommandError extends IOException {
        public CommandError(String message) { super(message); }
    }

    private WatchHandshake() { }

    /** Clear stale GPS only when supported; propagate all transport/other errors. */
    public static boolean prepare(Commands commands) throws Exception {
        commands.send("ping", "pong");
        try {
            commands.send("phone_clear", "phone_clear");
            return true;
        } catch (CommandError error) {
            if (!"unknown_cmd".equals(error.getMessage())) throw error;
            return false;
        }
    }
}
