package com.velaride.companion;

import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;

public final class ReviewClientTest {
    static final class Fake extends HttpURLConnection {
        final ByteArrayOutputStream sent = new ByteArrayOutputStream();
        byte[] reply = "{\"ok\":true}".getBytes(StandardCharsets.UTF_8);
        int status = 200; boolean disconnected;
        Fake() throws Exception { super(new URL("https://example.invalid/api/ride/review")); }
        public OutputStream getOutputStream() { return sent; }
        public InputStream getInputStream() { return new ByteArrayInputStream(reply); }
        public int getResponseCode() { return status; }
        public void disconnect() { disconnected = true; }
        public boolean usingProxy() { return false; }
        public void connect() { }
    }
    static void check(boolean value) { if (!value) throw new AssertionError(); }
    public static void main(String[] args) throws Exception {
        String endpoint = "https://example.invalid/api/ride/review";
        for (String invalid : new String[]{"http://example.invalid/api/ride/review", "http://10.0.0.2:4173/api/ride/review", "http://127.0.0.1.example/api/ride/review", "https://u:p@example.invalid/api/ride/review",
                endpoint+"?token=bad",endpoint+"#fragment","https://example.invalid/other","not-a-url"}) {
            try { ReviewClient.endpoint(invalid); throw new AssertionError(); }
            catch (IOException expected) { }
        }
        Fake fake = new Fake();
        ReviewClient.endpoint("http://127.0.0.1:4173/api/ride/review");
        ReviewClient.endpoint("http://localhost:4173/api/ride/review");
        String result = new ReviewClient(url -> fake).post(endpoint,"{\"骑行\":60}");
        check(result.contains("true") && fake.disconnected && !fake.getInstanceFollowRedirects());
        check(fake.getConnectTimeout()==10000 && fake.getReadTimeout()==60000);
        check(new String(fake.sent.toByteArray(),StandardCharsets.UTF_8).contains("骑行"));
        for (int status : new int[]{302,400,500}) {
            Fake broken = new Fake(); broken.status=status;
            try { new ReviewClient(url->broken).post(endpoint,"{}"); throw new AssertionError(); }
            catch (IOException expected) { check(broken.disconnected); }
        }
        Fake huge = new Fake(); huge.reply=new byte[16385];
        try { new ReviewClient(url->huge).post(endpoint,"{}"); throw new AssertionError(); }
        catch (IOException expected) { check(huge.disconnected); }
        System.out.println("PASS: HTTPS/loopback-only endpoint policy, UTF-8 POST, finite timeouts, redirects, HTTP errors, response cap and disconnect cleanup");
    }
}
