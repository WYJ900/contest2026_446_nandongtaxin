package com.velaride.companion;

/**
 * Turns successive watch ride_status replies into emergency decisions.
 * The watch owns detection (impact followed by stillness) and its own
 * 30-second confirmation; the phone only escalates when that confirmation
 * expires unanswered or the rider explicitly chooses "need help".
 */
public final class FallAlert {
    public static final int NONE = 0, WARN = 1, ARM = 2, CANCEL = 3, UNSUPPORTED = 4;
    public static final int ACTION_OK = 1, ACTION_PAUSE = 2, ACTION_HELP = 3, ACTION_UNANSWERED = 4;
    public enum Reason { UNANSWERED, HELP, DRILL }

    public static final class Decision {
        public final int kind; public final Reason reason; public final int remaining;
        Decision(int kind, Reason reason, int remaining) { this.kind = kind; this.reason = reason; this.remaining = remaining; }
    }
    private static final Decision QUIET = new Decision(NONE, null, 0);

    private boolean baselineSet, episodeFired, wasPending, wasActive, resumed;
    private long helpBaseline;
    private Reason armed;

    /**
     * The BLE link dropped. Keep the help baseline: a "need help" pressed while
     * the link was down must still escalate once the phone reconnects.
     */
    public synchronized void reset() { resumed = baselineSet; episodeFired = false; wasPending = false; armed = null; }
    /** The phone countdown finished, was cancelled, or was dispatched. */
    public synchronized void disarm() { armed = null; }
    public synchronized boolean isArmed() { return armed != null; }

    public synchronized Decision update(boolean supported, boolean active, boolean pending, int remaining,
                                        long help, int lastAction) {
        if (!supported) return new Decision(UNSUPPORTED, null, 0);
        if (!baselineSet || (active && !wasActive && !resumed)) {
            // Never escalate an action that happened before this phone was watching.
            baselineSet = true; helpBaseline = help; episodeFired = false;
        }
        wasActive = active; resumed = false;
        if (help < helpBaseline) helpBaseline = help;   // new ride resets the counter
        if (active && help > helpBaseline && lastAction == ACTION_HELP) {
            helpBaseline = help;
            armed = Reason.HELP;
            return new Decision(ARM, Reason.HELP, 0);
        }
        if (active && pending) {
            wasPending = true;
            if (remaining <= 0 && !episodeFired) {
                episodeFired = true; armed = Reason.UNANSWERED;
                return new Decision(ARM, Reason.UNANSWERED, 0);
            }
            return remaining > 0 ? new Decision(WARN, null, remaining) : QUIET;
        }
        boolean responded = wasPending && (lastAction == ACTION_OK || lastAction == ACTION_PAUSE);
        wasPending = false; episodeFired = false;
        if (armed == Reason.UNANSWERED && (responded || !active)) {
            armed = null;
            return new Decision(CANCEL, Reason.UNANSWERED, 0);
        }
        return QUIET;
    }
}
