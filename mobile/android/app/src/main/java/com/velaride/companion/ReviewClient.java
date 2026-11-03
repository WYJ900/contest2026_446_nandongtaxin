package com.velaride.companion;

import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;

/** User-triggered HTTPS connection to a configured VelaRide review gateway. */
public final class ReviewClient {
    interface Connections { HttpURLConnection open(URL url) throws IOException; }
    private final Connections connections;
    public ReviewClient() { this(url -> (HttpURLConnection)url.openConnection()); }
    ReviewClient(Connections connections) { this.connections = connections; }
    public static URL endpoint(String value) throws IOException {
        URL url = new URL(value.trim());
        if (!"https".equalsIgnoreCase(url.getProtocol()) || url.getHost().isEmpty()
                || url.getUserInfo() != null || url.getRef() != null || url.getQuery() != null
                || !url.getPath().endsWith("/api/ride/review"))
            throw new IOException("请输入完整 HTTPS 复盘地址，以 /api/ride/review 结尾，不包含凭据或参数。");
        return url;
    }
    public String post(String address, String payload) throws IOException {
        byte[] data = payload.getBytes(StandardCharsets.UTF_8);
        if (data.length > 16384) throw new IOException("骑行摘要过大");
        HttpURLConnection connection = connections.open(endpoint(address));
        try {
            connection.setConnectTimeout(10000); connection.setReadTimeout(60000);
            connection.setInstanceFollowRedirects(false);
            connection.setRequestMethod("POST"); connection.setDoOutput(true);
            connection.setRequestProperty("Content-Type", "application/json; charset=utf-8");
            connection.setFixedLengthStreamingMode(data.length);
            try (OutputStream output = connection.getOutputStream()) { output.write(data); }
            if (connection.getResponseCode() != 200) throw new IOException("复盘服务未成功响应（HTTP " + connection.getResponseCode() + "）");
            try (InputStream input = connection.getInputStream(); ByteArrayOutputStream bytes = new ByteArrayOutputStream()) {
                byte[] buffer = new byte[2048]; int count;
                while ((count = input.read(buffer)) != -1) {
                    if (bytes.size() + count > 16384) throw new IOException("复盘回复过长");
                    bytes.write(buffer, 0, count);
                }
                return new String(bytes.toByteArray(), StandardCharsets.UTF_8);
            }
        } finally { connection.disconnect(); }
    }
}
