#include <string>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

#include "Probe.h"
#include "Buzzer.h"
#include "Mqtt.h"
#include "Opengrill.h"
#include "GrillConfig.h"

namespace gpio{

    int power_button           = 35;

    // enable/disable power
    int power_probes           = 27;
    int power_screen_backlight =  4;
    int power_adc_circuit      = 13;

    int buzzer                 = 32;

    int battery_i2c_sda        = 21;
    int battery_i2c_scl        = 22;

    int hspi_probes_sclk 	   = 14;
    int hspi_probes_cs 	       = 15;
    int hspi_probes_miso 	   = 12;

    int mux_selector_a         = 33;
    int mux_selector_b         = 25;
    int mux_selector_c         = 26;

    int pwm_screen_channel     = 1;
    int pwm_screen_frequency   = 5000;
    int pwm_screen_resolution  = 8;

    int pwm_buzzer_channel     = 2;
    int pwm_buzzer_frequency   = 100;
    int pwm_buzzer_resolution  = 8;
}

namespace config{

    // ***********************************
    // * Settings - nvram storage
    // ***********************************

    Preferences settings_storage;
    GrillConfig config_helper = GrillConfig();

    // ***********************************
    // * Button
    // ***********************************

    int press_seconds_startup       = 2;
    int press_seconds_shutdown      = 2;
    int press_seconds_factory_reset = 10;

    // ***********************************
    // * Grill
    // ***********************************

    String grill_name                   = "";
    String grill_uuid                   = "";
    String grill_firmware_version       = "26.07.01-1";

    String temperature_unit             = "celcius";
    bool beep_enabled                   = true;
    bool beep_on_ready                  = true;
    bool beep_outside_target            = true;
    int  beep_volume                    = 5;
    int  beep_degrees_before            = 5;

    // ***********************************
    // * Buffers
    // ***********************************

    int json_buffer_size                = 3000;
    int mqtt_buffer_size                = 3000;
    int opengrill_buffer_size           = 3000;

    // ***********************************
    // * Mqtt
    // ***********************************

    WiFiClient WifiClient;
    Mqtt mqtt_client                    = Mqtt(WifiClient);

    String mqtt_broker                  = "";
    int    mqtt_port                    = 1883;
    String mqtt_topic                   = "free-grilly";
    String mqtt_user                    = "";
    String mqtt_password                = "";

    // ***********************************
    // * Opengrill
    // ***********************************
    WiFiClient OpengrillWifiClient;
    Opengrill opengrill_client          = Opengrill(OpengrillWifiClient);

    String opengrill_topic              = "opengrill/v1";
    String opengrill_server             = "";
    int    opengrill_port               = 1883;
    String opengrill_user               = "";
    String opengrill_password           = "";

    // ***********************************
    // * Json Utilities
    // ***********************************
    JsonUtilities json_handler;

    // ***********************************
    // * Timezone / NTP
    // ***********************************

    String timezone                     = "Europe/Brussels";
    String ntp_server_1                 = "ptbtime1.ptb.de";
    String ntp_server_2                 = "time-a-wwv.nist.gov";
    String ntp_server_3                 = "ntp.nict.jp";

    // ***********************************
    // * Wifi
    // * default declared in grillconfig.cpp
    // ***********************************

    String wifi_ssid                    = "";
    String wifi_password                = "";

    String wifi_ip                      = "";
    String wifi_subnet                  = "";
    String wifi_gateway                 = "";
    String wifi_dns                     = "";

    // Local AP
    String local_ap_ssid_prefix         = "FreeGrilly";
    String local_ap_ssid                = "";
    String local_ap_password            = "";

    String local_ap_ip                  = "";
    String local_ap_subnet              = "";
    String local_ap_gateway             = "";

    // ***********************************
    // * Probes
    // ***********************************
    int hspi_probes_clockspeed 	        =  16000000;

    // ***********************************
    // * Buzzer
    // ***********************************

    bool alarm_mute                     = false;
    int alarm_beep_amount               = 20;
    int alarm_beep_duration_ms          = 800;
    bool cucaracha_enabled              = false;

    // ***********************************
    // * Screen
    // ***********************************

    int screen_timeout_minutes          = 0;
    int backlight_timeout_minutes       = 0; // 0 = never timeout (original default; user can change in settings)
    int backlight_brightness            = 5;

    // ***********************************
    // * Power
    // ***********************************

    // Default OFF = "always reachable". Reliable connectivity out of the box (full TX
    // power, radio awake, SoftAP up) matters more than battery by default — an aggressive
    // default made the app/web unreliable. Users who want battery mode enable it in the
    // app (Settings → power saving). See lib/Globals/Config.h.
    bool power_saving                   = false;

    // Protective low-battery cutoff. The device otherwise runs until powered off by the
    // button; it only shuts itself down here to protect the cell from deep discharge.
    // A cutoff fires when NOT charging and either the fuel-gauge SoC drops to/below
    // battery_cutoff_percent (a plausible, non-zero reading) OR the measured cell voltage
    // drops to/below battery_cutoff_millivolts (hard backstop in case the gauge is
    // miscalibrated). Several consecutive confirmations are required (see task_battery).
    int battery_cutoff_percent          = 5;    // % state-of-charge
    int battery_cutoff_millivolts       = 3200; // mV per cell

    // ***********************************
    // * Diagnostics (populated at runtime)
    // ***********************************

    // Why the device last powered off (persisted to NVS across a shutdown) and why it last
    // reset (esp_reset_reason at boot). Surfaced via the status API to diagnose unexpected
    // self-power-offs (e.g. "brownout"/"panic" vs. a deliberate "button"/"low_battery").
    String last_off_reason              = "";
    String last_reset_reason            = "";

    // ***********************************
    // * mDNS
    // ***********************************

    String mdns_hostname                = ""; // Set at runtime: free-grilly-<short-uuid>
}

namespace grill{

    // ***********************************
    // * Wifi
    // ***********************************

    bool wifi_connected                 = false;
    int wifi_signal                     = -99;
    bool internet_connectivity          = false;

    // ***********************************
    // * Battery
    // ***********************************

    int battery_percentage              = 0;
    bool battery_charging               = false;
    int battery_millivolts              = 0; // measured cell voltage (mV), for diagnostics

    // ***********************************
    // * Alarm (global summary flag)
    // ***********************************

    bool alarm_active                   = false; // true when any probe is in alarm state

    // ***********************************
    // * Buzzer
    // ***********************************

    Buzzer buzzer;

    // ***********************************
    // * Probes
    // ***********************************

    Probe probe_1 = Probe(1);
    Probe probe_2 = Probe(2);
    Probe probe_3 = Probe(3);
    Probe probe_4 = Probe(4);
    Probe probe_5 = Probe(5);
    Probe probe_6 = Probe(6);
    Probe probe_7 = Probe(7);
    Probe probe_8 = Probe(8);
}

namespace web{
    // ***********************************
    // * Api / Webserver
    // ***********************************

    WebServer webserver(80);
}

namespace task{
    TaskHandle_t alarmTask;
    TaskHandle_t batteryTask;
    TaskHandle_t mqttTask;
    TaskHandle_t opengrillTask;
    TaskHandle_t powerbuttonTask;
    TaskHandle_t probesTask;
    TaskHandle_t screenTask;
    TaskHandle_t webserverTask;
    TaskHandle_t stackmonitorTask;

    int alarmStackSize        = 1000;
    int batteryStackSize      = 2000;
    int mqttStackSize         = 8000;
    int opengrillStackSize    = 8000;
    int powerbuttonStackSize  = 10000; //Needed to be able to handle factory reset
    int probesStackSize       = 1000;
    int screenStackSize       = 3000;
    int webserverStackSize    = 8000;
    int stackmonitorStackSize = 4000;
}
