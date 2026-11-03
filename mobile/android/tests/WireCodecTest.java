package com.velaride.companion;

import java.nio.charset.StandardCharsets;
import java.util.*;

public final class WireCodecTest {
    public static void main(String[] args) {
        String line = "{\"ok\":true,\"event\":\"ride_review\",\"text\":\"你好手表，今天继续骑行\"}";
        byte[] all = (line + "\n\n{\"ok\":true}\n").getBytes(StandardCharsets.UTF_8);
        for (int width = 1; width <= 25; width++) {
            WireCodec decoder = new WireCodec();
            List<String> results = new ArrayList<>();
            for (int offset = 0; offset < all.length; offset += width)
                results.addAll(decoder.accept(Arrays.copyOfRange(all, offset, Math.min(offset + width, all.length))));
            if (!results.equals(Arrays.asList(line, "{\"ok\":true}"))) throw new AssertionError("UTF-8/framing width " + width);
        }
        List<byte[]> chunks = WireCodec.frame(line);
        WireCodec decoder = new WireCodec();
        List<String> restored = new ArrayList<>();
        for (byte[] chunk : chunks) {
            if (chunk.length > 18) throw new AssertionError("ATT size");
            restored.addAll(decoder.accept(chunk));
        }
        if (!restored.equals(Collections.singletonList(line))) throw new AssertionError("ATT round trip");
        decoder.accept("partial old session".getBytes(StandardCharsets.UTF_8));
        decoder.reset();
        if (!decoder.accept("fresh\n".getBytes(StandardCharsets.UTF_8)).equals(Collections.singletonList("fresh")))
            throw new AssertionError("session reset");
        try { decoder.accept(new byte[4097]); throw new AssertionError("oversized notification accepted"); }
        catch (IllegalArgumentException expected) { }
        try { WireCodec.frame(String.join("", Collections.nCopies(767, "x"))); throw new AssertionError("oversized command accepted"); }
        catch (IllegalArgumentException expected) { }
        if (WireCodec.frame(String.join("", Collections.nCopies(766, "x"))).isEmpty()) throw new AssertionError();
        System.out.println("PASS: fragmented Chinese UTF-8, multiple/blank frames, ATT bounds, session reset and oversize rejection");
    }
}
