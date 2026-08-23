package com.gtanav.companion;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanRecord;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.ParcelUuid;
import android.util.Log;

import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.Locale;
import java.util.Queue;
import java.util.UUID;

public class GtaNavService extends Service implements SensorEventListener {
    public static final String ACTION_CONNECT = "com.gtanav.companion.CONNECT";
    public static final String ACTION_SCAN = "com.gtanav.companion.SCAN";
    public static final String ACTION_START_GPS = "com.gtanav.companion.START_GPS";
    public static final String ACTION_SEND = "com.gtanav.companion.SEND";
    public static final String ACTION_STATUS = "com.gtanav.companion.STATUS";
    public static final String EXTRA_COMMAND = "command";
    public static final String EXTRA_GPS = "gps";
    public static final String EXTRA_BRIDGE = "bridge";
    public static final String EXTRA_ADDRESS = "address";
    public static final String EXTRA_DEBUG = "debug";
    public static final String EXTRA_LAT = "lat";
    public static final String EXTRA_LON = "lon";
    public static final String EXTRA_ACC = "acc";
    public static final String EXTRA_HEADING = "heading";
    public static final String EXTRA_SCAN_ADDRESS = "scan_address";
    public static final String EXTRA_SCAN_LABEL = "scan_label";
    public static final String EXTRA_SCAN_RSSI = "scan_rssi";
    public static final String EXTRA_SCAN_SCORE = "scan_score";
    public static String lastGpsStatus = "Service idle";
    public static String lastBridgeStatus = "Offline";
    public static boolean hasLastLocation = false;
    public static double lastLatitude = 0.0;
    public static double lastLongitude = 0.0;
    public static float lastAccuracy = 0f;
    public static float lastHeading = 0f;
    public static String lastDebugLog = "";

    private static final UUID SERVICE_UUID = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    private static final UUID RX_UUID = UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
    private static final UUID TX_UUID = UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e");
    private static final UUID CCCD_UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    private static final String KNOWN_WATCH_ADDRESS = "28:84:85:B4:F2:7C";
    private static final String CHANNEL_ID = "gta_nav_live";
    private static final int NOTIFICATION_ID = 9105;
    private static final String PREFS = "gta_nav_bridge";
    private static final String PREF_SELECTED_ADDRESS = "selected_watch_address";

    private final Handler main = new Handler(Looper.getMainLooper());
    private final Queue<String> writeQueue = new ArrayDeque<>();
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic rx;
    private BluetoothGattCharacteristic tx;
    private BluetoothAdapter bluetoothAdapter;
    private BluetoothLeScanner scanner;
    private boolean writeInFlight = false;
    private boolean descriptorInFlight = false;
    private boolean writeWithoutResponse = false;
    private boolean connecting = false;
    private boolean scanning = false;
    private boolean scanOnly = false;
    private boolean discovering = false;
    private LocationManager locationManager;
    private SensorManager sensorManager;
    private Sensor rotationSensor;
    private Sensor accelerometer;
    private Sensor magnetometer;
    private float heading = 0f;
    private long lastHeadingSendMs = 0;
    private float lastSentHeading = -999f;
    private final float[] accelValues = new float[3];
    private final float[] magnetValues = new float[3];
    private boolean hasAccel = false;
    private boolean hasMagnet = false;
    private boolean gpsPrimed = false;
    private long lastNotificationMs = 0;
    private int connectAttempt = 0;
    private String requestedAddress = "";

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
        startForeground(NOTIFICATION_ID, notification("Starting live GTA-Nav bridge"));
        debug("service create");
        requestedAddress = getSelectedAddress();
        sensorManager = (SensorManager) getSystemService(Context.SENSOR_SERVICE);
        if (sensorManager != null) {
            rotationSensor = sensorManager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR);
            if (rotationSensor == null) {
                rotationSensor = sensorManager.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR);
            }
            accelerometer = sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
            magnetometer = sensorManager.getDefaultSensor(Sensor.TYPE_MAGNETIC_FIELD);
            if (rotationSensor != null) {
                sensorManager.registerListener(this, rotationSensor, SensorManager.SENSOR_DELAY_GAME);
            } else {
                if (accelerometer != null) sensorManager.registerListener(this, accelerometer, SensorManager.SENSOR_DELAY_GAME);
                if (magnetometer != null) sensorManager.registerListener(this, magnetometer, SensorManager.SENSOR_DELAY_GAME);
            }
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null || intent.getAction() == null) {
            connectKnownWatch(false);
            return START_STICKY;
        }
        String action = intent.getAction();
        if (ACTION_SCAN.equals(action)) {
            debug("action scan");
            startWatchScanOnly();
        } else if (ACTION_CONNECT.equals(action)) {
            requestedAddress = cleanAddress(intent.getStringExtra(EXTRA_ADDRESS));
            if (requestedAddress.length() > 0) {
                saveSelectedAddress(requestedAddress);
            }
            debug("action connect selected=" + requestedAddress);
            connectKnownWatch(false);
        } else if (ACTION_START_GPS.equals(action)) {
            String incomingAddress = cleanAddress(intent.getStringExtra(EXTRA_ADDRESS));
            if (incomingAddress.length() > 0) {
                requestedAddress = incomingAddress;
                saveSelectedAddress(requestedAddress);
            } else if (requestedAddress == null || requestedAddress.length() == 0) {
                requestedAddress = getSelectedAddress();
            }
            debug("action start gps selected=" + requestedAddress);
            if (rx == null && (requestedAddress == null || requestedAddress.length() == 0)) {
                status("Pick GTA-Nav watch from list before GPS", "Pick watch");
                debug("start gps blocked: no selected address");
                return START_STICKY;
            }
            connectKnownWatch(false);
            gpsPrimed = false;
            send("ANCHOR");
            startLocation();
        } else if (ACTION_SEND.equals(action)) {
            String command = intent.getStringExtra(EXTRA_COMMAND);
            if (command != null && command.length() > 0) {
                if ("SCREEN,map".equalsIgnoreCase(command) || command.toUpperCase(Locale.US).startsWith("ZOOM,")) {
                    debug("ignore stale UI command " + command.split(",", 2)[0]);
                    return START_STICKY;
                }
                if (command.startsWith("HEAD,")) {
                    try {
                        heading = Float.parseFloat(command.substring(5));
                    } catch (NumberFormatException ignored) {
                    }
                }
                send(command);
            }
        }
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        debug("service destroy");
        if (sensorManager != null) {
            sensorManager.unregisterListener(this);
        }
        if (locationManager != null) {
            locationManager.removeUpdates(locationListener);
        }
        if (gatt != null) {
            gatt.disconnect();
            gatt.close();
            gatt = null;
        }
        super.onDestroy();
    }

    @SuppressLint("MissingPermission")
    private void startWatchScanOnly() {
        scanOnly = true;
        connectKnownWatch(true);
    }

    private void connectKnownWatch(boolean listOnly) {
        if (scanning && !listOnly) {
            stopScan();
        }
        if (rx != null || connecting || scanning) {
            if (rx != null) {
                status("Watch service linked", "Linked");
            }
            debug("connect ignored rx=" + (rx != null) + " connecting=" + connecting + " scanning=" + scanning);
            return;
        }
        scanOnly = listOnly;
        if (!hasPermission(Manifest.permission.BLUETOOTH_CONNECT)) {
            status("Nearby Devices permission needed", "Permission");
            debug("missing bluetooth connect permission");
            return;
        }
        BluetoothManager manager = (BluetoothManager) getSystemService(Context.BLUETOOTH_SERVICE);
        bluetoothAdapter = manager == null ? null : manager.getAdapter();
        if (bluetoothAdapter == null || !bluetoothAdapter.isEnabled()) {
            status("Bluetooth is off", "BLE off");
            debug("bluetooth off");
            return;
        }
        if (!listOnly && (requestedAddress == null || requestedAddress.length() == 0)) {
            status("Pick GTA-Nav watch from list first", "Pick watch");
            debug("connect blocked: no selected address");
            return;
        }
        if (!listOnly && requestedAddress != null && requestedAddress.length() > 0) {
            debug("selected device direct path " + requestedAddress);
            connectDirectKnown();
            return;
        }
        if (hasPermission(Manifest.permission.BLUETOOTH_SCAN)) {
            scanner = bluetoothAdapter.getBluetoothLeScanner();
        }
        if (scanner != null) {
            scanning = true;
            status("Scanning for GTA-Nav watch", "Scanning");
            debug(listOnly ? "scan list start" : "scan start");
            ScanSettings settings = new ScanSettings.Builder()
                    .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                    .build();
            scanner.startScan(null, settings, scanCallback);
            main.postDelayed(() -> {
                if (rx != null || connecting) {
                    return;
                }
                stopScan();
                if (!scanOnly) {
                    connectDirectKnown();
                } else {
                    status("Pick GTA-Nav watch from list", "Pick watch");
                }
            }, 6500);
            return;
        }
        if (!scanOnly) {
            connectDirectKnown();
        }
    }

    @SuppressLint("MissingPermission")
    private void connectDirectKnown() {
        if (bluetoothAdapter == null) {
            return;
        }
        if (requestedAddress == null || requestedAddress.length() == 0) {
            status("Pick GTA-Nav watch from list first", "Pick watch");
            debug("direct connect blocked: no selected address");
            return;
        }
        String address = requestedAddress;
        status("Trying " + address, "Connecting");
        debug("direct connect " + address);
        connectDevice(bluetoothAdapter.getRemoteDevice(address));
    }

    @SuppressLint("MissingPermission")
    private void connectDevice(BluetoothDevice device) {
        if (device == null || rx != null || connecting) {
            return;
        }
        if (gatt != null) {
            debug("closing stale gatt before connect");
            gatt.close();
            gatt = null;
        }
        requestedAddress = device.getAddress();
        connecting = true;
        connectAttempt++;
        debug("connectGatt attempt=" + connectAttempt + " addr=" + device.getAddress());
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            gatt = device.connectGatt(this, false, gattCallback, BluetoothDevice.TRANSPORT_LE);
        } else {
            gatt = device.connectGatt(this, false, gattCallback);
        }
    }

    @SuppressLint("MissingPermission")
    private void stopScan() {
        if (scanner != null && scanning) {
            scanner.stopScan(scanCallback);
            debug("scan stop");
        }
        scanning = false;
    }

    private final ScanCallback scanCallback = new ScanCallback() {
        @SuppressLint("MissingPermission")
        @Override
        public void onScanResult(int callbackType, ScanResult result) {
            BluetoothDevice device = result.getDevice();
            ScanRecord record = result.getScanRecord();
            String name = device.getName();
            String recordName = record == null ? null : record.getDeviceName();
            String label = name != null ? name : recordName;
            String lower = label == null ? "" : label.toLowerCase(Locale.US);
            boolean nameMatch = lower.contains("gta-nav") || lower.contains("chronos") || lower.contains("navio");
            boolean addressMatch = KNOWN_WATCH_ADDRESS.equalsIgnoreCase(device.getAddress());
            boolean serviceMatch = false;
            if (record != null && record.getServiceUuids() != null) {
                for (ParcelUuid uuid : record.getServiceUuids()) {
                    if (SERVICE_UUID.equals(uuid.getUuid())) {
                        serviceMatch = true;
                        break;
                    }
                }
            }
            String shownLabel = label == null || label.length() == 0 ? device.getAddress() : label;
            int score = (addressMatch ? 100 : 0)
                    + (nameMatch ? 60 : 0)
                    + (serviceMatch ? 40 : 0)
                    + Math.max(0, result.getRssi() + 100);
            broadcastScanHit(device.getAddress(), shownLabel, result.getRssi(), score);
            debug("scan hit " + device.getAddress() + " label=" + shownLabel + " rssi=" + result.getRssi() + " score=" + score);
            if (scanOnly) {
                status("Pick watch from list", "Pick watch");
                return;
            }
            if (!addressMatch) {
                debug("ignored scan auto-connect candidate " + device.getAddress());
                return;
            }
            stopScan();
            status("Found " + shownLabel, "Connecting");
            connectDevice(device);
        }

        @Override
        public void onScanFailed(int errorCode) {
            scanning = false;
            status("BLE scan failed " + errorCode, "Scan failed");
            debug("scan failed code=" + errorCode);
            if (!scanOnly && requestedAddress != null && requestedAddress.length() > 0) {
                connectDirectKnown();
            }
        }
    };

    @SuppressLint("MissingPermission")
    private void startLocation() {
        if (!hasPermission(Manifest.permission.ACCESS_FINE_LOCATION)) {
            status("Location permission needed", rx == null ? "Local only" : "Linked");
            return;
        }
        locationManager = (LocationManager) getSystemService(Context.LOCATION_SERVICE);
        if (locationManager == null) {
            status("GPS unavailable", rx == null ? "Local only" : "Linked");
            return;
        }
        try {
            locationManager.requestLocationUpdates(LocationManager.GPS_PROVIDER, 500, 0f, locationListener);
        } catch (IllegalArgumentException ignored) {
        }
        try {
            locationManager.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, 1000, 0f, locationListener);
        } catch (IllegalArgumentException ignored) {
        }
        status("Live GPS service running", rx == null ? "Connecting" : "Linked");
    }

    private final LocationListener locationListener = location -> {
        hasLastLocation = true;
        lastLatitude = location.getLatitude();
        lastLongitude = location.getLongitude();
        lastAccuracy = location.hasAccuracy() ? location.getAccuracy() : 0f;
        lastHeading = heading;
        broadcastGps(location);
        if (!gpsPrimed) {
            gpsPrimed = true;
        }
        send(String.format(Locale.US, "GPS,%.6f,%.6f,%.1f,%.0f",
                location.getLatitude(),
                location.getLongitude(),
                location.hasSpeed() ? location.getSpeed() * 2.236936 : 0.0,
                heading));
    };

    @Override
    public void onSensorChanged(SensorEvent event) {
        int type = event.sensor.getType();
        if (type == Sensor.TYPE_ROTATION_VECTOR || type == Sensor.TYPE_GAME_ROTATION_VECTOR) {
            float[] matrix = new float[9];
            SensorManager.getRotationMatrixFromVector(matrix, event.values);
            float[] orientation = new float[3];
            SensorManager.getOrientation(matrix, orientation);
            updateHeading((float) ((Math.toDegrees(orientation[0]) + 360.0) % 360.0));
        } else if (type == Sensor.TYPE_ACCELEROMETER) {
            System.arraycopy(event.values, 0, accelValues, 0, accelValues.length);
            hasAccel = true;
            updateHeadingFromFallback();
        } else if (type == Sensor.TYPE_MAGNETIC_FIELD) {
            System.arraycopy(event.values, 0, magnetValues, 0, magnetValues.length);
            hasMagnet = true;
            updateHeadingFromFallback();
        }
    }

    private void updateHeadingFromFallback() {
        if (!hasAccel || !hasMagnet) return;
        float[] matrix = new float[9];
        if (!SensorManager.getRotationMatrix(matrix, null, accelValues, magnetValues)) return;
        float[] orientation = new float[3];
        SensorManager.getOrientation(matrix, orientation);
        updateHeading((float) ((Math.toDegrees(orientation[0]) + 360.0) % 360.0));
    }

    private void updateHeading(float incoming) {
        if (!Float.isFinite(incoming)) return;
        heading = incoming;
        lastHeading = incoming;
        long now = System.currentTimeMillis();
        float delta = Math.abs(incoming - lastSentHeading);
        delta = Math.min(delta, 360.0f - delta);
        if (now - lastHeadingSendMs < 1000 && delta < 6.0f) return;
        lastHeadingSendMs = now;
        lastSentHeading = incoming;
        send("HEAD," + String.format(Locale.US, "%.0f", heading));
    }

    private String cleanAddress(String address) {
        return address == null ? "" : address.trim().toUpperCase(Locale.US);
    }

    private String getSelectedAddress() {
        return getSharedPreferences(PREFS, MODE_PRIVATE).getString(PREF_SELECTED_ADDRESS, "");
    }

    private void saveSelectedAddress(String address) {
        getSharedPreferences(PREFS, MODE_PRIVATE)
                .edit()
                .putString(PREF_SELECTED_ADDRESS, cleanAddress(address))
                .apply();
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
    }

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
            @Override
            public void onConnectionStateChange(BluetoothGatt g, int statusCode, int newState) {
                if (newState == android.bluetooth.BluetoothProfile.STATE_CONNECTED) {
                    stopScan();
                    debug("gatt connected status=" + statusCode);
                    status("Watch service connected", "Discovering");
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                    g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH);
                }
                discovering = true;
                if (!g.requestMtu(185)) {
                    debug("mtu request failed; discover now");
                    g.discoverServices();
                }
                return;
            }
            rx = null;
            tx = null;
            connecting = false;
            scanning = false;
            writeInFlight = false;
            descriptorInFlight = false;
            discovering = false;
            writeQueue.clear();
            if (gatt == g) {
                gatt = null;
            }
            g.close();
            status("Watch service disconnected " + statusCode, "Offline");
            debug("gatt disconnected status=" + statusCode + " newState=" + newState);
            if (requestedAddress != null && requestedAddress.length() > 0) {
                main.postDelayed(() -> connectKnownWatch(false), 2500);
            }
        }

        @Override
        public void onMtuChanged(BluetoothGatt g, int mtu, int statusCode) {
            debug("mtu changed mtu=" + mtu + " status=" + statusCode);
            if (discovering) {
                g.discoverServices();
            }
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt g, int statusCode) {
            connecting = false;
            discovering = false;
            if (statusCode != BluetoothGatt.GATT_SUCCESS) {
                status("Watch service discovery failed " + statusCode, "BLE error");
                debug("discover failed status=" + statusCode);
                closeGattAndRetry(g, "discover failed");
                return;
            }
            BluetoothGattService service = g.getService(SERVICE_UUID);
            rx = service == null ? null : service.getCharacteristic(RX_UUID);
            tx = service == null ? null : service.getCharacteristic(TX_UUID);
            if (rx == null) {
                status("Watch write channel not found", "No write");
                debug("rx missing service=" + (service != null));
                closeGattAndRetry(g, "rx missing");
                return;
            }
            stopScan();
            enableTxNotifications(g);
            writeWithoutResponse = (rx.getProperties() & BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0;
            rx.setWriteType(writeWithoutResponse
                    ? BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
                    : BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
            status("Watch service linked", "Linked");
            debug("linked rxProps=" + rx.getProperties() + " writeNR=" + writeWithoutResponse + " tx=" + (tx != null));
            send("HEAD," + String.format(Locale.US, "%.0f", heading));
            flushWriteQueue();
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt g, BluetoothGattCharacteristic characteristic, int statusCode) {
            writeInFlight = false;
            if (statusCode != BluetoothGatt.GATT_SUCCESS) {
                debug("write callback status=" + statusCode);
            }
            flushWriteQueue();
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt g, BluetoothGattDescriptor descriptor, int statusCode) {
            descriptorInFlight = false;
            debug("descriptor write status=" + statusCode);
            flushWriteQueue();
        }
    };

    @SuppressLint("MissingPermission")
    private void closeGattAndRetry(BluetoothGatt closingGatt, String reason) {
        rx = null;
        tx = null;
        connecting = false;
        scanning = false;
        discovering = false;
        writeInFlight = false;
        descriptorInFlight = false;
        writeQueue.clear();
        if (closingGatt != null) {
            closingGatt.close();
        }
        if (gatt == closingGatt) {
            gatt = null;
        }
        debug("closed gatt " + reason);
        if (requestedAddress != null && requestedAddress.length() > 0) {
            main.postDelayed(() -> connectKnownWatch(false), 1800);
        }
    }

    @SuppressLint("MissingPermission")
    private void enableTxNotifications(BluetoothGatt g) {
        if (tx == null) {
            return;
        }
        g.setCharacteristicNotification(tx, true);
        BluetoothGattDescriptor descriptor = tx.getDescriptor(CCCD_UUID);
        if (descriptor != null) {
            descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
            descriptorInFlight = g.writeDescriptor(descriptor);
            debug("tx notify enable requested ok=" + descriptorInFlight);
        }
    }

    @SuppressLint("MissingPermission")
    private void send(String command) {
        if (command == null || command.length() == 0) {
            return;
        }
        if (rx == null) {
            if ((command.startsWith("GPS,") || command.startsWith("HEAD,")) &&
                    (requestedAddress == null || requestedAddress.length() == 0)) {
                debug("drop live command without selected watch " + command.split(",", 2)[0]);
                return;
            }
            connectKnownWatch(false);
            if (command.startsWith("GPS,") || command.startsWith("HEAD,")) {
                debug("drop live command while not linked " + command.split(",", 2)[0]);
                return;
            }
        }
        coalesce(command);
        writeQueue.offer(command.endsWith("\n") ? command : command + "\n");
        if (rx == null) {
            debug("queue command while connecting " + command.split(",", 2)[0] + " q=" + writeQueue.size());
            return;
        }
        flushWriteQueue();
    }

    @SuppressLint("MissingPermission")
    private void flushWriteQueue() {
        if (gatt == null || rx == null || writeInFlight || descriptorInFlight || writeQueue.isEmpty()) {
            return;
        }
        String command = writeQueue.poll();
        rx.setValue(command.getBytes(StandardCharsets.UTF_8));
        writeInFlight = gatt.writeCharacteristic(rx);
        debug("write " + command.trim().split(",", 2)[0] + " ok=" + writeInFlight + " q=" + writeQueue.size());
        if (!command.startsWith("HEAD,")) {
            status(writeInFlight ? "Service sent " + command.split(",", 2)[0] : "Service write failed",
                    writeInFlight ? "Linked" : "Failed");
        }
        if (writeInFlight && writeWithoutResponse) {
            main.postDelayed(() -> {
                writeInFlight = false;
                flushWriteQueue();
            }, 70);
        } else if (!writeInFlight) {
            main.postDelayed(this::flushWriteQueue, 120);
        }
    }

    private void coalesce(String command) {
        String prefix = null;
        if (command.startsWith("GPS,")) {
            prefix = "GPS,";
        } else if (command.startsWith("HEAD,")) {
            prefix = "HEAD,";
        }
        if (prefix == null || writeQueue.isEmpty()) {
            return;
        }
        Queue<String> kept = new ArrayDeque<>();
        while (!writeQueue.isEmpty()) {
            String queued = writeQueue.poll();
            if (queued == null || !queued.startsWith(prefix)) {
                kept.offer(queued);
            }
        }
        writeQueue.addAll(kept);
    }

    private boolean hasPermission(String permission) {
        if (Build.VERSION.SDK_INT < 23) {
            return true;
        }
        if (Build.VERSION.SDK_INT < 31 && Manifest.permission.BLUETOOTH_CONNECT.equals(permission)) {
            return true;
        }
        return checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED;
    }

    private void status(String gps, String bridge) {
        lastGpsStatus = gps == null ? "" : gps;
        lastBridgeStatus = bridge == null ? "" : bridge;
        debug("status gps=" + lastGpsStatus + " bridge=" + lastBridgeStatus);
        sendBroadcast(broadcastStatus(gps, bridge, null));
    }

    private void broadcastScanHit(String address, String label, int rssi, int score) {
        Intent intent = new Intent(ACTION_STATUS);
        intent.setPackage(getPackageName());
        intent.putExtra(EXTRA_GPS, lastGpsStatus);
        intent.putExtra(EXTRA_BRIDGE, lastBridgeStatus);
        intent.putExtra(EXTRA_DEBUG, lastDebugLog);
        intent.putExtra(EXTRA_SCAN_ADDRESS, address);
        intent.putExtra(EXTRA_SCAN_LABEL, label);
        intent.putExtra(EXTRA_SCAN_RSSI, rssi);
        intent.putExtra(EXTRA_SCAN_SCORE, score);
        sendBroadcast(intent);
    }

    private void broadcastGps(Location location) {
        Intent intent = broadcastStatus("GPS live " + Math.round(lastAccuracy) + "m",
                rx == null ? "Local only" : "Linked",
                location);
        sendBroadcast(intent);
    }

    private Intent broadcastStatus(String gps, String bridge, Location location) {
        lastGpsStatus = gps == null ? "" : gps;
        lastBridgeStatus = bridge == null ? "" : bridge;
        NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        long now = System.currentTimeMillis();
        if (manager != null && now - lastNotificationMs > 1000) {
            lastNotificationMs = now;
            manager.notify(NOTIFICATION_ID, notification(bridge + " / " + gps));
        }
        Intent intent = new Intent(ACTION_STATUS);
        intent.setPackage(getPackageName());
        intent.putExtra(EXTRA_GPS, gps);
        intent.putExtra(EXTRA_BRIDGE, bridge);
        intent.putExtra(EXTRA_DEBUG, lastDebugLog);
        if (location != null) {
            intent.putExtra(EXTRA_LAT, location.getLatitude());
            intent.putExtra(EXTRA_LON, location.getLongitude());
            intent.putExtra(EXTRA_ACC, location.hasAccuracy() ? location.getAccuracy() : 0f);
            intent.putExtra(EXTRA_HEADING, heading);
        }
        return intent;
    }

    private void debug(String line) {
        String stamp = String.format(Locale.US, "%1$tH:%1$tM:%1$tS ", System.currentTimeMillis());
        String entry = stamp + line;
        Log.d("GtaNavService", entry);
        if (lastDebugLog.length() > 3800) {
            lastDebugLog = lastDebugLog.substring(lastDebugLog.length() - 3000);
        }
        lastDebugLog = lastDebugLog + entry + "\n";
        Intent intent = new Intent(ACTION_STATUS);
        intent.setPackage(getPackageName());
        intent.putExtra(EXTRA_GPS, lastGpsStatus);
        intent.putExtra(EXTRA_BRIDGE, lastBridgeStatus);
        intent.putExtra(EXTRA_DEBUG, lastDebugLog);
        sendBroadcast(intent);
    }

    private Notification notification(String text) {
        Intent launch = new Intent(this, MainActivity.class);
        PendingIntent pending = PendingIntent.getActivity(this, 0, launch,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        Notification.Builder builder = Build.VERSION.SDK_INT >= 26
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);
        return builder
                .setContentTitle("GTA-Nav live bridge")
                .setContentText(text)
                .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
                .setContentIntent(pending)
                .setOngoing(true)
                .build();
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT < 26) {
            return;
        }
        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                "GTA-Nav live bridge",
                NotificationManager.IMPORTANCE_LOW);
        NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager != null) {
            manager.createNotificationChannel(channel);
        }
    }
}
