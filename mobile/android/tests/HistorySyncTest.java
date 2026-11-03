package com.velaride.companion;

import java.io.IOException;

public final class HistorySyncTest {
    static final class Memory implements RideHistory.Storage {
        byte[] value;
        public byte[] read() { return value; }
        public void write(byte[] data) { value = data.clone(); }
    }
    static final class Watch implements HistorySync.Source {
        long[] list = {3, 2, 1};
        long failAt = 2;
        boolean mismatch;
        public long[] sequences() { return list; }
        public RideHistory.Entry read(long sequence) throws Exception {
            if (sequence == failAt) throw new IOException("disconnected");
            return new RideHistory.Entry("watch-a", mismatch ? sequence + 1 : sequence,
                    60, 40, 10, 1, 0, 0, 1000, 1000, "");
        }
    }
    static void check(boolean condition) { if (!condition) throw new AssertionError(); }
    public static void main(String[] args) throws Exception {
        Memory disk = new Memory(); RideHistory history = new RideHistory(disk); Watch watch = new Watch();
        try { HistorySync.sync(history, "watch-a", watch); throw new AssertionError(); }
        catch (IOException expected) { }
        check(new RideHistory(disk).snapshot().size() == 1);
        watch.failAt = -1;
        check(HistorySync.sync(history, "watch-a", watch) == 3);
        check(history.snapshot().size() == 3);
        HistorySync.sync(history, "watch-a", watch);
        check(new RideHistory(disk).snapshot().size() == 3);
        for (long[] invalid : new long[][]{{1,1},{-1},{0x100000000L},new long[17]}) {
            watch.list = invalid;
            try { HistorySync.sync(history,"watch-a",watch); throw new AssertionError(); }
            catch (IOException expected) { }
            check(history.snapshot().size() == 3);
        }
        watch.list = new long[]{4}; watch.mismatch = true;
        try { HistorySync.sync(history,"watch-a",watch); throw new AssertionError(); }
        catch (IOException expected) { }
        watch.mismatch = false;
        try { HistorySync.sync(history,"watch-b",watch); throw new AssertionError(); }
        catch (IOException expected) { }
        check(history.snapshot().size() == 3);
        System.out.println("PASS: history backfill interruption, restart/deduplication, invalid indices and wrong record/device rejection");
    }
}
