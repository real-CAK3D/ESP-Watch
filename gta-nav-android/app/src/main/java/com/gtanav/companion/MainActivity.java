package com.gtanav.companion;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanRecord;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import android.provider.CallLog;
import android.provider.CalendarContract;
import android.provider.ContactsContract;
import android.provider.MediaStore;
import android.view.Surface;
import android.webkit.JavascriptInterface;
import android.webkit.PermissionRequest;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Queue;
import java.util.Locale;
import java.util.UUID;

public class MainActivity extends Activity implements SensorEventListener {
    private static final UUID SERVICE_UUID = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    private static final UUID RX_UUID = UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
    private static final UUID GENERIC_ACCESS_UUID = UUID.fromString("00001800-0000-1000-8000-00805f9b34fb");
    private static final UUID GENERIC_ATTRIBUTE_UUID = UUID.fromString("00001801-0000-1000-8000-00805f9b34fb");
    private static final String KNOWN_WATCH_ADDRESS = "28:84:85:B4:F2:7C";
    private static final String REMOTE_DASHBOARD_URL = "https://nukebox.tailac984b.ts.net/";
    private static final String LOCAL_DASHBOARD_URL = "file:///android_asset/dashboard/index.html";
    private static final int REQ_PERMS = 42;

    private final Handler main = new Handler(Looper.getMainLooper());
    private WebView web;
    private BluetoothLeScanner scanner;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic rx;
    private String connectedAddress = "";
    private final List<ScanHit> scanHits = new ArrayList<>();
    private final Map<String, BluetoothDevice> seenDevices = new HashMap<>();
    private final Queue<String> writeQueue = new ArrayDeque<>();
    private boolean writeInFlight = false;
    private boolean writeWithoutResponse = false;
    private boolean tryingScanFallback = false;
    private int reconnectAttempts = 0;
    private LocationManager locationManager;
    private SensorManager sensorManager;
    private Sensor rotationSensor;
    private Sensor accelerometer;
    private Sensor magnetometer;
    private Sensor stepCounterSensor;
    private Location origin;
    private Location lastLocation;
    private Location testLocation;
    private int testMeters = 0;
    private boolean anchorSent = false;
    private float compassHeading = 0f;
    private long lastHeadingSendMs = 0;
    private float lastSentHeading = -999f;
    private final float[] accelValues = new float[3];
    private final float[] magnetValues = new float[3];
    private boolean hasAccel = false;
    private boolean hasMagnet = false;
    private float stepCounterBase = -1f;
    private int latestSteps = 0;
    private float latestStepMiles = 0f;
    private int latestStepCalories = 0;
    private int latestActiveMinutes = 0;
    private boolean loadedLocalFallback = false;
    private boolean serviceLandmarksSynced = false;
    private final BroadcastReceiver serviceStatusReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (!GtaNavService.ACTION_STATUS.equals(intent.getAction())) {
                return;
            }
            status(intent.getStringExtra(GtaNavService.EXTRA_GPS),
                    intent.getStringExtra(GtaNavService.EXTRA_BRIDGE));
            String debug = intent.getStringExtra(GtaNavService.EXTRA_DEBUG);
            if (debug != null) {
                eval("window.nativeDebug && nativeDebug(" + quote(debug) + ");");
            }
            String scanAddress = intent.getStringExtra(GtaNavService.EXTRA_SCAN_ADDRESS);
            if (scanAddress != null && scanAddress.length() > 0) {
                String scanLabel = intent.getStringExtra(GtaNavService.EXTRA_SCAN_LABEL);
                int scanRssi = intent.getIntExtra(GtaNavService.EXTRA_SCAN_RSSI, -127);
                int scanScore = intent.getIntExtra(GtaNavService.EXTRA_SCAN_SCORE, 0);
                eval("window.nativeBleScanHit && nativeBleScanHit(" +
                        quote(scanAddress) + "," +
                        quote(scanLabel == null ? scanAddress : scanLabel) + "," +
                        scanRssi + "," + scanScore + ");");
            }
            if (intent.hasExtra(GtaNavService.EXTRA_LAT) && intent.hasExtra(GtaNavService.EXTRA_LON)) {
                double lat = intent.getDoubleExtra(GtaNavService.EXTRA_LAT, 0.0);
                double lon = intent.getDoubleExtra(GtaNavService.EXTRA_LON, 0.0);
                float acc = intent.getFloatExtra(GtaNavService.EXTRA_ACC, 0f);
                float heading = intent.getFloatExtra(GtaNavService.EXTRA_HEADING, compassHeading);
                eval(String.format(Locale.US,
                        "window.nativeGps && nativeGps(%.6f,%.6f,%.1f,%.0f);",
                        lat, lon, acc, heading));
            }
            String bridge = intent.getStringExtra(GtaNavService.EXTRA_BRIDGE);
            String bridgeLower = bridge == null ? "" : bridge.toLowerCase(Locale.US);
            boolean linked = bridgeLower.contains("linked") &&
                    !bridgeLower.contains("not linked") &&
                    !bridgeLower.contains("offline") &&
                    !bridgeLower.contains("local only");
            if (linked) {
                if (!serviceLandmarksSynced) {
                    serviceLandmarksSynced = true;
                    eval("window.syncLandmarksToWatch && syncLandmarksToWatch();");
                }
            } else {
                serviceLandmarksSynced = false;
            }
        }
    };

    private static class ScanHit {
        final BluetoothDevice device;
        final String label;
        final int rssi;
        final int score;

        ScanHit(BluetoothDevice device, String label, int rssi, int score) {
            this.device = device;
            this.label = label;
            this.rssi = rssi;
            this.score = score;
        }
    }

    private static final int PROP_WRITE =
            BluetoothGattCharacteristic.PROPERTY_WRITE |
                    BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestNeededPermissions();
        sensorManager = (SensorManager) getSystemService(Context.SENSOR_SERVICE);
        if (sensorManager != null) {
            rotationSensor = sensorManager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR);
            if (rotationSensor == null) {
                rotationSensor = sensorManager.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR);
            }
            accelerometer = sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
            magnetometer = sensorManager.getDefaultSensor(Sensor.TYPE_MAGNETIC_FIELD);
            stepCounterSensor = sensorManager.getDefaultSensor(Sensor.TYPE_STEP_COUNTER);
        }
        buildWebView();
    }

    @Override
    protected void onResume() {
        super.onResume();
        IntentFilter filter = new IntentFilter(GtaNavService.ACTION_STATUS);
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(serviceStatusReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(serviceStatusReceiver, filter);
        }
        if (sensorManager != null && rotationSensor != null) {
            sensorManager.registerListener(this, rotationSensor, SensorManager.SENSOR_DELAY_UI);
        } else if (sensorManager != null) {
            if (accelerometer != null) {
                sensorManager.registerListener(this, accelerometer, SensorManager.SENSOR_DELAY_UI);
            }
            if (magnetometer != null) {
                sensorManager.registerListener(this, magnetometer, SensorManager.SENSOR_DELAY_UI);
            }
        }
        if (sensorManager != null && stepCounterSensor != null &&
                (Build.VERSION.SDK_INT < 29 || hasPermission(Manifest.permission.ACTIVITY_RECOGNITION))) {
            sensorManager.registerListener(this, stepCounterSensor, SensorManager.SENSOR_DELAY_NORMAL);
        }
    }

    @Override
    protected void onPause() {
        if (sensorManager != null) {
            sensorManager.unregisterListener(this);
        }
        try {
            unregisterReceiver(serviceStatusReceiver);
        } catch (IllegalArgumentException ignored) {
        }
        super.onPause();
    }

    @SuppressLint({"SetJavaScriptEnabled", "AddJavascriptInterface"})
    private void buildWebView() {
        web = new WebView(this);
        WebSettings settings = web.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setCacheMode(WebSettings.LOAD_NO_CACHE);
        settings.setAllowFileAccess(true);
        settings.setAllowContentAccess(true);
        settings.setAllowFileAccessFromFileURLs(true);
        settings.setAllowUniversalAccessFromFileURLs(true);
        web.addJavascriptInterface(new NativeBridge(), "GtaNavNative");
        web.clearCache(true);
        web.setWebViewClient(new WebViewClient() {
            @Override
            public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
                if (request != null && request.isForMainFrame() && !loadedLocalFallback) {
                    loadedLocalFallback = true;
                    view.loadUrl(LOCAL_DASHBOARD_URL);
                }
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                injectNativeHooks();
            }
        });
        web.setWebChromeClient(new WebChromeClient() {
            @Override
            public void onPermissionRequest(PermissionRequest request) {
                main.post(() -> {
                    List<String> allowed = new ArrayList<>();
                    for (String resource : request.getResources()) {
                        if (PermissionRequest.RESOURCE_AUDIO_CAPTURE.equals(resource) &&
                                hasPermission(Manifest.permission.RECORD_AUDIO)) {
                            allowed.add(resource);
                        } else if (PermissionRequest.RESOURCE_VIDEO_CAPTURE.equals(resource) &&
                                hasPermission(Manifest.permission.CAMERA)) {
                            allowed.add(resource);
                        }
                    }
                    if (allowed.isEmpty()) {
                        request.deny();
                    } else {
                        request.grant(allowed.toArray(new String[0]));
                    }
                });
            }
        });
        setContentView(web);
        web.loadUrl(LOCAL_DASHBOARD_URL + "?native=1&t=" + System.currentTimeMillis());
    }

    private void injectNativeHooks() {
        eval("(() => {" +
                "document.body.classList.add('native-app');" +
                "const b=(id,fn)=>{const el=document.querySelector(id); if(el) el.onclick=fn;};" +
                "b('#connectBle',()=>GtaNavNative.connectWatch());" +
                "b('#startGps',()=>GtaNavNative.startGps());" +
                "b('#testMove',()=>GtaNavNative.testMove());" +
                "const pageSendCommand=window.sendCommand;" +
                "window.nativeSendCommand=async(command)=>{" +
                " const box=document.querySelector('#commandBox'); if(box) box.value=command;" +
                " GtaNavNative.sendCommand(command); return true;" +
                "};" +
                "window.sendCommand=async(command)=>{" +
                " if(window.gtaMockBridgeEnabled && window.gtaMockBridgeEnabled()) return window.mockSendCommand(command);" +
                " if(pageSendCommand) return pageSendCommand(command);" +
                " return window.nativeSendCommand(command);" +
                "};" +
                "window.nativeSetStatus=(gps,bridge)=>{" +
                " const g=document.querySelector('#gpsStatus'); if(g) g.textContent=gps;" +
                " if(window.setBleState) setBleState(bridge);" +
                " else { const b=document.querySelector('#bridgeStatus'); if(b) b.textContent=bridge; const s=document.querySelector('#bleState'); if(s) s.textContent=bridge; }" +
                "};" +
                "window.nativeGps=(lat,lon,acc,heading)=>{" +
                " updateGpsReadout({coords:{latitude:lat,longitude:lon,accuracy:acc,heading:heading,speed:0}});" +
                "};" +
                "window.nativeHeading=(heading)=>{" +
                " if(window.setCompassHeading) window.setCompassHeading(heading);" +
                "};" +
                "window.nativePollService=()=>{" +
                " if(!window.GtaNavNative || !GtaNavNative.getBridgeStatus) return;" +
                " const gps=GtaNavNative.getGpsStatus(); const bridge=GtaNavNative.getBridgeStatus();" +
                " nativeSetStatus(gps||'Native GPS ready', bridge||'Offline');" +
                " if(GtaNavNative.getDebugLog && window.nativeDebug) nativeDebug(GtaNavNative.getDebugLog());" +
                " if(GtaNavNative.hasLastLocation && GtaNavNative.hasLastLocation()) {" +
                "  nativeGps(GtaNavNative.getLastLatitude(),GtaNavNative.getLastLongitude(),GtaNavNative.getLastAccuracy(),GtaNavNative.getLastHeading());" +
                " }" +
                " const bl=String(bridge||'').toLowerCase();" +
                " const linked=bl.includes('linked')&&!bl.includes('not linked')&&!bl.includes('offline')&&!bl.includes('local only');" +
                " if(linked && !window.nativeLandmarksSynced && window.syncLandmarksToWatch) { window.nativeLandmarksSynced=true; syncLandmarksToWatch(); }" +
                " if(!linked) window.nativeLandmarksSynced=false;" +
                "};" +
                "setInterval(window.nativePollService, 500);" +
                "nativeSetStatus('Native GPS ready','Offline');" +
                "window.nativePollService();" +
                "})();");
    }

    private void requestNeededPermissions() {
        List<String> perms = new ArrayList<>();
        perms.add(Manifest.permission.ACCESS_FINE_LOCATION);
        perms.add(Manifest.permission.READ_CONTACTS);
        perms.add(Manifest.permission.READ_CALL_LOG);
        perms.add(Manifest.permission.READ_CALENDAR);
        perms.add(Manifest.permission.CAMERA);
        perms.add(Manifest.permission.RECORD_AUDIO);
        if (Build.VERSION.SDK_INT >= 29) {
            perms.add(Manifest.permission.ACTIVITY_RECOGNITION);
        }
        if (Build.VERSION.SDK_INT >= 31) {
            perms.add(Manifest.permission.BLUETOOTH_SCAN);
            perms.add(Manifest.permission.BLUETOOTH_CONNECT);
        }
        if (Build.VERSION.SDK_INT >= 33) {
            perms.add(Manifest.permission.POST_NOTIFICATIONS);
            perms.add(Manifest.permission.READ_MEDIA_IMAGES);
        } else {
            perms.add(Manifest.permission.READ_EXTERNAL_STORAGE);
        }
        requestPermissions(perms.toArray(new String[0]), REQ_PERMS);
    }

    private boolean hasPermission(String perm) {
        return checkSelfPermission(perm) == PackageManager.PERMISSION_GRANTED;
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != REQ_PERMS) {
            return;
        }
        boolean allGranted = true;
        for (int result : grantResults) {
            if (result != PackageManager.PERMISSION_GRANTED) {
                allGranted = false;
                break;
            }
        }
        status(allGranted ? "Permissions ready. Tap Connect Watch." : "Nearby Devices/Location permission denied",
                allGranted ? "Ready" : "Permission");
    }

    @SuppressLint("MissingPermission")
    private void startBleScan() {
        if (Build.VERSION.SDK_INT >= 31 && (!hasPermission(Manifest.permission.BLUETOOTH_SCAN) ||
                !hasPermission(Manifest.permission.BLUETOOTH_CONNECT))) {
            requestNeededPermissions();
            status("Allow Nearby Devices, then tap Connect again", "Permission");
            return;
        }
        if (!hasPermission(Manifest.permission.ACCESS_FINE_LOCATION)) {
            requestNeededPermissions();
            status("Allow Location for BLE scan, then retry", "Permission");
            return;
        }
        resetBleLink();
        BluetoothManager manager = (BluetoothManager) getSystemService(Context.BLUETOOTH_SERVICE);
        BluetoothAdapter adapter = manager.getAdapter();
        if (adapter == null || !adapter.isEnabled()) {
            status("Bluetooth is off", "BLE off");
            return;
        }
        scanner = adapter.getBluetoothLeScanner();
        scanHits.clear();
        seenDevices.clear();
        tryingScanFallback = false;
        reconnectAttempts = 0;
        status("Scanning for GTA-Nav", "Scanning");
        List<ScanFilter> filters = new ArrayList<>();
        ScanSettings settings = new ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build();
        scanner.startScan(filters, settings, scanCallback);
        main.postDelayed(() -> {
            if (rx != null) {
                return;
            }
            if (gatt != null) {
                gatt.disconnect();
                gatt.close();
                gatt = null;
            }
            if (scanner != null) {
                scanner.stopScan(scanCallback);
            }
            try {
                BluetoothDevice knownWatch = adapter.getRemoteDevice(KNOWN_WATCH_ADDRESS);
                seenDevices.put(KNOWN_WATCH_ADDRESS, knownWatch);
                status("Trying direct watch address", "Connecting");
                connectCandidate(knownWatch, "GTA-Nav watch direct", false);
                return;
            } catch (IllegalArgumentException ignored) {
            }
            tryNextScanCandidate();
        }, 12000);
    }

    @SuppressLint("MissingPermission")
    private void resetBleLink() {
        writeQueue.clear();
        writeInFlight = false;
        writeWithoutResponse = false;
        tryingScanFallback = false;
        rx = null;
        connectedAddress = "";
        if (gatt != null) {
            gatt.disconnect();
            gatt.close();
            gatt = null;
        }
    }

    private final ScanCallback scanCallback = new ScanCallback() {
        @SuppressLint("MissingPermission")
        @Override
        public void onScanResult(int callbackType, ScanResult result) {
            BluetoothDevice device = result.getDevice();
            String name = device.getName();
            ScanRecord record = result.getScanRecord();
            String advertisedName = record == null ? null : record.getDeviceName();
            String visibleName = name != null ? name : advertisedName;
            String lowerName = visibleName == null ? "" : visibleName.toLowerCase(Locale.US);
            boolean nameMatches = lowerName.contains("gta-nav") ||
                    lowerName.contains("chronos") ||
                    lowerName.contains("navio");
            boolean serviceMatches = false;
            if (record != null && record.getServiceUuids() != null) {
                for (ParcelUuid serviceUuid : record.getServiceUuids()) {
                    if (SERVICE_UUID.equals(serviceUuid.getUuid())) {
                        serviceMatches = true;
                        break;
                    }
                }
            }
            if (nameMatches || serviceMatches) {
                if (scanner != null) scanner.stopScan(this);
                connectCandidate(device, visibleName != null ? visibleName : device.getAddress(), false);
                return;
            }
            rememberScanHit(device, visibleName, result.getRssi(), record);
        }

        @Override
        public void onScanFailed(int errorCode) {
            status("BLE scan failed " + errorCode, "Scan failed");
        }
    };

    private void rememberScanHit(BluetoothDevice device, String visibleName, int rssi, ScanRecord record) {
        if (device == null) return;
        seenDevices.put(device.getAddress(), device);
        for (ScanHit hit : scanHits) {
            if (hit.device.getAddress().equals(device.getAddress())) {
                return;
            }
        }
        String label = visibleName != null ? visibleName : device.getAddress();
        String lower = label.toLowerCase(Locale.US);
        int score = rssi;
        if (lower.contains("watch")) score += 70;
        if (lower.contains("esp")) score += 65;
        if (lower.contains("waveshare")) score += 80;
        if (lower.contains("gta") || lower.contains("chronos") || lower.contains("navio")) score += 120;
        if (record != null && record.getServiceUuids() != null) score += record.getServiceUuids().size() * 4;
        if (rssi >= -75) score += 25;
        scanHits.add(new ScanHit(device, label, rssi, score));
        eval("window.nativeBleScanHit && nativeBleScanHit(" +
                quote(device.getAddress()) + "," +
                quote(label) + "," +
                rssi + "," +
                score + ");");
        status("Scanning " + scanHits.size() + " nearby BLE", "Scanning");
    }

    @SuppressLint("MissingPermission")
    private void connectDeviceAddress(String address) {
        BluetoothDevice device = seenDevices.get(address);
        if (device == null && address != null && address.equalsIgnoreCase(KNOWN_WATCH_ADDRESS)) {
            BluetoothManager manager = (BluetoothManager) getSystemService(Context.BLUETOOTH_SERVICE);
            BluetoothAdapter adapter = manager == null ? null : manager.getAdapter();
            if (adapter != null) {
                device = adapter.getRemoteDevice(KNOWN_WATCH_ADDRESS);
            }
        }
        if (device == null) {
            status("Device not in current scan list", "Scan again");
            return;
        }
        if (scanner != null) {
            scanner.stopScan(scanCallback);
        }
        connectCandidate(device, address, false);
    }

    @SuppressLint("MissingPermission")
    private void connectCandidate(BluetoothDevice device, String label, boolean fallback) {
        if (device == null) return;
        tryingScanFallback = fallback;
        connectedAddress = device.getAddress();
        status("Trying " + label, fallback ? "Trying nearby" : "Connecting");
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            gatt = device.connectGatt(MainActivity.this, false, gattCallback, BluetoothDevice.TRANSPORT_LE);
        } else {
            gatt = device.connectGatt(MainActivity.this, false, gattCallback);
        }
    }

    @SuppressLint("MissingPermission")
    private void tryNextScanCandidate() {
        if (gatt != null) return;
        if (scanHits.isEmpty()) {
            status("Watch not found. Power it on, keep map awake, then retry.", "Scan failed");
            return;
        }
        ScanHit best = null;
        for (ScanHit hit : scanHits) {
            if (best == null || hit.score > best.score) {
                best = hit;
            }
        }
        scanHits.remove(best);
        connectCandidate(best.device, best.label + " " + best.rssi + "dBm", true);
    }

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        @Override
        public void onConnectionStateChange(BluetoothGatt g, int statusCode, int newState) {
            if (newState == android.bluetooth.BluetoothProfile.STATE_CONNECTED) {
                reconnectAttempts = 0;
                status("Watch connected", "Discovering");
                g.requestMtu(185);
                g.discoverServices();
            } else {
                rx = null;
                writeQueue.clear();
                writeInFlight = false;
                if (gatt == g) gatt = null;
                if (tryingScanFallback) {
                    g.close();
                    tryingScanFallback = false;
                    tryNextScanCandidate();
                } else {
                    g.close();
                    status("Watch disconnected " + statusCode, "Offline");
                    if (connectedAddress.length() > 0 && reconnectAttempts < 2) {
                        reconnectAttempts++;
                        main.postDelayed(() -> connectDeviceAddress(connectedAddress), 1200L * reconnectAttempts);
                    }
                }
            }
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt g, int statusCode) {
            if (statusCode != BluetoothGatt.GATT_SUCCESS) {
                status("Service discovery failed " + statusCode, "BLE error");
                return;
            }
            BluetoothGattService service = g.getService(SERVICE_UUID);
            rx = service == null ? null : service.getCharacteristic(RX_UUID);
            if (rx == null) {
                rx = findWritableCharacteristic(g);
            }
            if (rx == null && tryingScanFallback) {
                status("No write channel on nearby device", "Trying next");
                g.disconnect();
                return;
            }
            if (rx != null) {
                writeWithoutResponse = (rx.getProperties() & BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0;
                rx.setWriteType(writeWithoutResponse
                        ? BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
                        : BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
            }
            status(rx == null ? "Watch write channel not found" : "Watch linked " + shortUuid(rx.getUuid()),
                    rx == null ? "No write" : "Linked");
            if (rx != null) {
                send("HEAD," + String.format(Locale.US, "%.0f", compassHeading));
                eval("window.syncLandmarksToWatch && syncLandmarksToWatch();");
            }
            flushWriteQueue();
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt g, BluetoothGattCharacteristic characteristic, int statusCode) {
            writeInFlight = false;
            flushWriteQueue();
        }
    };

    private BluetoothGattCharacteristic findWritableCharacteristic(BluetoothGatt g) {
        for (BluetoothGattService service : g.getServices()) {
            UUID serviceId = service.getUuid();
            if (GENERIC_ACCESS_UUID.equals(serviceId) ||
                    GENERIC_ATTRIBUTE_UUID.equals(serviceId)) {
                continue;
            }
            for (BluetoothGattCharacteristic ch : service.getCharacteristics()) {
                if ((ch.getProperties() & PROP_WRITE) != 0) {
                    return ch;
                }
            }
        }
        return null;
    }

    private String shortUuid(UUID uuid) {
        String value = uuid.toString();
        return value.length() > 8 ? value.substring(0, 8) : value;
    }

    @SuppressLint("MissingPermission")
    private void startLocation() {
        if (!hasPermission(Manifest.permission.ACCESS_FINE_LOCATION)) {
            requestNeededPermissions();
            return;
        }
        origin = null;
        anchorSent = false;
        locationManager = (LocationManager) getSystemService(Context.LOCATION_SERVICE);
        status("GPS starting", rx == null ? "Local only" : "Linked");
        locationManager.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000, 1f, locationListener);
        locationManager.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, 1000, 1f, locationListener);
    }

    private final LocationListener locationListener = location -> {
        if (origin == null) origin = new Location(location);
        lastLocation = new Location(location);
        pushGpsToWeb(location);
        if (!anchorSent) {
            send("ANCHOR");
            eval("window.syncLandmarksToWatch && syncLandmarksToWatch();");
            anchorSent = true;
        }
        send(String.format(Locale.US, "GPS,%.6f,%.6f,%.1f,%.0f",
                location.getLatitude(),
                location.getLongitude(),
                location.hasSpeed() ? location.getSpeed() * 2.236936 : 0.0,
                compassHeading));
    };

    private void testMove() {
        if (origin == null) {
            origin = new Location("test");
            origin.setLatitude(44.09785);
            origin.setLongitude(-70.19420);
            anchorSent = false;
        }
        if (testLocation == null) {
            testLocation = new Location(origin);
        }
        testMeters += 8;
        double rad = Math.toRadians(compassHeading);
        double stepMeters = 8.0;
        Location fake = new Location("test");
        fake.setLatitude(testLocation.getLatitude() + Math.cos(rad) * stepMeters / 111320.0);
        fake.setLongitude(testLocation.getLongitude() + Math.sin(rad) * stepMeters / metersPerLon(testLocation.getLatitude()));
        fake.setBearing(compassHeading);
        fake.setAccuracy(3);
        testLocation = new Location(fake);
        lastLocation = new Location(fake);
        pushGpsToWeb(fake);
        if (!anchorSent) {
            sendToBridgeService("ANCHOR");
            anchorSent = true;
        }
        sendToBridgeService(String.format(Locale.US, "GPS,%.6f,%.6f,0.0,%.0f", fake.getLatitude(), fake.getLongitude(), compassHeading));
    }

    private double metersPerLon(double lat) {
        return 111320.0 * Math.cos(Math.toRadians(lat));
    }

    private void pushGpsToWeb(Location location) {
        eval(String.format(Locale.US, "window.nativeGps && nativeGps(%.6f,%.6f,%.1f,%.0f);",
                location.getLatitude(),
                location.getLongitude(),
                location.hasAccuracy() ? location.getAccuracy() : 0.0,
                compassHeading));
    }

    @SuppressLint("MissingPermission")
    private void send(String command) {
        if (gatt == null || rx == null) {
            if (gatt != null && !command.startsWith("GPS,") && !command.startsWith("HEAD,")) {
                writeQueue.offer(command.endsWith("\n") ? command : command + "\n");
                status("Queued " + command.split(",", 2)[0], "Connecting");
                return;
            }
            status("GPS local only", "Not linked");
            return;
        }
        coalesce(command);
        writeQueue.offer(command.endsWith("\n") ? command : command + "\n");
        flushWriteQueue();
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

    @SuppressLint("MissingPermission")
    private void flushWriteQueue() {
        if (gatt == null || rx == null || writeInFlight || writeQueue.isEmpty()) {
            return;
        }
        String command = writeQueue.poll();
        rx.setValue(command.getBytes(StandardCharsets.UTF_8));
        writeInFlight = gatt.writeCharacteristic(rx);
        String label = command.startsWith("HEAD,") ? "Sent heading" : command.startsWith("POI,") ? "Sent landmark" : "Sent GPS to watch";
        status(writeInFlight ? label : "BLE write failed", writeInFlight ? "Linked" : "Failed");
        if (writeInFlight && writeWithoutResponse) {
            main.postDelayed(() -> {
                writeInFlight = false;
                flushWriteQueue();
            }, 70);
        } else if (!writeInFlight) {
            main.postDelayed(this::flushWriteQueue, 120);
        }
    }

    private void status(String gps, String bridge) {
        Log.d("GtaNavMain", "status gps=" + gps + " bridge=" + bridge);
        eval("window.nativeSetStatus && nativeSetStatus(" + quote(gps) + "," + quote(bridge) + ");");
    }

    private String quote(String text) {
        if (text == null) {
            text = "";
        }
        return "'" + text
                .replace("\\", "\\\\")
                .replace("'", "\\'")
                .replace("\r", "\\r")
                .replace("\n", "\\n") + "'";
    }

    private String jsonQuote(String text) {
        if (text == null) text = "";
        return "\"" + text
                .replace("\\", "\\\\")
                .replace("\"", "\\\"")
                .replace("\r", "\\r")
                .replace("\n", "\\n") + "\"";
    }

    private String contactsJson() {
        if (!hasPermission(Manifest.permission.READ_CONTACTS)) {
            requestNeededPermissions();
            return "[]";
        }
        StringBuilder json = new StringBuilder("[");
        Uri uri = ContactsContract.CommonDataKinds.Phone.CONTENT_URI;
        String[] projection = {
                ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME,
                ContactsContract.CommonDataKinds.Phone.NUMBER,
                ContactsContract.CommonDataKinds.Phone.TYPE
        };
        try (Cursor cursor = getContentResolver().query(uri, projection, null, null,
                ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME + " ASC")) {
            int count = 0;
            if (cursor != null) {
                while (cursor.moveToNext() && count < 40) {
                    String name = cursor.getString(0);
                    String number = cursor.getString(1);
                    if (name == null || number == null || number.trim().length() == 0) continue;
                    if (count > 0) json.append(",");
                    json.append("{\"name\":").append(jsonQuote(name))
                            .append(",\"number\":").append(jsonQuote(number))
                            .append(",\"role\":\"Phone contact\"}");
                    count++;
                }
            }
        } catch (Exception error) {
            Log.w("GtaNavMain", "contactsJson failed", error);
        }
        json.append("]");
        return json.toString();
    }

    private String callKind(int type) {
        switch (type) {
            case CallLog.Calls.INCOMING_TYPE:
                return "incoming";
            case CallLog.Calls.OUTGOING_TYPE:
                return "outgoing";
            case CallLog.Calls.MISSED_TYPE:
                return "missed";
            default:
                return "call";
        }
    }

    private String recentCallsJson() {
        if (!hasPermission(Manifest.permission.READ_CALL_LOG)) {
            requestNeededPermissions();
            return "[]";
        }
        StringBuilder json = new StringBuilder("[");
        String[] projection = {
                CallLog.Calls.CACHED_NAME,
                CallLog.Calls.NUMBER,
                CallLog.Calls.TYPE,
                CallLog.Calls.DATE
        };
        try (Cursor cursor = getContentResolver().query(CallLog.Calls.CONTENT_URI, projection, null, null,
                CallLog.Calls.DATE + " DESC")) {
            int count = 0;
            if (cursor != null) {
                while (cursor.moveToNext() && count < 24) {
                    String name = cursor.getString(0);
                    String number = cursor.getString(1);
                    int type = cursor.getInt(2);
                    if ((name == null || name.length() == 0) && number != null) name = number;
                    if (name == null || name.length() == 0) continue;
                    if (count > 0) json.append(",");
                    json.append("{\"name\":").append(jsonQuote(name))
                            .append(",\"number\":").append(jsonQuote(number == null ? "" : number))
                            .append(",\"kind\":").append(jsonQuote(callKind(type))).append("}");
                    count++;
                }
            }
        } catch (Exception error) {
            Log.w("GtaNavMain", "recentCallsJson failed", error);
        }
        json.append("]");
        return json.toString();
    }

    private String galleryItemsJson() {
        String imagePermission = Build.VERSION.SDK_INT >= 33
                ? Manifest.permission.READ_MEDIA_IMAGES
                : Manifest.permission.READ_EXTERNAL_STORAGE;
        if (!hasPermission(imagePermission)) {
            requestNeededPermissions();
            return "[]";
        }
        StringBuilder json = new StringBuilder("[");
        String[] projection = {
                MediaStore.Images.Media.DISPLAY_NAME,
                MediaStore.Images.Media.BUCKET_DISPLAY_NAME,
                MediaStore.Images.Media.DATE_ADDED
        };
        try (Cursor cursor = getContentResolver().query(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, projection, null, null,
                MediaStore.Images.Media.DATE_ADDED + " DESC")) {
            int count = 0;
            if (cursor != null) {
                while (cursor.moveToNext() && count < 24) {
                    String label = cursor.getString(0);
                    String album = cursor.getString(1);
                    long date = cursor.getLong(2);
                    if (count > 0) json.append(",");
                    json.append("{\"label\":").append(jsonQuote(label == null ? "Photo" : label))
                            .append(",\"album\":").append(jsonQuote(album == null ? "Phone Album" : album))
                            .append(",\"date\":").append(jsonQuote(String.valueOf(date))).append("}");
                    count++;
                }
            }
        } catch (Exception error) {
            Log.w("GtaNavMain", "galleryItemsJson failed", error);
        }
        json.append("]");
        return json.toString();
    }

    private String calendarJson() {
        if (!hasPermission(Manifest.permission.READ_CALENDAR)) {
            requestNeededPermissions();
            return "[]";
        }
        long now = System.currentTimeMillis();
        long horizon = now + 14L * 24L * 60L * 60L * 1000L;
        Uri.Builder builder = CalendarContract.Instances.CONTENT_URI.buildUpon();
        android.content.ContentUris.appendId(builder, now);
        android.content.ContentUris.appendId(builder, horizon);
        String[] projection = {
                CalendarContract.Instances.TITLE,
                CalendarContract.Instances.BEGIN,
                CalendarContract.Instances.END,
                CalendarContract.Instances.EVENT_LOCATION
        };
        StringBuilder json = new StringBuilder("[");
        try (Cursor cursor = getContentResolver().query(builder.build(), projection, null, null,
                CalendarContract.Instances.BEGIN + " ASC")) {
            int count = 0;
            if (cursor != null) {
                while (cursor.moveToNext() && count < 12) {
                    if (count > 0) json.append(",");
                    json.append("{")
                            .append("\"title\":").append(jsonQuote(cursor.getString(0))).append(",")
                            .append("\"begin\":").append(cursor.getLong(1)).append(",")
                            .append("\"end\":").append(cursor.getLong(2)).append(",")
                            .append("\"location\":").append(jsonQuote(cursor.getString(3)))
                            .append("}");
                    count++;
                }
            }
        } catch (Exception error) {
            Log.w("GtaNavMain", "calendarJson failed", error);
        }
        json.append("]");
        return json.toString();
    }

    private void openExternalUrl(String url) {
        String target = (url == null || url.trim().length() == 0)
                ? "https://www.playdosgames.com/play/grand-theft-auto"
                : url.trim();
        try {
            Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(target));
            intent.addCategory(Intent.CATEGORY_BROWSABLE);
            startActivity(intent);
        } catch (Exception error) {
            Log.w("GtaNavMain", "openExternalUrl failed", error);
        }
    }

    private void openDialer(String number) {
        String target = (number == null || number.trim().length() == 0) ? "" : number.trim();
        try {
            Intent intent = new Intent(Intent.ACTION_DIAL, Uri.parse("tel:" + Uri.encode(target)));
            startActivity(intent);
        } catch (Exception error) {
            Log.w("GtaNavMain", "openDialer failed", error);
        }
    }

    private void openCameraApp() {
        try {
            Intent intent = new Intent(MediaStore.ACTION_IMAGE_CAPTURE);
            startActivity(intent);
        } catch (Exception error) {
            Log.w("GtaNavMain", "openCameraApp failed", error);
        }
    }

    private void eval(String js) {
        main.post(() -> {
            if (web != null) web.evaluateJavascript(js, null);
        });
    }

    private void startBridgeService(String action) {
        startBridgeService(action, null);
    }

    private void startBridgeService(String action, String address) {
        Intent intent = new Intent(this, GtaNavService.class);
        intent.setAction(action);
        if (address != null && address.length() > 0) {
            intent.putExtra(GtaNavService.EXTRA_ADDRESS, address);
        }
        if (Build.VERSION.SDK_INT >= 26) {
            startForegroundService(intent);
        } else {
            startService(intent);
        }
    }

    private void sendToBridgeService(String command) {
        Intent intent = new Intent(this, GtaNavService.class);
        intent.setAction(GtaNavService.ACTION_SEND);
        intent.putExtra(GtaNavService.EXTRA_COMMAND, command);
        if (Build.VERSION.SDK_INT >= 26) {
            startForegroundService(intent);
        } else {
            startService(intent);
        }
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        int type = event.sensor.getType();
        if (type == Sensor.TYPE_ROTATION_VECTOR || type == Sensor.TYPE_GAME_ROTATION_VECTOR) {
            float[] matrix = new float[9];
            SensorManager.getRotationMatrixFromVector(matrix, event.values);
            updateCompassHeading(headingFromMatrix(matrix));
        } else if (type == Sensor.TYPE_ACCELEROMETER) {
            System.arraycopy(event.values, 0, accelValues, 0, accelValues.length);
            hasAccel = true;
            updateCompassFromMagnetics();
        } else if (type == Sensor.TYPE_MAGNETIC_FIELD) {
            System.arraycopy(event.values, 0, magnetValues, 0, magnetValues.length);
            hasMagnet = true;
            updateCompassFromMagnetics();
        } else if (type == Sensor.TYPE_STEP_COUNTER && event.values.length > 0) {
            float rawSteps = event.values[0];
            if (stepCounterBase < 0f || rawSteps < stepCounterBase) {
                stepCounterBase = rawSteps;
            }
            latestSteps = Math.max(0, Math.round(rawSteps - stepCounterBase));
            latestStepMiles = latestSteps * 2.5f / 5280f;
            latestStepCalories = Math.round(latestSteps * 0.04f);
            latestActiveMinutes = Math.max(0, latestSteps / 100);
        }
    }

    private void updateCompassFromMagnetics() {
        if (!hasAccel || !hasMagnet) {
            return;
        }
        float[] matrix = new float[9];
        if (!SensorManager.getRotationMatrix(matrix, null, accelValues, magnetValues)) {
            return;
        }
        updateCompassHeading(headingFromMatrix(matrix));
    }

    private float headingFromMatrix(float[] matrix) {
        float[] adjusted = new float[9];
        int rotation = getWindowManager().getDefaultDisplay().getRotation();
        switch (rotation) {
            case Surface.ROTATION_90:
                SensorManager.remapCoordinateSystem(matrix, SensorManager.AXIS_Y, SensorManager.AXIS_MINUS_X, adjusted);
                break;
            case Surface.ROTATION_180:
                SensorManager.remapCoordinateSystem(matrix, SensorManager.AXIS_MINUS_X, SensorManager.AXIS_MINUS_Y, adjusted);
                break;
            case Surface.ROTATION_270:
                SensorManager.remapCoordinateSystem(matrix, SensorManager.AXIS_MINUS_Y, SensorManager.AXIS_X, adjusted);
                break;
            default:
                System.arraycopy(matrix, 0, adjusted, 0, matrix.length);
                break;
        }
        float[] orientation = new float[3];
        SensorManager.getOrientation(adjusted, orientation);
        return (float) ((Math.toDegrees(orientation[0]) + 360.0) % 360.0);
    }

    private void updateCompassHeading(float heading) {
        compassHeading = heading;
        eval(String.format(Locale.US, "window.nativeHeading && nativeHeading(%.0f);", compassHeading));
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
    }

    public class NativeBridge {
        @JavascriptInterface
        public void connectWatch() {
            main.post(() -> {
                startBridgeService(GtaNavService.ACTION_SCAN);
                status("Scanning. Pick GTA-Nav watch from list.", "Scanning");
            });
        }

        @JavascriptInterface
        public void startGps() {
            main.post(() -> {
                startBridgeService(GtaNavService.ACTION_START_GPS);
                status("Persistent GPS bridge starting", "Connecting");
            });
        }

        @JavascriptInterface
        public void testMove() {
            main.post(MainActivity.this::testMove);
        }

        @JavascriptInterface
        public void sendCommand(String command) {
            main.post(() -> sendToBridgeService(command));
        }

        @JavascriptInterface
        public void connectDevice(String address) {
            main.post(() -> {
                startBridgeService(GtaNavService.ACTION_CONNECT, address);
                status("Persistent watch bridge starting", "Connecting");
            });
        }

        @JavascriptInterface
        public String getAssetText(String path) {
            String safePath = path == null ? "" : path.replace("\\", "/");
            if (safePath.startsWith("/") || safePath.contains("..")) {
                return "";
            }
            try (InputStream in = getAssets().open("dashboard/" + safePath);
                 ByteArrayOutputStream out = new ByteArrayOutputStream()) {
                byte[] buffer = new byte[8192];
                int read;
                while ((read = in.read(buffer)) != -1) {
                    out.write(buffer, 0, read);
                }
                return out.toString(StandardCharsets.UTF_8.name());
            } catch (Exception error) {
                return "";
            }
        }

        @JavascriptInterface
        public String getGpsStatus() {
            return GtaNavService.lastGpsStatus;
        }

        @JavascriptInterface
        public String getBridgeStatus() {
            return GtaNavService.lastBridgeStatus;
        }

        @JavascriptInterface
        public String getDebugLog() {
            return GtaNavService.lastDebugLog;
        }

        @JavascriptInterface
        public boolean hasLastLocation() {
            return GtaNavService.hasLastLocation;
        }

        @JavascriptInterface
        public double getLastLatitude() {
            return GtaNavService.lastLatitude;
        }

        @JavascriptInterface
        public double getLastLongitude() {
            return GtaNavService.lastLongitude;
        }

        @JavascriptInterface
        public float getLastAccuracy() {
            return GtaNavService.lastAccuracy;
        }

        @JavascriptInterface
        public float getLastHeading() {
            return GtaNavService.lastHeading;
        }

        @JavascriptInterface
        public int getStepCount() {
            return latestSteps;
        }

        @JavascriptInterface
        public float getStepMiles() {
            return latestStepMiles;
        }

        @JavascriptInterface
        public int getStepCalories() {
            return latestStepCalories;
        }

        @JavascriptInterface
        public int getActiveMinutes() {
            return latestActiveMinutes;
        }

        @JavascriptInterface
        public String getPhoneContactsJson() {
            return contactsJson();
        }

        @JavascriptInterface
        public String getRecentCallsJson() {
            return recentCallsJson();
        }

        @JavascriptInterface
        public String getGalleryItemsJson() {
            return galleryItemsJson();
        }

        @JavascriptInterface
        public String getCalendarEventsJson() {
            return calendarJson();
        }

        @JavascriptInterface
        public void openBrowser(String url) {
            main.post(() -> openExternalUrl(url));
        }

        @JavascriptInterface
        public void openDialer(String number) {
            main.post(() -> openDialer(number));
        }

        @JavascriptInterface
        public void openCameraApp() {
            main.post(MainActivity.this::openCameraApp);
        }
    }
}
