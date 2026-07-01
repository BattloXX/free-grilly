#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include <WebServer.h>
#include <Wire.h>
#include <ElegantOTA.h>
#include <esp_system.h>
#include <string>

#include "Api.h"
#include "Buzzer.h"
#include "GrillConfig.h"
#include "JsonUtilities.h"
#include "Mqtt.h"
#include "Network.h"
#include "Opengrill.h"
#include "Power.h"
#include "Preferences.h"
#include "Website.h"
#include "Web.h"
#include "Display.h"

// ************************************
// * Config.h initializes variables
// ************************************
#include "Settings.h"

// Task functions
void task_alarm(void* pvParameters);
void task_battery(void* pvParameters);
void task_opengrill(void* pvParameters);
void task_mqtt(void* pvParameters);
void task_powerbutton(void* pvParameters);
void task_probes(void* pvParameters);
void task_screen(void* pvParameters);
void task_webserver(void* pvParameters);
void task_stackmonitor(void* pvParameters);

// ***********************************
// * Power / diagnostics helpers
// ***********************************

// Human-readable form of esp_reset_reason() for logs and the status API.
static const char* reset_reason_str(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "poweron";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_SW:        return "sw";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:   return "int_wdt";
        case ESP_RST_TASK_WDT:  return "task_wdt";
        case ESP_RST_WDT:       return "wdt";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_EXT:       return "ext";
        case ESP_RST_SDIO:      return "sdio";
        default:                return "unknown";
    }
}

// Record why the device is about to power off, both in RAM (for the current status response)
// and in NVS so it survives the deep sleep and can be shown after the next wake.
static void record_off_reason(const char* reason) {
    config::last_off_reason = reason;
    config::settings_storage.putString("last_off", reason);
}

// Debounced read of the power button. GPIO35 is input-only and has no internal pull, so a
// single digitalRead can catch electrical noise (moisture/EMI on a grill device). Require
// several consecutive LOW samples before treating the button as pressed, so a brief glitch
// can no longer masquerade as a press (and, at 2-10 s, trigger a shutdown).
static bool button_is_low(void) {
    for (int i = 0; i < 3; i++) {
        if (digitalRead(gpio::power_button) == HIGH) return false;
        delay(5);
    }
    return true;
}

void setup() {

    // ***********************************
    // * Serial
    // ***********************************

    Serial.begin(115200); // Initialize serial communication at 115200 bits per second
    // delay(5000);          // Give serial monitor time to catch up

    // ***********************************
    // * Load nvram settings and init
    // ***********************************
    config::settings_storage.begin("free-grilly", false);
    config::config_helper.load_settings();
    config::config_helper.load_probes();

    // ***********************************
    // * Power button bootup
    // ***********************************

    //* Power button pin is set here so that we can use it to check for boot
    pinMode(gpio::power_button, INPUT);

    // Why did we (re)boot? The "hold the button to turn on" gate below must ONLY apply to
    // deliberate power-on events: a cold power-up (ESP_RST_POWERON, e.g. battery inserted)
    // or a wake from our own deep sleep (ESP_RST_DEEPSLEEP, button via ext0). Any other
    // reset — brownout, panic, watchdog, ... — means the device was ALREADY running and
    // reset unexpectedly. In that case we must resume operation instead of going (back) to
    // sleep, otherwise a transient fault looks to the user like "it turned itself off".
    esp_reset_reason_t reset_reason = esp_reset_reason();
    config::last_reset_reason = reset_reason_str(reset_reason);
    config::last_off_reason   = config::settings_storage.getString("last_off", "");
    Serial.print("Reset reason: ");
    Serial.println(config::last_reset_reason);

    bool deliberate_power_on = (reset_reason == ESP_RST_POWERON ||
                               reset_reason == ESP_RST_DEEPSLEEP);

    if (deliberate_power_on) {
        unsigned long millis_pressed        = 0;
        unsigned long millis_button_start   = 0;

        int bootup_press_time   = config::press_seconds_startup * 1000;

        if(button_is_low()){
            millis_button_start = millis();
        }

        while(button_is_low()){
            millis_pressed = millis() - millis_button_start;

            // beep if the button is held long enough
            if(millis_pressed > bootup_press_time){
                grill::buzzer.beep(1, 200);
                break;
            }
        }

        if(millis_pressed < bootup_press_time){
            record_off_reason("boot_gate");
            power.shutdown();
        }
    } else {
        // Unexpected reset while running -> self-recover, keep running.
        Serial.println("Unexpected reset -> resuming without power-on gate");
    }

    // ***********************************
    // * Startup Buzzer
    // ***********************************

    grill::buzzer.beep(2, 100);

    // ***********************************
    // * SPI for probes
    // ***********************************

    SPISettings spiSettings(config::hspi_probes_clockspeed, MSBFIRST, SPI_MODE0);
    SPI.begin(gpio::hspi_probes_sclk, gpio::hspi_probes_miso, -1, gpio::hspi_probes_cs);
    SPI.beginTransaction(spiSettings);

    pinMode(gpio::hspi_probes_cs, OUTPUT); // Prep CS line for data reading

    // ***********************************
    // * Launch DEVICE tasks
    // ***********************************

    delay(100); //Needed to give the power rail time to adjust
    xTaskCreatePinnedToCore(task_battery, "Battery", task::batteryStackSize, NULL, 1, &task::batteryTask, 1);
    delay(100); //Needed to give the power rail time to adjust
    xTaskCreatePinnedToCore(task_screen, "Screen", task::screenStackSize, NULL, 1, &task::screenTask, 1);
    delay(1000); //Needed to give the power rail time to adjust
    xTaskCreatePinnedToCore(task_powerbutton, "PowerButton", task::powerbuttonStackSize, NULL, 1, &task::powerbuttonTask, 1);
    delay(100); //Needed to give the power rail time to adjust
    xTaskCreatePinnedToCore(task_probes, "Probes", task::probesStackSize, NULL, 1, &task::probesTask, 1);
    xTaskCreatePinnedToCore(task_alarm, "Alarm", task::alarmStackSize, NULL, 1, &task::alarmTask, 1);

    // ***********************************
    // * WIFI
    // ***********************************

    // Event handlers
    WiFi.onEvent(event_wifi_connected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
    WiFi.onEvent(event_wifi_ip_acquired, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(event_wifi_disconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    WiFi.disconnect(true);  // Remove stale settings
    delay(100);             // Delay for stability
    WiFi.mode(WIFI_AP_STA); // AP + STATION (AP is dropped later in power-saving mode once connected)

    if (config::power_saving) {
        // "Max battery": let the radio sleep between DTIM beacons (this is the standard
        // Arduino modem-sleep, safe for incoming HTTP). TX power is left at full — dropping
        // it to 11 dBm made connections flaky/unreachable. The SoftAP is stopped once the
        // home network is joined (see task_webserver) to cut continuous beaconing.
        WiFi.setSleep(WIFI_PS_MIN_MODEM);
    } else {
        // "Always reachable": keep the radio fully awake for stable incoming HTTP
        // connections and keep the SoftAP running permanently.
        WiFi.setSleep(false);
    }

    start_local_ap();
    delay(1000);            //Needed to give the power rail time to adjust

    if(config::wifi_ssid != ""){
        Serial.println("Waiting a bit before STA connect...");
        delay(800);         // Extra breathing room for the power rail on cold boot
        connect_to_wifi();
    }

    // ***********************************
    // * Launch NETWORK tasks
    // ***********************************
    xTaskCreatePinnedToCore(task_webserver, "Webserver", task::webserverStackSize, NULL, 1, &task::webserverTask, 1);
    xTaskCreatePinnedToCore(task_mqtt, "Mqtt", task::mqttStackSize, NULL, 1, &task::mqttTask, 1);
    xTaskCreatePinnedToCore(task_opengrill, "Opengrill", task::opengrillStackSize, NULL, 1, &task::opengrillTask, 1);
    // xTaskCreatePinnedToCore(task_stackmonitor, "StackMonitor", task::stackmonitorStackSize, NULL, 1, &task::stackmonitorTask, 1);
}

// ***********************************
// * TASKS
// ***********************************

// ***********************************
// * API / WEB
// ***********************************

void task_webserver(void* pvParameters) {
    Serial.println("Launching task :: WEBSERVER / API");
    delay(5);   //Give FreeRtos a chance to properly schedule the task

    setup_api_routes();
    setup_web_routes();

    web::webserver.enableCORS();
    web::webserver.onNotFound(not_found);
    web::webserver.begin();

    ElegantOTA.begin(&web::webserver); // OTA webserver

    // ***********************************
    // * Phase 5: mDNS Discovery
    // * Hostname: free-grilly-<first 8 chars of UUID>
    // * Browsers: http://free-grilly-xxxxxxxx.local
    // * Android NSD: service type "_free-grilly._tcp"
    // ***********************************
    if (config::grill_uuid.length() >= 8) {
        config::mdns_hostname = "free-grilly-" + config::grill_uuid.substring(0, 8);
    } else {
        config::mdns_hostname = "free-grilly";
    }

    if (MDNS.begin(config::mdns_hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("free-grilly", "tcp", 80);
        // TXT records on _http._tcp (legacy / web browsers)
        MDNS.addServiceTxt("http", "tcp", "uuid", config::grill_uuid.c_str());
        MDNS.addServiceTxt("http", "tcp", "name", config::grill_name.c_str());
        MDNS.addServiceTxt("http", "tcp", "fw",   config::grill_firmware_version.c_str());
        // TXT records on _free-grilly._tcp — Android app reads "uuid" from this service type
        MDNS.addServiceTxt("free-grilly", "tcp", "uuid", config::grill_uuid.c_str());
        MDNS.addServiceTxt("free-grilly", "tcp", "name", config::grill_name.c_str());
        MDNS.addServiceTxt("free-grilly", "tcp", "fw",   config::grill_firmware_version.c_str());
        Serial.println("mDNS started: " + config::mdns_hostname + ".local");
    } else {
        Serial.println("mDNS: failed to start");
    }

    bool softap_disabled = false;

    while (true){
        web::webserver.handleClient();
        ElegantOTA.loop();

        // Power-saving: once on the home network, stop the SoftAP to cut continuous
        // beaconing. Use softAPdisconnect (NOT WiFi.mode(WIFI_STA)) — switching mode at
        // runtime reinitialises the netif and kills the mDNS responder the app discovers
        // the device through. softAPdisconnect stops the AP while STA + mDNS stay up.
        // Re-provisioning then needs a reboot/reset (acceptable in "max battery" mode).
        if (config::power_saving && !softap_disabled && grill::wifi_connected) {
            Serial.println("Power-saving: WiFi connected — stopping SoftAP (STA + mDNS stay up)");
            WiFi.softAPdisconnect(true);
            softap_disabled = true;
        }

        delay(5);   // small yield; the synchronous WebServer must service sockets promptly
    }
}

// ***********************************
// * Opengrill
// ***********************************

void task_opengrill(void* pvParameters) {
    Serial.println("Launching task :: Opengrill");
    delay(5);   //Give FreeRtos a chance to properly schedule the task

    String opengrill_server = "";
    int opengrill_port = 1883;

    unsigned long last_publish_time = 0;
    const unsigned long publish_interval_ms = 1000;

    while (true){

        if(opengrill_server != config::opengrill_server || opengrill_port != config::opengrill_port){
            Serial.println("(Re)loaded Opengrill Settings");
            // Settings have changed. We have to update our vars and set a new client
            // This can also be used for launching since we initialize empty vars - also i'm lazy
            opengrill_server = config::opengrill_server;
            opengrill_port = config::opengrill_port;

            config::opengrill_client.setup(opengrill_server, opengrill_port);

            // Only loop/reconnect if we have a broker filled in
            if(opengrill_server != ""){
                Serial.println("Opengrill server set, initializing connection");
                config::opengrill_client.reconnect();
            } else {
                Serial.println("Opengrill server not set, skipping Opengrill connection");
            }
        }

        if(opengrill_server != "" && config::opengrill_client.connected()){
            config::opengrill_client.loop();

            unsigned long now = millis();
            if (now - last_publish_time >= publish_interval_ms) {
                last_publish_time = now;
                config::opengrill_client.publish_grill();
            }
        }

        if(opengrill_server != "" && !config::opengrill_client.connected() && grill::wifi_connected){
            Serial.println("Opengrill client disconnected, trying to reconnect");
            config::opengrill_client.reconnect();
        }

        // Idle slowly when no broker is configured (the common case) instead of
        // waking 20×/s for nothing.
        delay(opengrill_server == "" ? 1000 : 50);
    }
}

// ***********************************
// * MQTT
// ***********************************

void task_mqtt(void* pvParameters) {
    Serial.println("Launching task :: MQTT");
    delay(5);   //Give FreeRtos a chance to properly schedule the task

    String mqtt_broker = "";
    int mqtt_port = 1883;

    unsigned long last_mqtt_publish_time = 0;
    const unsigned long mqtt_publish_interval_ms = 1000;

    while (true){

        if(mqtt_broker != config::mqtt_broker || mqtt_port != config::mqtt_port){
            Serial.println("(Re)loaded MQTT Settings");
            // Settings have changed. We have to update our vars and set a new client
            // This can also be used for launching since we initialize empty vars - also i'm lazy
            mqtt_broker = config::mqtt_broker;
            mqtt_port = config::mqtt_port;

            config::mqtt_client.setup(mqtt_broker, mqtt_port);

            // Only loop/reconnect if we have a broker filled in
            if(mqtt_broker != ""){
                Serial.println("MQTT broker set, initializing connection");
                config::mqtt_client.reconnect();
            } else {
                Serial.println("MQTT broker not set, skipping MQTT connection");
            }
        }

        if(mqtt_broker != "" && config::mqtt_client.connected()){
            config::mqtt_client.loop();

            unsigned long now = millis();
            if (now - last_mqtt_publish_time >= mqtt_publish_interval_ms) {
                last_mqtt_publish_time = now;
                config::mqtt_client.publish_grill();
            }
        }

        if(mqtt_broker != "" && !config::mqtt_client.connected() && grill::wifi_connected){
            Serial.println("MQTT client disconnected, trying to reconnect");
            config::mqtt_client.reconnect();
        }

        // Idle slowly when no broker is configured (the common case) instead of
        // waking 20×/s for nothing.
        delay(mqtt_broker == "" ? 1000 : 50);
    }
}

// ***********************************
// * Buzzer alarm
// ***********************************

void task_alarm(void* pvParameters) {
    Serial.println("Launching task :: Alarm");
    delay(5);   //Give FreeRtos a chance to properly schedule the task

    int alarm_beep_todo = 0;  // The amount of beeps remaining when sounding the alarm

    while (true){

        int alarm = 0;  // Counter for easy checks to see if there is an alarm

        //* Check for alarms
        if(grill::probe_1.alarm){ alarm++; };
        if(grill::probe_2.alarm){ alarm++; };
        if(grill::probe_3.alarm){ alarm++; };
        if(grill::probe_4.alarm){ alarm++; };
        if(grill::probe_5.alarm){ alarm++; };
        if(grill::probe_6.alarm){ alarm++; };
        if(grill::probe_7.alarm){ alarm++; };
        if(grill::probe_8.alarm){ alarm++; };

        //* Trigger alarms if needed
        if(alarm > 0 && alarm_beep_todo == 0){
            alarm_beep_todo = config::alarm_beep_amount;
        }

        //* Mute alarms if needed
        if(config::alarm_mute == true){
            // When we need to mute we remove all needed alarms and wait
            // for 2 seconds for the probes and other devices to catch up
            delay(2000);

            alarm_beep_todo = 0;
            config::alarm_mute = false;
        }

        if(alarm_beep_todo > 0){
            alarm_beep_todo--;
            if(config::cucaracha_enabled)
                grill::buzzer.play_cucaracha();
            else
                grill::buzzer.beep(1, config::alarm_beep_duration_ms);
        }

        delay(100);
    }

}
// ***********************************
// * Power Button
// ***********************************

void task_powerbutton(void* pvParameters) {
    Serial.println("Launching task :: POWER BUTTON");
    delay(5);   //Give FreeRtos a chance to properly schedule the task

    // Reference timers
    unsigned long millis_pressed        = 0;
    unsigned long millis_current        = 0;
    unsigned long millis_button_start   = 0;

    // Time in ms that defines each button press breakpoint
    int short_press_time   = 1000;
    int medium_press_time  = config::press_seconds_shutdown * 1000;
    int long_press_time    = config::press_seconds_factory_reset * 1000;

    bool button_pressed    = false;
    bool buzzed_short      = false;
    bool buzzed_medium     = false;
    bool buzzed_long       = false;

    if(button_is_low()){
        millis_button_start = millis();
    }

    while(true){
        // Single debounced read per iteration (avoids the pin being sampled inconsistently
        // three times within one loop, and rejects short electrical glitches).
        bool is_low = button_is_low();

        if(is_low && not button_pressed) {
            // Initialize millis counter
            button_pressed = true;
            millis_button_start = millis();
        } else if(is_low){
            millis_pressed = millis() - millis_button_start;

            // beep if the button is held long enough to indicate the action
            if(millis_pressed > short_press_time && buzzed_short == false){
                buzzed_short = true;
                grill::buzzer.beep(2, 100);
            }
            if(millis_pressed > medium_press_time && buzzed_medium == false){
                buzzed_medium = true;
                grill::buzzer.beep(3, 100);
            }
            if(millis_pressed > long_press_time && buzzed_long == false){
                buzzed_long = true;
                grill::buzzer.beep(3, 500);
            }
        }
        else if (!is_low && button_pressed)
        {
            button_pressed = false;
            buzzed_short      = false;
            buzzed_medium     = false;
            buzzed_long       = false;

            millis_pressed = millis() - millis_button_start;

            // Serial.print("Button pressed for: ");
            // Serial.println(millis_pressed);

            if(millis_pressed < short_press_time) {
                Serial.println("Button pressed for less than 1 second");

                // Interrupts all running beeps
                config::alarm_mute = true;
                grill::buzzer.beep(1, 100);

                display.switch_page();
            }
            else if (millis_pressed < medium_press_time) {
                Serial.println("Button pressed 1-3 seconds");
                display.show_settings_page();
            }
            else if (millis_pressed < long_press_time) {
                Serial.println("Button pressed 3-10 seconds");
                record_off_reason("button");
                power.shutdown();
            }
            else if (millis_pressed > long_press_time) {
                Serial.println("Button pressed for more than 10 seconds");
                config::config_helper.factory_reset();
            }
        }

        delay(100);
    }
}

// ***********************************
// * Probes
// ***********************************

void task_probes(void* pvParameters) {
    Serial.println("Launching task :: PROBES");
    delay(5);   //Give FreeRtos a chance to properly schedule the task

    pinMode(gpio::mux_selector_a, OUTPUT);
    pinMode(gpio::mux_selector_b, OUTPUT);
    pinMode(gpio::mux_selector_c, OUTPUT);

    // Phase 3: history sampling — one sample every 10 s (HISTORY_INTERVAL_S). Timed off
    // millis() instead of a cycle counter so it stays correct when power-saving changes
    // the probe poll period at runtime.
    unsigned long last_history_ms = 0;
    const unsigned long HISTORY_INTERVAL_MS = 10000;

    // Coarse history for long cooks (Pulled Pork etc.): sampled in lockstep across
    // all probes at an adaptive interval (starts at 60 s, doubles as the buffers fill
    // — see Probe::push_coarse). Because every probe is pushed together they stay in
    // lockstep and share a single interval (read from probe_1 for the timer/API).
    unsigned long last_coarse_ms = 0;

    for (;;) {
        // Read probes and also check if beeps/alarms/.. are needed
        grill::probe_1.calculate_temperature(); grill::probe_1.check_temperature_status();
        grill::probe_2.calculate_temperature(); grill::probe_2.check_temperature_status();
        grill::probe_3.calculate_temperature(); grill::probe_3.check_temperature_status();
        grill::probe_4.calculate_temperature(); grill::probe_4.check_temperature_status();
        grill::probe_5.calculate_temperature(); grill::probe_5.check_temperature_status();
        grill::probe_6.calculate_temperature(); grill::probe_6.check_temperature_status();
        grill::probe_7.calculate_temperature(); grill::probe_7.check_temperature_status();
        grill::probe_8.calculate_temperature(); grill::probe_8.check_temperature_status();

        // Phase 3: update global alarm flag
        grill::alarm_active = grill::probe_1.alarm || grill::probe_2.alarm ||
                              grill::probe_3.alarm || grill::probe_4.alarm ||
                              grill::probe_5.alarm || grill::probe_6.alarm ||
                              grill::probe_7.alarm || grill::probe_8.alarm;

        // Phase 3: push temperature history every 10 s
        if (millis() - last_history_ms >= HISTORY_INTERVAL_MS) {
            last_history_ms = millis();
            if (grill::probe_1.connected) grill::probe_1.push_history(grill::probe_1.celcius);
            if (grill::probe_2.connected) grill::probe_2.push_history(grill::probe_2.celcius);
            if (grill::probe_3.connected) grill::probe_3.push_history(grill::probe_3.celcius);
            if (grill::probe_4.connected) grill::probe_4.push_history(grill::probe_4.celcius);
            if (grill::probe_5.connected) grill::probe_5.push_history(grill::probe_5.celcius);
            if (grill::probe_6.connected) grill::probe_6.push_history(grill::probe_6.celcius);
            if (grill::probe_7.connected) grill::probe_7.push_history(grill::probe_7.celcius);
            if (grill::probe_8.connected) grill::probe_8.push_history(grill::probe_8.celcius);
        }

        // Coarse history — push all probes together at the current adaptive interval so
        // they stay in lockstep (one shared interval). No extra ADC work: the temperature
        // was already measured above; this only writes the buffer, and ever less often.
        unsigned long coarse_interval_ms = (unsigned long)grill::probe_1.coarse_interval() * 1000UL;
        if (millis() - last_coarse_ms >= coarse_interval_ms) {
            last_coarse_ms = millis();
            grill::probe_1.push_coarse(grill::probe_1.celcius);
            grill::probe_2.push_coarse(grill::probe_2.celcius);
            grill::probe_3.push_coarse(grill::probe_3.celcius);
            grill::probe_4.push_coarse(grill::probe_4.celcius);
            grill::probe_5.push_coarse(grill::probe_5.celcius);
            grill::probe_6.push_coarse(grill::probe_6.celcius);
            grill::probe_7.push_coarse(grill::probe_7.celcius);
            grill::probe_8.push_coarse(grill::probe_8.celcius);
        }

        // Slower polling in power-saving mode — grill temperatures change slowly, so this
        // halves ADC/MUX/SPI activity and CPU duty without hurting usability.
        delay(config::power_saving ? 1000 : 500);
    }
}

// ***********************************
// * Battery
// ***********************************

void task_battery(void* pvParameters) {
    Serial.println("Launching task :: BATTERY");
    delay(5);   //Give FreeRtos a chance to properly schedule the task

    // Only enforce the protective cutoff when the fuel gauge is confirmed present. If the
    // gauge can't be reached, fail OPEN (keep running) rather than risk shutting down on a
    // bad reading — the whole point is that the device should keep running.
    bool gauge_present = battery.init();
    power.startup();

    // Phase 1: Enable Dynamic Frequency Scaling after power rails are stable.
    // CPU scales 80–240 MHz.  light_sleep_enable=false keeps SPI/I2C alive.
    power.enable_power_management();

    int low_battery_confirmations = 0;

    for (;;) {
        battery.read_battery();

        // Protective low-battery cutoff. The device is meant to run until powered off by the
        // button; it only shuts itself down here to protect the cell from deep discharge.
        // Guards against false triggers:
        //   - only when the gauge is present and the device is NOT charging,
        //   - SoC must be low but plausible (> 0): a raw 0 usually means a failed I2C read,
        //   - a voltage backstop catches a miscalibrated gauge,
        //   - require several consecutive confirmations (~15 s) so a single glitch is ignored.
        if (gauge_present && !grill::battery_charging) {
            bool soc_low = (grill::battery_percentage > 0 &&
                            grill::battery_percentage <= config::battery_cutoff_percent);
            bool voltage_low = (grill::battery_millivolts > 0 &&
                                grill::battery_millivolts <= config::battery_cutoff_millivolts);

            if (soc_low || voltage_low) {
                low_battery_confirmations++;
            } else {
                low_battery_confirmations = 0;
            }

            if (low_battery_confirmations >= 3) {
                Serial.printf("Battery critically low (%d%%, %d mV) -> protective shutdown\n",
                              grill::battery_percentage, grill::battery_millivolts);
                record_off_reason("low_battery");
                power.shutdown();
            }
        }

        // SoC and charge flag change slowly; polling every 5 s is plenty and avoids
        // an I2C transaction + CPU wakeup every second.
        delay(5000);
    }
}

// ***********************************
// * Screen
// ***********************************

void task_screen(void* pvParameters) {
    Serial.println("Launching task :: SCREEN");
    delay(5);   //Give FreeRtos a chance to properly schedule the task

    // Ensure LEDC PWM is configured before display.init() calls setScreenBrightness().
    // task_battery also calls power.init() via power.startup(), but both tasks start
    // concurrently — calling init() here eliminates the race without side effects.
    power.init();
    display.init();

    for (;;) {
        display.display_update();
        // Serial.println("screen update");

        delay(1000);
    }
}

// ***********************************
// * Stack monitor
// ***********************************

void task_stackmonitor(void* pvParameters) {
    Serial.println("Launching task :: STACK MONITOR");
    delay(5);   //Give FreeRtos a chance to properly schedule the task

    float stack_free = 0;
    float stack_used = 0;

    for (;;) {
         // The high water mark is the maximum value of stack that is still free
        // https://www.freertos.org/Why-FreeRTOS/FAQs/Memory-usage-boot-times-context#how-big-should-the-stack-be

        Serial.println("|++++++++++++++ STACK +++++++++++++++|");


        stack_free = (float)uxTaskGetStackHighWaterMark(task::alarmTask);
        stack_used = task::alarmStackSize - stack_free;
        Serial.print("ALARM stack used: ");
        Serial.print(stack_used);
        Serial.print("/");
        Serial.println(task::alarmStackSize);

        stack_free = (float)uxTaskGetStackHighWaterMark(task::batteryTask);
        stack_used = task::batteryStackSize - stack_free;
        Serial.print("BATTERY stack used: ");
        Serial.print(stack_used);
        Serial.print("/");
        Serial.println(task::batteryStackSize);

        stack_free = (float)uxTaskGetStackHighWaterMark(task::mqttTask);
        stack_used = task::mqttStackSize - stack_free;
        Serial.print("MQTT stack used: ");
        Serial.print(stack_used);
        Serial.print("/");
        Serial.println(task::mqttStackSize);

        stack_free = (float)uxTaskGetStackHighWaterMark(task::powerbuttonTask);
        stack_used = task::powerbuttonStackSize - stack_free;
        Serial.print("POWERBUTTON stack used: ");
        Serial.print(stack_used);
        Serial.print("/");
        Serial.println(task::powerbuttonStackSize);

        stack_free = (float)uxTaskGetStackHighWaterMark(task::probesTask);
        stack_used = task::probesStackSize - stack_free;
        Serial.print("PROBES stack used: ");
        Serial.print(stack_used);
        Serial.print("/");
        Serial.println(task::probesStackSize);

        stack_free = (float)uxTaskGetStackHighWaterMark(task::screenTask);
        stack_used = task::screenStackSize - stack_free;
        Serial.print("SCREEN stack used: ");
        Serial.print(stack_used);
        Serial.print("/");
        Serial.println(task::screenStackSize);

        stack_free = (float)uxTaskGetStackHighWaterMark(task::webserverTask);
        stack_used = task::webserverStackSize - stack_free;
        Serial.print("WEBSERVER stack used: ");
        Serial.print(stack_used);
        Serial.print("/");
        Serial.println(task::webserverStackSize);

        stack_free = (float)uxTaskGetStackHighWaterMark(task::stackmonitorTask);
        stack_used = task::stackmonitorStackSize - stack_free;
        Serial.print("STACKMONITOR stack used: ");
        Serial.print(stack_used);
        Serial.print("/");
        Serial.println(task::stackmonitorStackSize);

        delay(5000);
    }
}



void loop() {
    // To make sure that the loop idle does not block freeRTOS
    delay(10000);
}
