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
        // Independently encode the shipped VRH2 layout, including real GPS.
        ByteArrayOutputStream v2Bytes = new ByteArrayOutputStream();
        DataOutputStream v2 = new DataOutputStream(v2Bytes);
        v2.writeInt(0x56524832); v2.writeInt(1); v2.writeUTF("watch-v2");
        for (long value : new long[]{12, 60, 40, 10, 1, 0, 1, 1000, 2000}) v2.writeLong(value);
        v2.writeUTF("保留备注");
        for (long value : new long[]{10000, 50, 1800, 2000}) v2.writeLong(value);
        v2.flush(); CRC32 v2Crc = new CRC32(); v2Crc.update(v2Bytes.toByteArray()); v2.writeLong(v2Crc.getValue());
        Memory v2Disk = new Memory(); v2Disk.bytes = v2Bytes.toByteArray();
        byte[] originalV2 = v2Disk.bytes.clone();
        RideHistory v2History = new RideHistory(v2Disk);
        RideHistory.Entry v2Entry = v2History.snapshot().get(0);
        check(!v2Entry.safety.available() && v2Entry.gps.distanceM == 50, "V2 migration invented safety or lost GPS");
        check(Arrays.equals(v2Disk.bytes, originalV2), "V2 load rewrote disk");
        RideSafety safety = new RideSafety(1, 3, 1, 1, 1, 1, 3, 50);
        v2History.upsert(new RideHistory.Entry(v2Entry.device, 12, 60, 40, 10, 1, 0, 1, 3000, 3000,
                "", v2Entry.gps, safety));
        v2History.upsert(v2Entry.withNote("更新备注")); // An old peer must not erase safety.
        RideHistory.Entry enriched = new RideHistory(v2Disk).snapshot().get(0);
        check(v2History.snapshot().size() == 1 && enriched.safety.help == 1 && enriched.safety.unanswered == 1,
                "V3 safety enrichment/reload failed or old peer erased it");
        check(enriched.gps.distanceM == 50 && enriched.firstSync == 1000 && enriched.note.equals("更新备注"),
                "Safety enrichment lost previous metadata");
        check(RideHistory.csv(Collections.singletonList(enriched)).contains(",3,1,1,1,1,3,50,"), "CSV lost safety");
        check(RideSafety.NONE.summary().contains("旧记录") && safety.summary().contains("不表示已发送"),
                "Safety display claims delivery or invents old data");
        for (long[] bad : new long[][]{{2,0,0,0,0,0,0,0},{0,1,0,0,0,0,0,0},
                {1,1,1,1,0,0,1,1},{1,1,0,0,0,2,4,1},{1,0,0,0,0,0,3,1},
                {1,65536,0,0,0,0,0,0},{1,1,0,0,0,0,5,0}}) {
            try { new RideSafety(bad[0],bad[1],bad[2],bad[3],bad[4],bad[5],bad[6],bad[7]);
                throw new AssertionError("Invalid safety accepted"); }
            catch (IllegalArgumentException expected) { }
        }
        ByteArrayOutputStream v3Bytes=new ByteArrayOutputStream();
        DataOutputStream v3=new DataOutputStream(v3Bytes);
        v3.writeInt(0x56524833);v3.writeInt(1);v3.writeUTF("watch-v3");
        for(long value:new long[]{13,100,60,20,2,3,2,1000,2000})v3.writeLong(value);
        v3.writeUTF("旧V3备注");
        for(long value:new long[]{10000,50,1800,2000,1,2,1,0,1,1,3,90})v3.writeLong(value);
        v3.flush();CRC32 v3Crc=new CRC32();v3Crc.update(v3Bytes.toByteArray());v3.writeLong(v3Crc.getValue());
        Memory v3Disk=new Memory();v3Disk.bytes=v3Bytes.toByteArray();
        RideHistory v3History=new RideHistory(v3Disk);RideHistory.Entry v3Entry=v3History.snapshot().get(0);
        check(v3Entry.safety.help==1 && !v3Entry.extras.available(),"V3 migration lost safety/invented extras");
        RideExtras extras=new RideExtras(1,5000,4,1,1,1,2,1,1,0);
        v3History.upsert(new RideHistory.Entry(v3Entry.device,13,100,60,20,2,3,2,3000,3000,"",
                v3Entry.gps,v3Entry.safety,extras));
        v3History.upsert(v3Entry.withNote("更新V4备注"));
        RideHistory.Entry v4Entry=new RideHistory(v3Disk).snapshot().get(0);
        check(v4Entry.extras.laps==4 && v4Entry.extras.restTaken==1 && v4Entry.extras.drinkSnoozed==1,
                "V4 extras lost across old peer or restart");
        check(v4Entry.note.equals("更新V4备注") && v4Entry.firstSync==1000 && v4Entry.safety.help==1,
                "V4 enrichment lost metadata");
        check(RideHistory.csv(Collections.singletonList(v4Entry)).contains(",5000,4,1,1,1,2,1,1,0,"),"CSV lost extras");
        for(long[] bad:new long[][]{{0,1,0,0,0,0,0,0,0,0},{1,0,0,0,0,0,1,1,1,0},{1,2000001,0,0,0,0,0,0,0,0}}) {
            try {new RideExtras(bad[0],bad[1],bad[2],bad[3],bad[4],bad[5],bad[6],bad[7],bad[8],bad[9]);throw new AssertionError();}
            catch(IllegalArgumentException expected){}
        }
        try { new RideHistory.Entry("a", 1, 10, 5, 2, 0, 0, 0, 0, 0, "", RideGps.NONE, safety);
            throw new AssertionError("Out-of-ride safety timestamp accepted"); }
        catch (IllegalArgumentException expected) { }
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
        System.out.println("PASS: V1/V2/V3/V4 history migration, GPS/safety/reminder enrichment, deduplication, restart, notes, write failures, CRC, capacity and CSV");
    }
}
