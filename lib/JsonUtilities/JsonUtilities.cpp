#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>

#include "Config.h"
#include "Grill.h"
#include "GrillConfig.h"
#include "JsonUtilities.h"
#include "Probe.h"

JsonDocument jsondoc;

void JsonUtilities::load_json_status(char *buffer){
    jsondoc.clear();

    jsondoc["name"]               = config::grill_name;
    jsondoc["unique_id"]          = config::grill_uuid;
    jsondoc["uuid"]               = config::grill_uuid;         // Android app compat alias
    jsondoc["firmware_version"]   = config::grill_firmware_version;
    jsondoc["battery_percentage"] = grill::battery_percentage;
    jsondoc["battery_charging"]   = grill::battery_charging;
    jsondoc["wifi_connected"]     = grill::wifi_connected;
    jsondoc["wifi_ssid"]          = config::wifi_ssid;
    jsondoc["wifi_ip"]            = config::wifi_ip;
    jsondoc["wifi_signal"]        = WiFi.RSSI();
    jsondoc["temperature_unit"]   = config::temperature_unit;
    jsondoc["alarm_active"]       = grill::alarm_active;
    jsondoc["mdns_hostname"]      = config::mdns_hostname;

    // Helper lambda to fill one probe object (includes alarm + eta_seconds)
    auto fill_probe = [](JsonObject obj, int id, Probe& p){
        obj["probe_id"]            = id;
        obj["id"]                  = id;     // Android app compat alias
        obj["name"]                = p.name;
        obj["temperature"]         = p.temperature;
        obj["minimum_temperature"] = p.minimum_temperature;
        obj["target_temperature"]  = p.target_temperature;
        obj["connected"]           = p.connected;
        obj["alarm"]               = p.alarm;
        obj["eta_seconds"]         = p.seconds_to_target(); // -1 = unknown
    };

    JsonArray probeData = jsondoc["probes"].to<JsonArray>();
    fill_probe(probeData.add<JsonObject>(), 1, grill::probe_1);
    fill_probe(probeData.add<JsonObject>(), 2, grill::probe_2);
    fill_probe(probeData.add<JsonObject>(), 3, grill::probe_3);
    fill_probe(probeData.add<JsonObject>(), 4, grill::probe_4);
    fill_probe(probeData.add<JsonObject>(), 5, grill::probe_5);
    fill_probe(probeData.add<JsonObject>(), 6, grill::probe_6);
    fill_probe(probeData.add<JsonObject>(), 7, grill::probe_7);
    fill_probe(probeData.add<JsonObject>(), 8, grill::probe_8);

    jsondoc.shrinkToFit();
    serializeJson(jsondoc, buffer, config::json_buffer_size);
}

void JsonUtilities::load_json_settings(char* buffer){

    jsondoc.clear();

    jsondoc["name"]                      = config::grill_name;
    jsondoc["grill_name"]                = config::grill_name;  // Android app compat alias
    jsondoc["uuid"]                      = config::grill_uuid;
    jsondoc["firmware_version"]          = config::grill_firmware_version;

    jsondoc["temperature_unit"]          = config::temperature_unit;
    jsondoc["beep_enabled"]              = config::beep_enabled;
    jsondoc["beep_volume"]               = config::beep_volume;
    jsondoc["beep_degrees_before"]       = config::beep_degrees_before;
    jsondoc["beep_outside_target"]       = config::beep_outside_target;
    jsondoc["beep_on_ready"]             = config::beep_on_ready;
    jsondoc["cucaracha_enabled"]         = config::cucaracha_enabled;

    jsondoc["screen_timeout_minutes"]    = config::screen_timeout_minutes;
    jsondoc["backlight_timeout_minutes"] = config::backlight_timeout_minutes;
    jsondoc["backlight_brightness"]      = config::backlight_brightness;
    jsondoc["power_saving"]              = config::power_saving;

    jsondoc["opengrill_server"]          = config::opengrill_server;

    jsondoc["mqtt_broker"]               = config::mqtt_broker;
    jsondoc["mqtt_port"]                 = config::mqtt_port;
    jsondoc["mqtt_topic"]                = config::mqtt_topic;
    jsondoc["mqtt_user"]                 = config::mqtt_user;
    jsondoc["mqtt_password"]             = config::mqtt_password;

    jsondoc["wifi_ssid"]                 = config::wifi_ssid;
    jsondoc["wifi_ip"]                   = config::wifi_ip;
    jsondoc["wifi_subnet"]               = config::wifi_subnet;
    jsondoc["wifi_gateway"]              = config::wifi_gateway;
    jsondoc["wifi_dns"]                  = config::wifi_dns;
    jsondoc["wifi_password"]             = config::wifi_password;

    jsondoc["local_ap_ssid"]             = config::local_ap_ssid;
    jsondoc["local_ap_ip"]               = config::local_ap_ip;
    jsondoc["local_ap_subnet"]           = config::local_ap_subnet;
    jsondoc["local_ap_gateway"]          = config::local_ap_gateway;
    jsondoc["local_ap_password"]         = config::local_ap_password;

    jsondoc.shrinkToFit();

    serializeJson(jsondoc, buffer, config::json_buffer_size);
}

jsonResult JsonUtilities::save_json_settings(char* raw_json){
    DeserializationError err = deserializeJson(jsondoc, raw_json);

    if(err){ return {false, "Could not deserialize json"}; }

    JsonObject json_data = jsondoc.as<JsonObject>();

    if(json_data["local_ap_password"].as<String>().length() > 0 && json_data["local_ap_password"].as<String>().length() < 8){
        return {false, "local_ap_password should be empty or at least 8 characters"};
    }

    if(json_data["screen_timeout_minutes"].as<int>() < 0){
        return {false, "screen_timeout_minutes should be > 0"};
    }

    if(json_data["backlight_timeout_minutes"].as<int>() < 0){
        return {false, "backlight_timeout_minutes should be > 0"};
    }

    // Merge semantics: only overwrite a config field when its key is actually present
    // in the incoming JSON. The Android app sends a *partial* settings object (only the
    // handful of fields it wants to change); previously every absent key was deserialized
    // to "" / 0 and clobbered the stored value — wiping local_ap_*, wifi_* IP config,
    // mqtt_*, brightness and timeouts. These helpers preserve unspecified fields.
    auto set_str  = [&](const char* key, String& dst){ if (!json_data[key].isNull()) dst = json_data[key].as<String>(); };
    auto set_int  = [&](const char* key, int& dst)   { if (!json_data[key].isNull()) dst = json_data[key].as<int>();    };
    auto set_bool = [&](const char* key, bool& dst)  { if (!json_data[key].isNull()) dst = json_data[key].as<bool>();   };

    // Data ingress — accept both "grill_name" (Android app) and "name" (web UI / legacy).
    // Leave the name untouched when neither key carries a non-empty value.
    if (json_data["grill_name"].is<const char*>() && json_data["grill_name"].as<String>().length() > 0) {
        config::grill_name            = json_data["grill_name"].as<String>();
    } else if (json_data["name"].is<const char*>() && json_data["name"].as<String>().length() > 0) {
        config::grill_name            = json_data["name"].as<String>();
    }

    set_str("temperature_unit",   config::temperature_unit);
    set_bool("beep_enabled",      config::beep_enabled);
    set_int("beep_volume",        config::beep_volume);
    set_int("beep_degrees_before", config::beep_degrees_before);
    set_bool("beep_outside_target", config::beep_outside_target);
    set_bool("beep_on_ready",     config::beep_on_ready);
    set_bool("cucaracha_enabled", config::cucaracha_enabled);

    set_int("screen_timeout_minutes",    config::screen_timeout_minutes);
    set_int("backlight_timeout_minutes", config::backlight_timeout_minutes);
    set_int("backlight_brightness",      config::backlight_brightness);
    set_bool("power_saving",             config::power_saving);

    set_str("opengrill_server", config::opengrill_server);

    set_str("mqtt_broker",   config::mqtt_broker);
    set_int("mqtt_port",     config::mqtt_port);
    set_str("mqtt_topic",    config::mqtt_topic);
    set_str("mqtt_user",     config::mqtt_user);
    set_str("mqtt_password", config::mqtt_password);

    set_str("wifi_ssid",     config::wifi_ssid);
    set_str("wifi_password", config::wifi_password);
    set_str("wifi_ip",       config::wifi_ip);
    set_str("wifi_subnet",   config::wifi_subnet);
    set_str("wifi_gateway",  config::wifi_gateway);
    set_str("wifi_dns",      config::wifi_dns);

    set_str("local_ap_ssid",     config::local_ap_ssid);
    set_str("local_ap_password", config::local_ap_password);
    set_str("local_ap_ip",       config::local_ap_ip);
    set_str("local_ap_subnet",   config::local_ap_subnet);
    set_str("local_ap_gateway",  config::local_ap_gateway);

    // Set default value for empty topics
    if(config::mqtt_topic.length() == 0){
        config::mqtt_topic = "free-grilly";
    }

    config::config_helper.save_settings();
    return {true, "Ok"};
}

void JsonUtilities::load_json_probes(char* buffer){
    jsondoc.clear();

    // Note: each probe object emits both legacy keys (probe_id, probe_type) consumed by the
    // firmware's own web UI AND the Android app's keys (id, type). The app uses ignoreUnknownKeys
    // so the legacy keys are silently skipped; the web UI only reads the legacy keys.
    JsonObject doc_0 = jsondoc.add<JsonObject>();
    doc_0["probe_id"] = 1; doc_0["id"] = 1;
    doc_0["temperature"] = grill::probe_1.temperature;
    doc_0["name"] = grill::probe_1.name;
    doc_0["minimum_temperature"] = grill::probe_1.minimum_temperature;
    doc_0["target_temperature"] = grill::probe_1.target_temperature;
    doc_0["connected"] = grill::probe_1.connected;
    doc_0["probe_type"] = grill::probe_1.type; doc_0["type"] = grill::probe_1.type;
    doc_0["reference_kohm"] = grill::probe_1.reference_kohm;
    doc_0["reference_celcius"] = grill::probe_1.reference_celcius;
    doc_0["reference_beta"] = grill::probe_1.reference_beta;

    JsonObject doc_1 = jsondoc.add<JsonObject>();
    doc_1["probe_id"] = 2; doc_1["id"] = 2;
    doc_1["temperature"] = grill::probe_2.temperature;
    doc_1["name"] = grill::probe_2.name;
    doc_1["minimum_temperature"] = grill::probe_2.minimum_temperature;
    doc_1["target_temperature"] = grill::probe_2.target_temperature;
    doc_1["connected"] = grill::probe_2.connected;
    doc_1["probe_type"] = grill::probe_2.type; doc_1["type"] = grill::probe_2.type;
    doc_1["reference_kohm"] = grill::probe_2.reference_kohm;
    doc_1["reference_celcius"] = grill::probe_2.reference_celcius;
    doc_1["reference_beta"] = grill::probe_2.reference_beta;

    JsonObject doc_2 = jsondoc.add<JsonObject>();
    doc_2["probe_id"] = 3; doc_2["id"] = 3;
    doc_2["temperature"] = grill::probe_3.temperature;
    doc_2["name"] = grill::probe_3.name;
    doc_2["minimum_temperature"] = grill::probe_3.minimum_temperature;
    doc_2["target_temperature"] = grill::probe_3.target_temperature;
    doc_2["connected"] = grill::probe_3.connected;
    doc_2["probe_type"] = grill::probe_3.type; doc_2["type"] = grill::probe_3.type;
    doc_2["reference_kohm"] = grill::probe_3.reference_kohm;
    doc_2["reference_celcius"] = grill::probe_3.reference_celcius;
    doc_2["reference_beta"] = grill::probe_3.reference_beta;

    JsonObject doc_3 = jsondoc.add<JsonObject>();
    doc_3["probe_id"] = 4; doc_3["id"] = 4;
    doc_3["temperature"] = grill::probe_4.temperature;
    doc_3["name"] = grill::probe_4.name;
    doc_3["minimum_temperature"] = grill::probe_4.minimum_temperature;
    doc_3["target_temperature"] = grill::probe_4.target_temperature;
    doc_3["connected"] = grill::probe_4.connected;
    doc_3["probe_type"] = grill::probe_4.type; doc_3["type"] = grill::probe_4.type;
    doc_3["reference_kohm"] = grill::probe_4.reference_kohm;
    doc_3["reference_celcius"] = grill::probe_4.reference_celcius;
    doc_3["reference_beta"] = grill::probe_4.reference_beta;

    JsonObject doc_4 = jsondoc.add<JsonObject>();
    doc_4["probe_id"] = 5; doc_4["id"] = 5;
    doc_4["temperature"] = grill::probe_5.temperature;
    doc_4["name"] = grill::probe_5.name;
    doc_4["minimum_temperature"] = grill::probe_5.minimum_temperature;
    doc_4["target_temperature"] = grill::probe_5.target_temperature;
    doc_4["connected"] = grill::probe_5.connected;
    doc_4["probe_type"] = grill::probe_5.type; doc_4["type"] = grill::probe_5.type;
    doc_4["reference_kohm"] = grill::probe_5.reference_kohm;
    doc_4["reference_celcius"] = grill::probe_5.reference_celcius;
    doc_4["reference_beta"] = grill::probe_5.reference_beta;

    JsonObject doc_5 = jsondoc.add<JsonObject>();
    doc_5["probe_id"] = 6; doc_5["id"] = 6;
    doc_5["temperature"] = grill::probe_6.temperature;
    doc_5["name"] = grill::probe_6.name;
    doc_5["minimum_temperature"] = grill::probe_6.minimum_temperature;
    doc_5["target_temperature"] = grill::probe_6.target_temperature;
    doc_5["connected"] = grill::probe_6.connected;
    doc_5["probe_type"] = grill::probe_6.type; doc_5["type"] = grill::probe_6.type;
    doc_5["reference_kohm"] = grill::probe_6.reference_kohm;
    doc_5["reference_celcius"] = grill::probe_6.reference_celcius;
    doc_5["reference_beta"] = grill::probe_6.reference_beta;

    JsonObject doc_6 = jsondoc.add<JsonObject>();
    doc_6["probe_id"] = 7; doc_6["id"] = 7;
    doc_6["temperature"] = grill::probe_7.temperature;
    doc_6["name"] = grill::probe_7.name;
    doc_6["minimum_temperature"] = grill::probe_7.minimum_temperature;
    doc_6["target_temperature"] = grill::probe_7.target_temperature;
    doc_6["connected"] = grill::probe_7.connected;
    doc_6["probe_type"] = grill::probe_7.type; doc_6["type"] = grill::probe_7.type;
    doc_6["reference_kohm"] = grill::probe_7.reference_kohm;
    doc_6["reference_celcius"] = grill::probe_7.reference_celcius;
    doc_6["reference_beta"] = grill::probe_7.reference_beta;

    JsonObject doc_7 = jsondoc.add<JsonObject>();
    doc_7["probe_id"] = 8; doc_7["id"] = 8;
    doc_7["temperature"] = grill::probe_8.temperature;
    doc_7["name"] = grill::probe_8.name;
    doc_7["minimum_temperature"] = grill::probe_8.minimum_temperature;
    doc_7["target_temperature"] = grill::probe_8.target_temperature;
    doc_7["connected"] = grill::probe_8.connected;
    doc_7["probe_type"] = grill::probe_8.type; doc_7["type"] = grill::probe_8.type;
    doc_7["reference_kohm"] = grill::probe_8.reference_kohm;
    doc_7["reference_celcius"] = grill::probe_8.reference_celcius;
    doc_7["reference_beta"] = grill::probe_8.reference_beta;

    jsondoc.shrinkToFit();
    serializeJson(jsondoc, buffer, config::json_buffer_size);
}

jsonResult JsonUtilities::save_json_probes(char* raw_json){

    DeserializationError err = deserializeJson(jsondoc, raw_json);
    if(err){ return {false, "Could not deserialize json"}; }

    for (JsonObject item : jsondoc.as<JsonArray>()) {

        // Accept both "id" (Android app) and "probe_id" (legacy web UI)
        int    probe_id            = item["id"] | item["probe_id"].as<int>();
        String name                = item["name"];
        float  minimum_temperature = item["minimum_temperature"];
        float  target_temperature  = item["target_temperature"];
        // Accept both "type" (Android app) and "probe_type" (legacy web UI)
        String probe_type          = item["type"].is<const char*>()
                                         ? item["type"].as<String>()
                                         : item["probe_type"].as<String>();
        int    reference_kohm      = item["reference_kohm"];
        int    reference_celcius   = item["reference_celcius"];
        int    reference_beta      = item["reference_beta"];

        switch (probe_id){
            case 1:
                grill::probe_1.set_temperature(target_temperature, minimum_temperature);
                grill::probe_1.set_type(probe_type, reference_kohm, reference_celcius, reference_beta);
                grill::probe_1.set_name(name);
                break;
            case 2:
                grill::probe_2.set_temperature(target_temperature, minimum_temperature);
                grill::probe_2.set_type(probe_type, reference_kohm, reference_celcius, reference_beta);
                grill::probe_2.set_name(name);
                break;
            case 3:
                grill::probe_3.set_temperature(target_temperature, minimum_temperature);
                grill::probe_3.set_type(probe_type, reference_kohm, reference_celcius, reference_beta);
                grill::probe_3.set_name(name);
                break;
            case 4:
                grill::probe_4.set_temperature(target_temperature, minimum_temperature);
                grill::probe_4.set_type(probe_type, reference_kohm, reference_celcius, reference_beta);
                grill::probe_4.set_name(name);
                break;
            case 5:
                grill::probe_5.set_temperature(target_temperature, minimum_temperature);
                grill::probe_5.set_type(probe_type, reference_kohm, reference_celcius, reference_beta);
                grill::probe_5.set_name(name);
                break;
            case 6:
                grill::probe_6.set_temperature(target_temperature, minimum_temperature);
                grill::probe_6.set_type(probe_type, reference_kohm, reference_celcius, reference_beta);
                grill::probe_6.set_name(name);
                break;
            case 7:
                grill::probe_7.set_temperature(target_temperature, minimum_temperature);
                grill::probe_7.set_type(probe_type, reference_kohm, reference_celcius, reference_beta);
                grill::probe_7.set_name(name);
                break;
            case 8:
                grill::probe_8.set_temperature(target_temperature, minimum_temperature);
                grill::probe_8.set_type(probe_type, reference_kohm, reference_celcius, reference_beta);
                grill::probe_8.set_name(name);
                break;
            default:
                break;
        }
    }

    config::config_helper.save_probes();

    return {true, "Ok"};
}

void JsonUtilities::load_opengrill_grill(char *buffer){
    jsondoc.clear();

    jsondoc["name"]                 = config::grill_name;
    jsondoc["battery_percentage"]   = grill::battery_percentage;
    jsondoc["temperature_unit"]     = config::temperature_unit;
    jsondoc["max_supported_probes"] = 8;

    JsonObject temperatures = jsondoc["temperatures"].to<JsonObject>();
    temperatures["1"] = grill::probe_1.temperature;
    temperatures["2"] = grill::probe_2.temperature;
    temperatures["3"] = grill::probe_3.temperature;
    temperatures["4"] = grill::probe_4.temperature;
    temperatures["5"] = grill::probe_5.temperature;
    temperatures["6"] = grill::probe_6.temperature;
    temperatures["7"] = grill::probe_7.temperature;
    temperatures["8"] = grill::probe_8.temperature;

    jsondoc.shrinkToFit();
    serializeJson(jsondoc, buffer, config::json_buffer_size);
}

jsonResult JsonUtilities::save_opengrill_grill(char* raw_json){
    DeserializationError err = deserializeJson(jsondoc, raw_json);

    if(err){ return {false, "Could not deserialize json"}; }

    JsonObject json_data = jsondoc.as<JsonObject>();

    // Data ingress
    config::grill_name                = json_data["name"].as<String>();

    config::config_helper.save_settings();
    return {true, "Ok"};
}

void JsonUtilities::load_opengrill_probes(char* buffer){
    jsondoc.clear();

    JsonObject p1 = jsondoc["1"].to<JsonObject>();
    p1["name"] = grill::probe_1.name;
    p1["target_temperature"] = grill::probe_1.target_temperature;

    if(grill::probe_1.minimum_temperature != 0.00f){
        p1["minimum_temperature"] = grill::probe_1.minimum_temperature;
    } else {
        p1["minimum_temperature"] = nullptr;
    }

    JsonObject p2 = jsondoc["2"].to<JsonObject>();
    p2["name"] = grill::probe_2.name;
    p2["target_temperature"] = grill::probe_2.target_temperature;

    if(grill::probe_2.minimum_temperature != 0.00f){
        p2["minimum_temperature"] = grill::probe_2.minimum_temperature;
    } else {
        p2["minimum_temperature"] = nullptr;
    }

    JsonObject p3 = jsondoc["3"].to<JsonObject>();
    p3["name"] = grill::probe_3.name;
    p3["target_temperature"] = grill::probe_3.target_temperature;

    if(grill::probe_3.minimum_temperature != 0.00f){
        p3["minimum_temperature"] = grill::probe_3.minimum_temperature;
    } else {
        p3["minimum_temperature"] = nullptr;
    }

    JsonObject p4 = jsondoc["4"].to<JsonObject>();
    p4["name"] = grill::probe_4.name;
    p4["target_temperature"] = grill::probe_4.target_temperature;

    if(grill::probe_4.minimum_temperature != 0.00f){
        p4["minimum_temperature"] = grill::probe_4.minimum_temperature;
    } else {
        p4["minimum_temperature"] = nullptr;
    }

    JsonObject p5 = jsondoc["5"].to<JsonObject>();
    p5["name"] = grill::probe_5.name;
    p5["target_temperature"] = grill::probe_5.target_temperature;

    if(grill::probe_5.minimum_temperature != 0.00f){
        p5["minimum_temperature"] = grill::probe_5.minimum_temperature;
    } else {
        p5["minimum_temperature"] = nullptr;
    }

    JsonObject p6 = jsondoc["6"].to<JsonObject>();
    p6["name"] = grill::probe_6.name;
    p6["target_temperature"] = grill::probe_6.target_temperature;

    if(grill::probe_6.minimum_temperature != 0.00f){
        p6["minimum_temperature"] = grill::probe_6.minimum_temperature;
    } else {
        p6["minimum_temperature"] = nullptr;
    }

    JsonObject p7 = jsondoc["7"].to<JsonObject>();
    p7["name"] = grill::probe_7.name;
    p7["target_temperature"] = grill::probe_7.target_temperature;

    if(grill::probe_7.minimum_temperature != 0.00f){
        p7["minimum_temperature"] = grill::probe_7.minimum_temperature;
    } else {
        p7["minimum_temperature"] = nullptr;
    }

    JsonObject p8 = jsondoc["8"].to<JsonObject>();
    p8["name"] = grill::probe_8.name;
    p8["target_temperature"] = grill::probe_8.target_temperature;

    if(grill::probe_8.minimum_temperature != 0.00f){
        p8["minimum_temperature"] = grill::probe_8.minimum_temperature;
    } else {
        p8["minimum_temperature"] = nullptr;
    }

    jsondoc.shrinkToFit();
    serializeJson(jsondoc, buffer, config::json_buffer_size);
}

jsonResult JsonUtilities::save_opengrill_probes(char* raw_json){

    DeserializationError err = deserializeJson(jsondoc, raw_json);
    if(err){ return {false, "Could not deserialize json"}; }

    for (JsonPair item : jsondoc.as<JsonObject>()) {

        int    probe_id            = atoi(item.key().c_str());

        String name                = item.value()["name"];
        float  minimum_temperature = item.value()["minimum_temperature"];
        float  target_temperature  = item.value()["target_temperature"];

        switch (probe_id){
            case 1:
                grill::probe_1.set_temperature(target_temperature, minimum_temperature);
                grill::probe_1.set_name(name);
                break;
            case 2:
                grill::probe_2.set_temperature(target_temperature, minimum_temperature);
                grill::probe_2.set_name(name);
                break;
            case 3:
                grill::probe_3.set_temperature(target_temperature, minimum_temperature);
                grill::probe_3.set_name(name);
                break;
            case 4:
                grill::probe_4.set_temperature(target_temperature, minimum_temperature);
                grill::probe_4.set_name(name);
                break;
            case 5:
                grill::probe_5.set_temperature(target_temperature, minimum_temperature);
                grill::probe_5.set_name(name);
                break;
            case 6:
                grill::probe_6.set_temperature(target_temperature, minimum_temperature);
                grill::probe_6.set_name(name);
                break;
            case 7:
                grill::probe_7.set_temperature(target_temperature, minimum_temperature);
                grill::probe_7.set_name(name);
                break;
            case 8:
                grill::probe_8.set_temperature(target_temperature, minimum_temperature);
                grill::probe_8.set_name(name);
                break;
            default:
                break;
        }
    }

    config::config_helper.save_probes();

    return {true, "Ok"};
}

void JsonUtilities::load_json_wifiscan(char* buffer){

    Serial.println("Starting WIFI scan");

    int scanned_networks = WiFi.scanNetworks();

    if (scanned_networks == 0) {
        Serial.println("no networks found");
    }

    jsondoc.clear();

    JsonArray networks = jsondoc.to<JsonArray>();

    for (int network_nr = 0; network_nr < scanned_networks; ++network_nr) {

        JsonObject scanned_network = networks.add<JsonObject>();

        scanned_network["ssid"]            = WiFi.SSID(network_nr).c_str();
        scanned_network["signal_strength"] = WiFi.RSSI(network_nr);
        scanned_network["rssi"]            = WiFi.RSSI(network_nr); // Android app compat alias

        const char* auth_str = "unknown";
        switch (WiFi.encryptionType(network_nr)) {
            case WIFI_AUTH_OPEN:            auth_str = "open";            break;
            case WIFI_AUTH_WEP:             auth_str = "wep";             break;
            case WIFI_AUTH_WPA_PSK:         auth_str = "wpa_psk";         break;
            case WIFI_AUTH_WPA2_PSK:        auth_str = "wpa2_psk";        break;
            case WIFI_AUTH_WPA_WPA2_PSK:    auth_str = "wpa_wpa2_psk";    break;
            case WIFI_AUTH_WPA2_ENTERPRISE: auth_str = "wpa2_enterprise";  break;
            case WIFI_AUTH_WPA3_PSK:        auth_str = "wpa3_psk";        break;
            case WIFI_AUTH_WPA2_WPA3_PSK:   auth_str = "wpa2_wpa3_psk";   break;
            case WIFI_AUTH_WAPI_PSK:        auth_str = "wpapi_psk";       break;
            default:                        auth_str = "unknown";         break;
        }
        scanned_network["auth_method"]  = auth_str;
        scanned_network["encryption"]   = auth_str; // Android app compat alias
    }
    // Free memory
    WiFi.scanDelete();

    jsondoc.shrinkToFit();

    serializeJson(jsondoc, buffer, config::json_buffer_size);
}

// ***********************************
// * Probe history (Phase 3)
// * Returns compact array of int16 values (celsius × 10) per probe.
// ***********************************

void JsonUtilities::load_json_history(char* buffer, size_t buf_size) {
    jsondoc.clear();

    jsondoc["interval_seconds"]        = 10; // fine tier — matches Probe::HISTORY_INTERVAL_S
    // Coarse tier interval is adaptive (doubles as the buffers fill). All probes are
    // sampled in lockstep, so probe_1's value represents the whole device.
    jsondoc["coarse_interval_seconds"] = grill::probe_1.coarse_interval();

    JsonArray probes = jsondoc["probes"].to<JsonArray>();

    Probe* probe_list[8] = {
        &grill::probe_1, &grill::probe_2, &grill::probe_3, &grill::probe_4,
        &grill::probe_5, &grill::probe_6, &grill::probe_7, &grill::probe_8
    };

    int16_t tmp[180];
    for (int i = 0; i < 8; i++) {
        Probe& p         = *probe_list[i];
        JsonObject obj   = probes.add<JsonObject>();
        obj["probe_id"]  = i + 1;
        obj["id"]        = i + 1;    // Android app compat alias (required field)
        obj["name"]      = p.name;   // Android app compat (required field)
        obj["connected"] = p.connected;

        JsonArray arr    = obj["history"].to<JsonArray>();        // fine tier (recent detail)
        int count        = p.get_history(tmp, 180);
        for (int j = 0; j < count; j++) arr.add(tmp[j]);

        JsonArray carr   = obj["history_coarse"].to<JsonArray>(); // coarse tier (whole cook)
        int ccount       = p.get_coarse(tmp, 180);
        for (int j = 0; j < ccount; j++) carr.add(tmp[j]);
    }

    jsondoc.shrinkToFit();
    serializeJson(jsondoc, buffer, buf_size);
}
