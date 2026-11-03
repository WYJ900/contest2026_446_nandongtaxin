package com.velaride.companion;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.zip.CRC32;

/** Phone-owned history. No fabricated ride date, GPS or watch identity. */
public final class RideHistory {
    public static final int MAX_RECORDS = 5000, MAX_BYTES = 4 * 1024 * 1024;
    private static final int MAGIC_V1 = 0x56524831, MAGIC = 0x56524832;
    public interface Storage {
        byte[] read() throws IOException; // null means absent, not unreadable
        void write(byte[] data) throws IOException; // commit atomically or throw
    }
    public static final class Entry {
        public final String device, note;
        public final long sequence, elapsed, moving, still, pauses, rests, impacts, firstSync, lastSync;
        public final RideGps gps;
        public Entry(String device, long sequence, long elapsed, long moving, long still,
                     long pauses, long rests, long impacts, long firstSync, long lastSync, String note) {
            this(device, sequence, elapsed, moving, still, pauses, rests, impacts, firstSync, lastSync, note, RideGps.NONE);
        }
        public Entry(String device, long sequence, long elapsed, long moving, long still,
                     long pauses, long rests, long impacts, long firstSync, long lastSync, String note, RideGps gps) {
            if (device == null || device.isEmpty() || device.length() > 64 || note == null
                    || note.getBytes(StandardCharsets.UTF_8).length > 1024 || firstSync < 0 || lastSync < firstSync)
                throw new IllegalArgumentException("骑行历史字段异常");
            for (long value : new long[]{sequence, elapsed, moving, still, pauses, rests, impacts})
                if (value < 0 || value > 0xffffffffL) throw new IllegalArgumentException("骑行历史数值异常");
            if (moving + still > elapsed) throw new IllegalArgumentException("骑行历史时长不一致");
            this.device = device; this.sequence = sequence; this.elapsed = elapsed; this.moving = moving;
            this.still = still; this.pauses = pauses; this.rests = rests; this.impacts = impacts;
            if (gps == null) throw new IllegalArgumentException("骑行 GPS 数据缺失");
            this.firstSync = firstSync; this.lastSync = lastSync; this.note = note; this.gps = gps;
        }
        public Entry withNote(String value) {
            return new Entry(device, sequence, elapsed, moving, still, pauses, rests, impacts, firstSync, lastSync, value, gps);
        }
        // Sequence can restart after a factory reset. Keep different summaries with the same number.
        public boolean sameRide(Entry other) {
            return device.equals(other.device) && sequence == other.sequence && elapsed == other.elapsed
                    && moving == other.moving && still == other.still && pauses == other.pauses
                    && rests == other.rests && impacts == other.impacts;
        }
    }
    private final Storage storage;
    private List<Entry> entries;
    public RideHistory(Storage storage) throws IOException {
        this.storage = storage;
        byte[] data = storage.read();
        entries = data == null ? new ArrayList<>() : decode(data);
    }
    public synchronized List<Entry> snapshot() { return new ArrayList<>(entries); }
    public synchronized void upsert(Entry incoming) throws IOException {
        List<Entry> next = new ArrayList<>(entries);
        for (int i = 0; i < next.size(); i++) {
            Entry old = next.get(i);
            if (!old.sameRide(incoming)) continue;
            incoming = new Entry(old.device, old.sequence, old.elapsed, old.moving, old.still,
                    old.pauses, old.rests, old.impacts, old.firstSync == 0 ? incoming.firstSync : old.firstSync,
                    Math.max(old.lastSync, incoming.lastSync), incoming.note.isEmpty() ? old.note : incoming.note,
                    incoming.gps.available() ? incoming.gps : old.gps);
            next.remove(i); break;
        }
        if (next.size() >= MAX_RECORDS) throw new IOException("历史已达 5000 条，暂不能新增；可导出备份，已有记录仍可查看。");
        next.add(incoming);
        next.sort((a, b) -> Long.compare(b.firstSync, a.firstSync));
        storage.write(encode(next));
        entries = next; // A failed write must not appear as saved in the UI.
    }
    static byte[] encode(List<Entry> entries) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        DataOutputStream out = new DataOutputStream(bytes);
        out.writeInt(MAGIC); out.writeInt(entries.size());
        for (Entry e : entries) {
            out.writeUTF(e.device);
            for (long value : new long[]{e.sequence, e.elapsed, e.moving, e.still, e.pauses, e.rests,
                    e.impacts, e.firstSync, e.lastSync}) out.writeLong(value);
            out.writeUTF(e.note);
            out.writeLong(e.gps.timeMs); out.writeLong(e.gps.distanceM);
            out.writeLong(e.gps.avgCenti); out.writeLong(e.gps.maxCenti);
        }
        out.flush(); CRC32 crc = new CRC32(); crc.update(bytes.toByteArray()); out.writeLong(crc.getValue());
        byte[] result = bytes.toByteArray();
        if (result.length > MAX_BYTES) throw new IOException("骑行历史文件过大，原记录已保留。");
        return result;
    }
    static List<Entry> decode(byte[] bytes) throws IOException {
        if (bytes.length < 16 || bytes.length > MAX_BYTES) throw new IOException("骑行历史文件长度异常");
        CRC32 crc = new CRC32(); crc.update(bytes, 0, bytes.length - 8);
        DataInputStream in = new DataInputStream(new ByteArrayInputStream(bytes));
        int magic = in.readInt();
        if (magic != MAGIC && magic != MAGIC_V1) throw new IOException("无法识别骑行历史版本");
        int count = in.readInt();
        if (count < 0 || count > MAX_RECORDS) throw new IOException("骑行历史数量异常");
        List<Entry> result = new ArrayList<>();
        try {
            for (int i = 0; i < count; i++) {
                Entry entry = new Entry(in.readUTF(), in.readLong(), in.readLong(),
                    in.readLong(), in.readLong(), in.readLong(), in.readLong(), in.readLong(),
                    in.readLong(), in.readLong(), in.readUTF());
                RideGps gps = magic == MAGIC_V1 ? RideGps.NONE : new RideGps(in.readLong(), in.readLong(), in.readLong(), in.readLong());
                result.add(new Entry(entry.device, entry.sequence, entry.elapsed, entry.moving, entry.still,
                        entry.pauses, entry.rests, entry.impacts, entry.firstSync, entry.lastSync, entry.note, gps));
            }
        } catch (IllegalArgumentException error) { throw new IOException("骑行历史内容异常", error); }
        if (in.readLong() != crc.getValue() || in.available() != 0) throw new IOException("骑行历史校验失败");
        return result;
    }
    private static String cell(String value) {
        // Quoting alone does not prevent spreadsheet formula execution.
        String trimmed = value.trim();
        if (!trimmed.isEmpty() && ("=+-@".indexOf(trimmed.charAt(0)) >= 0
                || value.charAt(0) == '\t' || value.charAt(0) == '\r' || value.charAt(0) == '\n')) value = "'" + value;
        return "\"" + value.replace("\"", "\"\"") + "\"";
    }
    public static String csv(List<Entry> entries) {
        StringBuilder csv = new StringBuilder("\ufeffwatch,sequence,elapsed_s,moving_s,still_s,pause_count,rest_count,impacts,first_sync_epoch_ms,last_sync_epoch_ms,gps_time_ms,gps_distance_m,gps_avg_centi_kmh,gps_max_centi_kmh,note\r\n");
        for (Entry e : entries) csv.append(cell(e.device)).append(',').append(e.sequence).append(',')
                .append(e.elapsed).append(',').append(e.moving).append(',').append(e.still).append(',')
                .append(e.pauses).append(',').append(e.rests).append(',').append(e.impacts).append(',')
                .append(e.firstSync).append(',').append(e.lastSync).append(',')
                .append(e.gps.available() ? e.gps.timeMs : "").append(',')
                .append(e.gps.available() ? e.gps.distanceM : "").append(',')
                .append(e.gps.available() ? e.gps.avgCenti : "").append(',')
                .append(e.gps.available() ? e.gps.maxCenti : "").append(',').append(cell(e.note)).append("\r\n");
        return csv.toString();
    }
}
