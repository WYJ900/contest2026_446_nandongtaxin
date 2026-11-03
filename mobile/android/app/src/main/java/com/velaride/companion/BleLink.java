package com.velaride.companion;

import android.annotation.SuppressLint;
import android.bluetooth.*;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import org.json.JSONObject;
import java.io.IOException;
import java.util.UUID;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;

/** One GATT connection, one serialized command, finite waits, no background scanning. */
@SuppressLint("MissingPermission")
public final class BleLink {
    public interface Listener { void changed(boolean ready, String message); }
    private static final UUID SERVICE = uuid("fff0"), TX = uuid("fff1"), RX = uuid("fff2");
    private static final UUID CCC = uuid("2902");
    private final Context context;
    private final Listener listener;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final WireCodec codec = new WireCodec();
    private final ArrayBlockingQueue<Integer> writes = new ArrayBlockingQueue<>(4);
    private final ArrayBlockingQueue<JSONObject> replies = new ArrayBlockingQueue<>(8);
    private volatile BluetoothGatt gatt;
    private volatile BluetoothGattCharacteristic rx;
    private volatile boolean ready;
    private volatile long generation;
    private boolean notificationsRequested;

    public BleLink(Context context, Listener listener) {
        this.context = context;
        this.listener = listener;
    }

    private static UUID uuid(String shortUuid) {
        return UUID.fromString("0000" + shortUuid + "-0000-1000-8000-00805f9b34fb");
    }

    public boolean isReady() { return ready; }

    public void connect(BluetoothDevice device) {
        disconnect("正在连接手表…");
        final long current = generation;
        notificationsRequested = false;
        BluetoothGatt created = device.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE);
        gatt = created;
        if (created == null) { disconnect("蓝牙连接未能启动，请重试"); return; }
        main.postDelayed(() -> {
            if (generation == current && !ready) disconnect("连接超时，请确认手表已开机并靠近手机");
        }, 20000);
    }

    public synchronized void disconnect(String reason) {
        generation++;
        ready = false;
        rx = null;
        BluetoothGatt previous = gatt;
        gatt = null;
        codec.reset();
        replies.clear();
        writes.clear();
        writes.offer(-1);
        if (previous != null) {
            try { previous.disconnect(); } catch (SecurityException ignored) { }
            previous.close();
        }
        main.post(() -> listener.changed(false, reason));
    }

    /** Invoke from the app's single worker, never from a GATT callback/UI thread. */
    public JSONObject request(JSONObject command, String expected) throws Exception {
        final long current = generation;
        final BluetoothGatt connection = gatt;
        final BluetoothGattCharacteristic characteristic = rx;
        if (!ready || connection == null || characteristic == null || current != generation)
            throw new IOException("请先连接手表");
        writes.clear();
        replies.clear();
        try {
            for (byte[] chunk : WireCodec.frame(command.toString())) {
                if (current != generation || !ready) throw new IOException("手表连接已断开");
                boolean accepted;
                if (Build.VERSION.SDK_INT >= 33) {
                    accepted = connection.writeCharacteristic(characteristic, chunk,
                            BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothStatusCodes.SUCCESS;
                } else {
                    characteristic.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
                    characteristic.setValue(chunk);
                    accepted = connection.writeCharacteristic(characteristic);
                }
                if (!accepted) throw new IOException("蓝牙正在忙，请重新连接");
                Integer status = writes.poll(5, TimeUnit.SECONDS);
                if (current != generation || status == null || status != BluetoothGatt.GATT_SUCCESS)
                    throw new IOException("手表写入失败或连接已断开");
            }
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(12);
            while (System.nanoTime() < deadline) {
                if (current != generation || !ready) throw new IOException("手表连接已断开");
                JSONObject response = replies.poll(200, TimeUnit.MILLISECONDS);
                if (response == null) continue;
                if (!response.optBoolean("ok", false))
                    throw new DeviceError(response.optString("error", "设备操作失败"));
                if (expected.equals(response.optString("event"))) return response;
            }
            throw new IOException("手表回复超时，请重新连接");
        } catch (DeviceError error) {
            throw error;
        } catch (Exception error) {
            // A late reply has no request ID in v1. End the session so it
            // cannot accidentally complete a later operation.
            if (current == generation) disconnect(error.getMessage());
            throw error;
        }
    }

    public static final class DeviceError extends WatchHandshake.CommandError {
        public DeviceError(String message) { super(message); }
    }

    private void enableNotifications(BluetoothGatt connection) {
        if (connection != gatt || notificationsRequested) return;
        notificationsRequested = true;
        BluetoothGattService service = connection.getService(SERVICE);
        BluetoothGattCharacteristic tx = service == null ? null : service.getCharacteristic(TX);
        rx = service == null ? null : service.getCharacteristic(RX);
        BluetoothGattDescriptor descriptor = tx == null ? null : tx.getDescriptor(CCC);
        if (tx == null || rx == null || descriptor == null || !connection.setCharacteristicNotification(tx, true)) {
            disconnect("这台设备没有 VelaRide 数据服务，请检查固件版本");
            return;
        }
        boolean started;
        if (Build.VERSION.SDK_INT >= 33) {
            started = connection.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                    == BluetoothStatusCodes.SUCCESS;
        } else {
            descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
            started = connection.writeDescriptor(descriptor);
        }
        if (!started) disconnect("无法订阅手表数据，请重连");
    }

    private void accept(BluetoothGatt connection, byte[] value) {
        if (connection != gatt) return;
        try {
            for (String line : codec.accept(value)) {
                if (!replies.offer(new JSONObject(line))) throw new IOException("收到过多手表回复");
            }
        } catch (Exception error) { disconnect("手表回复格式异常，请重连"); }
    }

    private final BluetoothGattCallback callback = new BluetoothGattCallback() {
        @Override public void onConnectionStateChange(BluetoothGatt connection, int status, int state) {
            main.post(() -> {
                if (connection != gatt) return;
                if (status != BluetoothGatt.GATT_SUCCESS || state == BluetoothProfile.STATE_DISCONNECTED) {
                    disconnect("手表已断开，点击重新连接");
                } else if (state == BluetoothProfile.STATE_CONNECTED) {
                    if (!connection.discoverServices()) disconnect("无法读取手表服务，请重连");
                }
            });
        }
        @Override public void onServicesDiscovered(BluetoothGatt connection, int status) {
            main.post(() -> {
                if (connection != gatt) return;
                if (status != BluetoothGatt.GATT_SUCCESS) { disconnect("读取手表服务失败"); return; }
                enableNotifications(connection);
            });
        }
        @Override public void onDescriptorWrite(BluetoothGatt connection, BluetoothGattDescriptor descriptor, int status) {
            main.post(() -> {
                if (connection != gatt || !CCC.equals(descriptor.getUuid())) return;
                if (status != BluetoothGatt.GATT_SUCCESS) { disconnect("订阅手表数据失败"); return; }
                ready = true;
                listener.changed(true, "手表已连接 · 数据通道就绪");
            });
        }
        @Override public void onCharacteristicWrite(BluetoothGatt connection, BluetoothGattCharacteristic characteristic, int status) {
            if (connection == gatt && RX.equals(characteristic.getUuid())) writes.offer(status);
        }
        @Override public void onCharacteristicChanged(BluetoothGatt connection, BluetoothGattCharacteristic characteristic, byte[] value) {
            if (TX.equals(characteristic.getUuid())) accept(connection, value);
        }
        @Override public void onCharacteristicChanged(BluetoothGatt connection, BluetoothGattCharacteristic characteristic) {
            if (Build.VERSION.SDK_INT < 33 && TX.equals(characteristic.getUuid())) accept(connection, characteristic.getValue());
        }
    };
}
