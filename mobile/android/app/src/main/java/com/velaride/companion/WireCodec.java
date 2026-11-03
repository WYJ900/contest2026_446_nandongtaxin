package com.velaride.companion;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Existing FFF0 protocol: complete UTF-8 JSON lines, conservative ATT writes. */
public final class WireCodec {
    private final ByteArrayOutputStream input = new ByteArrayOutputStream();

    public static List<byte[]> frame(String json) {
        byte[] bytes = (json + "\n").getBytes(StandardCharsets.UTF_8);
        if (bytes.length > 767) throw new IllegalArgumentException("内容超过手表接收上限");
        List<byte[]> result = new ArrayList<>();
        for (int offset = 0; offset < bytes.length; offset += 18) {
            result.add(Arrays.copyOfRange(bytes, offset, Math.min(offset + 18, bytes.length)));
        }
        return result;
    }

    public synchronized List<String> accept(byte[] bytes) {
        List<String> result = new ArrayList<>();
        for (byte value : bytes) {
            if (value == '\n') {
                String line = new String(input.toByteArray(), StandardCharsets.UTF_8).trim();
                input.reset();
                if (!line.isEmpty()) result.add(line);
            } else {
                if (input.size() >= 4096) {
                    input.reset();
                    throw new IllegalArgumentException("手表回复过长，请重新连接");
                }
                input.write(value);
            }
        }
        return result;
    }

    public synchronized void reset() { input.reset(); }
}
