package com.velaride.companion;

import java.io.IOException;
import java.util.HashSet;
import java.util.Set;

/** Restartable read-only backfill; each successful record is atomically saved. */
public final class HistorySync {
    public interface Source {
        long[] sequences() throws Exception;
        RideHistory.Entry read(long sequence) throws Exception;
    }
    public static int sync(RideHistory destination, String device, Source source) throws Exception {
        long[] sequences = source.sequences();
        if (sequences == null || sequences.length > 16) throw new IOException("手表历史清单异常");
        Set<Long> seen = new HashSet<>();
        for (long sequence : sequences)
            if (sequence < 0 || sequence > 0xffffffffL || !seen.add(sequence))
                throw new IOException("手表历史序号异常");
        int saved = 0;
        for (long sequence : sequences) {
            RideHistory.Entry entry = source.read(sequence);
            if (entry == null || entry.sequence != sequence || !entry.device.equals(device))
                throw new IOException("手表历史已变化，请重新补传");
            destination.upsert(entry);
            saved++;
        }
        return saved;
    }
    private HistorySync() { }
}
