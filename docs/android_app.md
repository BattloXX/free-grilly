# Free Grilly — Android App: API Guide & Architecture

> **Status:** API contract document for the separate Android app build.  
> **Firmware:** free-grilly (BattloXX fork), v25.06+  
> **Contract file:** `docs/openapi.yaml`

---

## Table of Contents

1. [Overview](#1-overview)
2. [Provisioning Flow (AP-mode Setup)](#2-provisioning-flow)
3. [Discovery on the Home Network (mDNS / NSD)](#3-discovery-mdns--nsd)
4. [REST API Reference](#4-rest-api-reference)
5. [Alarm & Notifications Flow](#5-alarm--notifications-flow)
6. [Temperature History & Graphs](#6-temperature-history--graphs)
7. [Recommended App Architecture](#7-recommended-app-architecture)
8. [Future Extension Points](#8-future-extension-points)

---

## 1. Overview

The Grilleye Max runs an embedded web server (port 80) that exposes a JSON REST API.
The Android app communicates exclusively over HTTP — no cloud, no proprietary protocol.

```
┌──────────────┐  HTTP/80   ┌──────────────────────────┐
│  Android App │ ─────────► │  Grilleye Max  (ESP32)   │
│              │ ◄───────── │  Free Grilly firmware     │
└──────────────┘  JSON      └──────────────────────────┘
```

Two connectivity phases exist:

| Phase | Network | Purpose |
|-------|---------|---------|
| **Setup** | Grilleye AP (`FreeGrilly_<mac6>`) | Provision home Wi-Fi credentials & device name |
| **Operate** | Home Wi-Fi | Real-time temperature monitoring & control |

---

## 2. Provisioning Flow

### 2.1 Connect to the Device AP

The Grilleye creates its own access point when not yet connected to a home network
(or when reset). Default SSID follows the pattern `FreeGrilly_<mac6>`, where `<mac6>`
is the last 6 hex digits of the device's Wi-Fi MAC address (see `generate_hostname()`
in `lib/Util/Util.cpp`).

**Android steps:**
1. Ask the user to manually connect to the `FreeGrilly_…` Wi-Fi **or** use
   `WifiManager.addNetwork` + `WifiManager.enableNetwork` (requires
   `ACCESS_WIFI_STATE`, `CHANGE_WIFI_STATE`).
2. Device IP in AP mode: `192.168.200.10`.

### 2.2 Scan for Available Networks

```http
GET http://192.168.200.10/api/wifiscan
```

**Response:**
```json
[
  { "ssid": "HomeNetwork", "rssi": -55, "encryption": "WPA2" },
  { "ssid": "Neighbor",    "rssi": -80, "encryption": "WPA2" }
]
```

*(Scan may take 2–4 seconds; poll once per second until the array is non-empty.)*

### 2.3 Push Credentials & Name

```http
POST http://192.168.200.10/api/settings
Content-Type: application/json

{
  "wifi_ssid":     "HomeNetwork",
  "wifi_password": "secret",
  "grill_name":    "My BBQ",
  "temperature_unit": "celcius"
}
```

**Response:** `{ "success": true }` (or `{ "success": false, "message": "..." }`)

After a successful POST the device reboots into STA mode and joins the home network.
The AP disappears within ~5 seconds.

### 2.4 Provisioning State Machine

```
[App launched]
      │
      ├─ Device already known? ──Yes──► [Discovery / Operate]
      │
      No
      │
      ▼
[Guide user: connect to FreeGrilly_xxxx AP]
      │
      ▼
[GET /api/wifiscan] ──── show list ────►
      │                                 [User picks network + enters password]
      ▼
[POST /api/settings  { ssid, password, name }]
      │
      ▼
[Wait for device to reboot into STA mode (~8 s)]
      │
      ▼
[Re-join home Wi-Fi, run NSD Discovery]
      │
      ▼
[Dashboard]
```

---

## 3. Discovery — mDNS / NSD

Once the device is on the home network it advertises itself via mDNS.

| Property | Value |
|----------|-------|
| Hostname | `free-grilly-<uuid8>.local` |
| Service type | `_http._tcp` (port 80) |
| Service type | `_free-grilly._tcp` (port 80) |
| TXT: `uuid` | Full device UUID |
| TXT: `name` | Human-readable grill name |
| TXT: `fw` | Firmware version |

### Android NSD snippet (Kotlin)

```kotlin
val nsdManager = getSystemService(NSD_SERVICE) as NsdManager

val discoveryListener = object : NsdManager.DiscoveryListener {
    override fun onServiceFound(service: NsdServiceInfo) {
        if (service.serviceType == "_http._tcp.") {
            nsdManager.resolveService(service, resolveListener)
        }
    }
    // … onStartDiscoveryFailed, onDiscoveryStarted, onDiscoveryStopped, onServiceLost
}

nsdManager.discoverServices("_free-grilly._tcp", NsdManager.PROTOCOL_DNS_SD, discoveryListener)
```

After resolving, confirm the device with:

```http
GET http://<resolved-ip>/api/info
```

```json
{
  "uuid":          "a1b2c3d4-...",
  "name":          "My BBQ",
  "firmware":      "25.06.28",
  "mdns_hostname": "free-grilly-a1b2c3d4",
  "capabilities":  ["history", "alarm_mute", "eta"]
}
```

Store the IP (and optionally hostname) for subsequent requests.  
Cache it — NSD resolution is slow (~500 ms). Refresh if a request times out.

---

## 4. REST API Reference

Base URL: `http://<device-ip>` (relative path from device-local web UI).

### 4.1 `GET /api/grill` — Main Status

Polls at 1 s intervals for live temperature data.

**Response schema:**
```json
{
  "name":              "My BBQ",
  "uuid":              "a1b2c3d4-e5f6-...",
  "temperature_unit":  "celcius",
  "battery_percentage": 82,
  "battery_charging":  false,
  "wifi_connected":    true,
  "wifi_signal":       -65,
  "alarm_active":      false,
  "mdns_hostname":     "free-grilly-a1b2c3d4",
  "probes": [
    {
      "id":                   1,
      "name":                 "Brisket",
      "connected":            true,
      "temperature":          87.5,
      "target_temperature":   90.0,
      "minimum_temperature":  0.0,
      "alarm":                false,
      "eta_seconds":          840
    }
    /* … probes 2–8 … */
  ]
}
```

**Field notes:**

| Field | Notes |
|-------|-------|
| `eta_seconds` | Seconds until `target_temperature` is reached (linear regression). `-1` = unknown / temperature falling. `0` = target reached. |
| `alarm_active` | `true` when any probe has `alarm: true`. |
| `alarm` (per probe) | `true` when `temperature >= target_temperature` (or outside min–max range). |
| `wifi_signal` | dBm (negative). Map to %: `percent = 140 + dBm` (clamped 0–100). |
| `minimum_temperature` | `0` = single-target mode; `>0` = range mode (keep between min and target). |

---

### 4.2 `GET /api/probes` — Probe Configuration

```json
[
  {
    "id":                  1,
    "name":                "Brisket",
    "type":                "meat",
    "target_temperature":  90.0,
    "minimum_temperature": 0.0,
    "beep":                true
  }
  /* … */
]
```

### 4.3 `POST /api/probes` — Update Probe Configuration

```http
POST /api/probes
Content-Type: application/json

[
  {
    "id":                  1,
    "name":                "Brisket",
    "type":                "meat",
    "target_temperature":  90.0,
    "minimum_temperature": 0.0,
    "beep":                true
  }
]
```

**Response:** `{ "success": true }`

---

### 4.4 `GET /api/settings` — Device Settings

```json
{
  "grill_name":              "My BBQ",
  "wifi_ssid":               "HomeNetwork",
  "temperature_unit":        "celcius",
  "backlight_timeout_minutes": 3,
  "screen_timeout_minutes":  0,
  "power_saving":            true
}
```

> **`power_saving`** (boolean, default `true`) — battery mode toggle. `true` = "max battery"
> (WiFi modem-sleep, reduced TX power, the setup SoftAP is dropped once the home network
> is joined, slower polling, default display timeout). `false` = "always reachable" (radio
> stays awake, SoftAP keeps running). Note: in `power_saving` mode the device is **only**
> reachable over the home Wi-Fi after setup — the `FreeGrilly_…` AP is no longer available
> until a factory reset.

> **Partial updates are safe:** `POST /api/settings` only changes the fields present in the
> body; omitted fields keep their current value.

### 4.5 `POST /api/settings` — Update Device Settings

Same structure as GET response. Only fields present in the body are updated.

---

### 4.6 `GET /api/probes/history` — Temperature History

Returns the last ≤60 readings per probe (sampled every 10 seconds, so ≤10 minutes of history).
Values are **celsius × 10** (integer) to save bandwidth.

```json
{
  "interval_seconds": 10,
  "probes": [
    {
      "id":      1,
      "name":    "Brisket",
      "history": [850, 855, 862, 870, 878, 887]
    }
    /* … */
  ]
}
```

**Usage:** divide each value by 10.0 to get °C.  
For a richer graph seed: fetch once on app open, then extend client-side from `/api/grill` polling.

---

### 4.7 `POST /api/alarm/mute` — Mute Active Alarm

Silences the buzzer and web alarm banner until the next alarm trigger.

```http
POST /api/alarm/mute
Content-Type: application/json
{}
```

**Response:** `{ "success": true }`

Also send `OPTIONS /api/alarm/mute` is available for CORS pre-flight.

---

### 4.8 `GET /api/info` — Device Identity

```json
{
  "uuid":          "a1b2c3d4-e5f6-...",
  "name":          "My BBQ",
  "firmware":      "25.06.28",
  "mdns_hostname": "free-grilly-a1b2c3d4",
  "capabilities":  ["history", "alarm_mute", "eta"]
}
```

Use `capabilities` to handle older firmware gracefully (feature-flag the UI).

---

### 4.9 `GET /api/wifiscan` — Available Networks

(Also used during provisioning — see §2.2.)

---

## 5. Alarm & Notifications Flow

```
App polls GET /api/grill every 1 s
        │
        ├─ alarm_active == false ──────────────────────────────────►  Normal UI
        │
        └─ alarm_active == true
                │
                ├─ Show in-app alarm banner (red, with probe name)
                ├─ Fire Android local notification (if app is in background)
                │     title:  "Probe ready: <probe_name>"
                │     body:   "<probe_name> reached <target>°C"
                │     action: "Mute" → POST /api/alarm/mute
                │
                └─ User taps Mute → POST /api/alarm/mute
                        │
                        └─ Clear alarm UI, await next alarm_active == true
```

**Android permissions required:**
- `POST_NOTIFICATIONS` (Android 13+)
- Request at runtime before first alarm.

---

## 6. Temperature History & Graphs

### Strategy

| Source | Update rate | Use case |
|--------|-------------|----------|
| `GET /api/grill` every 1 s | 1 sample/s (client-side accumulation) | Live sparkline, last 2 min |
| `GET /api/probes/history` | on-demand | Initial graph seed on app open, last 10 min |

**Recommended approach:**

1. On screen open: `GET /api/probes/history` → seed in-memory ring buffer per probe.
2. Every polling cycle: append `temperature` from `/api/grill` → extend buffer.
3. Buffer size: 600 entries (= 10 min at 1 sample/s).
4. Render with MPAndroidChart (or Compose Canvas) — x-axis = time offsets, y-axis = °C.

**Target line:** draw a horizontal dashed line at `target_temperature` in a contrasting colour.

---

## 7. Recommended App Architecture

### 7.1 Stack

| Concern | Library / approach |
|---------|-------------------|
| Language | **Kotlin** |
| UI | **Jetpack Compose** |
| HTTP | **Retrofit 2 + OkHttp 4** |
| JSON | **Kotlin Serialization** (or Moshi/Gson) |
| DI | **Hilt** |
| State | **ViewModel + StateFlow** |
| Graphs | **MPAndroidChart** or Compose Canvas |
| NSD | `android.net.nsd.NsdManager` |
| Notifications | `NotificationCompat` + `NotificationChannel` |
| Wi-Fi config | `WifiNetworkSpecifier` (API 29+) or guide user |

### 7.2 Module Structure

```
app/
├── ui/
│   ├── onboarding/       ← AP connect, Wi-Fi scan, provisioning
│   ├── dashboard/        ← probe cards, alarm banner, battery badge
│   ├── probe_detail/     ← full-screen graph + ETA + settings per probe
│   └── settings/         ← device name, unit, timeouts
├── data/
│   ├── GrillyRepository  ← poll loop, history buffer, mute
│   ├── NsdDiscovery      ← device discovery
│   └── api/
│       ├── GrillyApiService.kt   ← Retrofit interface
│       └── models/               ← GrillStatus, Probe, HistoryResponse, …
└── domain/
    ├── EtaFormatter      ← "in 1h 24m" / "Ready!"
    └── AlarmManager      ← notification posting, mute state
```

### 7.3 Main Dashboard Loop

```kotlin
// GrillyRepository
val statusFlow: StateFlow<GrillStatus> = flow {
    while (true) {
        try {
            emit(api.getGrillStatus())
        } catch (e: IOException) {
            emit(GrillStatus.Disconnected)
        }
        delay(1_000)
    }
}.stateIn(scope, SharingStarted.WhileSubscribed(5_000), GrillStatus.Loading)
```

### 7.4 Provisioning Screen Flow

```
OnboardingScreen
    │
    ├── WifiScanScreen   (GET /api/wifiscan, shows list)
    │
    ├── CredentialsScreen (enter password, grill name)
    │
    └── ProvisioningScreen (POST /api/settings, progress indicator)
            │
            └── [success] ─► NsdDiscoveryScreen ─► DashboardScreen
```

---

## 8. Future Extension Points

These hooks are **prepared in firmware** but not yet active. Implement them as opt-in
capabilities in the app (check `capabilities` array from `/api/info`).

| Feature | Mechanism | Notes |
|---------|-----------|-------|
| **Push / real-time updates** | Server-Sent Events (`GET /api/events`) or WebSocket | Eliminates 1 s polling — better battery on phone side |
| **Power saving mode** | `POST /api/settings { "power_saving": true }` | Reduces device poll rate / Wi-Fi duty cycle |
| **Multi-device** | NSD discovers multiple `_free-grilly._tcp` instances | Each device independent; app shows device picker |
| **OTA via app** | `PUT /update` (ElegantOTA-compatible) | Allows in-app firmware updates |

---

*Contract version: 1.0 — 2025-06 (free-grilly BattloXX fork)*  
*See `docs/openapi.yaml` for the machine-readable OpenAPI 3.0 spec.*
