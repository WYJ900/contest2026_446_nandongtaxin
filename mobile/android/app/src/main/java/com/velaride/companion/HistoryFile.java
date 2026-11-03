package com.velaride.companion;

import android.util.AtomicFile;
import java.io.*;
import java.util.Arrays;

/** Private app storage; caller serializes access through RideHistory. */
final class HistoryFile implements RideHistory.Storage {
    private final AtomicFile file;
    HistoryFile(File directory) { file = new AtomicFile(new File(directory, "rides-v1.bin")); }
    @Override public byte[] read() throws IOException {
        if (!file.getBaseFile().exists() && !new File(file.getBaseFile() + ".bak").exists()) return null;
        try (InputStream input = file.openRead(); ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096]; int count;
            while ((count = input.read(buffer)) != -1) {
                if (output.size() + count > RideHistory.MAX_BYTES) throw new IOException("骑行历史文件过大");
                output.write(buffer, 0, count);
            }
            return output.toByteArray();
        }
    }
    @Override public void write(byte[] data) throws IOException {
        FileOutputStream output = file.startWrite();
        try {
            output.write(data); output.getFD().sync(); file.finishWrite(output);
        } catch (IOException error) { file.failWrite(output); throw error; }
        // AtomicFile.finishWrite can log a failure without throwing on some Android versions.
        if (!Arrays.equals(data, read())) throw new IOException("骑行历史保存未完成，请重试。");
    }
}
