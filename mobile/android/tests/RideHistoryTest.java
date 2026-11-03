package com.velaride.companion;

import java.io.*;
import java.util.*;
import java.util.zip.CRC32;

public final class RideHistoryTest {
    private static final class Memory implements RideHistory.Storage {
        byte[] bytes; boolean fail;
        public byte[] read() { return bytes == null ? null : bytes.clone(); }
        public void write(byte[] value) throws IOException {
            if (fail) throw new IOException("disk full");
            bytes = value.clone();
        }
    }
    private static RideHistory.Entry ride(String watch, long sequence, long duration, long synced, String note) {
        return new RideHistory.Entry(watch, sequence, duration, duration / 2, duration / 4, 1, 2, 3, synced, synced, note);
    }
    private static void check(boolean value, String reason) { if (!value) throw new AssertionError(reason); }
    private static void rejects(byte[] bytes) throws Exception {
        try { RideHistory.decode(bytes); throw new AssertionError("Corrupt file accepted"); }
        catch (IOException expected) { }
    }
    public static void main(String[] args) throws Exception {
        // Encode the frozen V1 layout independently of the production V2 writer.
        ByteArrayOutputStream legacyBytes = new ByteArrayOutputStream();
        DataOutputStream legacy = new DataOutputStream(legacyBytes);
        legacy.writeInt(0x56524831); legacy.writeInt(1); legacy.writeUTF("watch-legacy");
        for (long value : new long[]{11, 23, 19, 1, 1, 0, 2, 1000, 2000}) legacy.writeLong(value);
        legacy.writeUTF("旧版备注"); legacy.flush();
        CRC32 legacyCrc = new CRC32(); legacyCrc.update(legacyBytes.toByteArray()); legacy.writeLong(legacyCrc.getValue());
        Memory legacyDisk = new Memory(); legacyDisk.bytes = legacyBytes.toByteArray();
        byte[] original = legacyDisk.bytes.clone();
        RideHistory migrated = new RideHistory(legacyDisk);
        check(!migrated.snapshot().get(0).gps.available(), "Legacy record invented GPS");
        check(Arrays.equals(original, legacyDisk.bytes), "Read-only migration rewrote old file");
        RideHistory.Entry legacyEntry = migrated.snapshot().get(0);
        migrated.upsert(new RideHistory.Entry(legacyEntry.device, 11, 23, 19, 1, 1, 0, 2,
                3000, 3000, "", new RideGps(10000, 50, 1800, 2000)));
        check(migrated.snapshot().size() == 1, "GPS enrichment duplicated old ride");
        check(migrated.snapshot().get(0).note.equals("旧版备注"), "GPS enrichment lost note");
        migrated.upsert(legacyEntry.withNote("新备注"));
        RideHistory.Entry gpsEntry = new RideHistory(legacyDisk).snapshot().get(0);
        check(gpsEntry.gps.distanceM == 50 && gpsEntry.gps.timeMs == 10000 && gpsEntry.gps.avgCenti == 1800
                && gpsEntry.gps.maxCenti == 2000, "Reload or old peer erased GPS");
        check(RideHistory.csv(Collections.singletonList(gpsEntry)).contains(",10000,50,1800,2000,"), "CSV lost recorded GPS");
        check(!RideGps.NONE.available() && new RideGps(5000, 0, 0, 0).available(), "Unknown and stationary confused");
        for (long[] bad : new long[][]{{0,1,0,0},{-1,0,0,0},{1,0,20001,0},{1,0,0,20001}}) {
            try { new RideGps(bad[0],bad[1],bad[2],bad[3]); throw new AssertionError("Invalid GPS accepted"); }
            catch (IllegalArgumentException expected) { }
        }
        Memory disk = new Memory(); RideHistory history = new RideHistory(disk);
        check(history.snapshot().isEmpty(), "Empty storage fabricated records");
        history.upsert(ride("watch-a", 11, 100, 1000, ""));
        history.upsert(ride("watch-a", 11, 100, 2000, "骑行备注，\"清楚\"\n下一行"));
        history.upsert(ride("watch-a", 11, 100, 3000, ""));
        check(history.snapshot().size() == 1, "Repeated sync duplicated a ride");
        RideHistory.Entry first = history.snapshot().get(0);
        check(first.firstSync == 1000 && first.lastSync == 3000 && !first.note.isEmpty(), "Sync lost original time/note");
        history.upsert(ride("watch-b", 11, 100, 4000, ""));
        history.upsert(ride("watch-a", 11, 120, 5000, ""));
        history.upsert(ride("watch-a", 12, 100, 6000, ""));
        check(history.snapshot().size() == 4, "Device or reset sequence collision");
        check(history.snapshot().get(0).sequence == 12, "Wrong order");
        RideHistory reopened = new RideHistory(disk);
        check(reopened.snapshot().size() == 4 && reopened.snapshot().get(3).note.equals(first.note), "Restart lost Unicode/note");
        List<RideHistory.Entry> isolated = reopened.snapshot(); isolated.clear();
        check(reopened.snapshot().size() == 4, "Mutable snapshot leaked");
        byte[] beforeFailure = disk.bytes.clone(); disk.fail = true;
        try { reopened.upsert(ride("watch-a", 13, 60, 7000, "")); throw new AssertionError("Write failure hidden"); }
        catch (IOException expected) { }
        check(reopened.snapshot().size() == 4 && Arrays.equals(beforeFailure, disk.bytes), "Failed save changed records");
        disk.fail = false;
        history.upsert(ride("legacy-unknown", 9, 40, 0, ""));
        check(new RideHistory(disk).snapshot().get(4).firstSync == 0, "Unknown date fabricated");
        byte[] valid = disk.bytes.clone();
        for (int length = 0; length < valid.length; length++) rejects(Arrays.copyOf(valid, length));
        byte[] corrupt = valid.clone(); corrupt[20] ^= 1; rejects(corrupt);
        corrupt = valid.clone(); corrupt[0] ^= 1; rejects(corrupt);
        rejects(Arrays.copyOf(valid, valid.length + 1));
        disk.bytes = corrupt;
        try { new RideHistory(disk); throw new AssertionError("Corrupt history silently reset"); }
        catch (IOException expected) { }
        check(Arrays.equals(disk.bytes, corrupt), "Read failure overwrote original");
        String csv = RideHistory.csv(Collections.singletonList(first));
        check(csv.startsWith("\ufeffwatch,sequence,"), "Missing Excel UTF-8 marker/columns");
        check(csv.contains("\"骑行备注，\"\"清楚\"\"\n下一行\"\r\n"), "CSV escaping lost Unicode/quotes/newline");
        for (String unsafe : new String[]{"=1+1", "+1", "-1", "@SUM(A1)", "  =cmd", "\t=1", "\rabc", "\nabc"}) {
            check(RideHistory.csv(Collections.singletonList(first.withNote(unsafe))).contains("\"'" + unsafe + "\""), "CSV formula not escaped");
        }
        List<RideHistory.Entry> full = new ArrayList<>();
        for (int i = 0; i < RideHistory.MAX_RECORDS; i++) full.add(ride("watch-a", i, 100, i, ""));
        disk.bytes = RideHistory.encode(full); RideHistory capacity = new RideHistory(disk);
        capacity.upsert(ride("watch-a", 0, 100, 9000, "updated"));
        try { capacity.upsert(ride("watch-a", 6000, 100, 9001, "")); throw new AssertionError("Capacity silently evicted records"); }
        catch (IOException expected) { }
        check(new RideHistory(disk).snapshot().size() == RideHistory.MAX_RECORDS, "Capacity lost history");
        try { new RideHistory.Entry("a", 1, 10, 8, 8, 0, 0, 0, 0, 0, ""); throw new AssertionError("Invalid durations accepted"); }
        catch (IllegalArgumentException expected) { }
        System.out.println("PASS: history deduplication, watch/reset isolation, V1/V2 GPS migration, enrichment, restart, notes, write failure, corruption, capacity and CSV escaping");
    }
}
