#include <ArduinoJson.h>
#include <string>
#include <WiFi.h>

#include "Probe.h"
#include "Buzzer.h"
#include "GrillConfig.h"

#include "Api.h"
#include "Config.h"
#include "Grill.h"
#include "JsonUtilities.h"
#include "Web.h"

// Set this to config::json_buffer_size, cant do this dynamically
char api_json_buffer[3000];

// History endpoint worst case: 8 probes × (180 fine + 180 coarse) int16 values plus
// JSON structure ≈ 22 KB. Allocated on the heap per request (and freed) instead of a
// permanently reserved static buffer.
static const size_t HISTORY_BUFFER_SIZE = 24576;

void setup_api_routes()
{
    web::webserver.on("/api/grill",           HTTP_GET,     get_api_grill);

    web::webserver.on("/api/probes",          HTTP_GET,     get_api_probes);
    web::webserver.on("/api/probes",          HTTP_POST,    post_api_probes);
    web::webserver.on("/api/probes",          HTTP_OPTIONS, cors_api_probes);

    web::webserver.on("/api/probes/history",  HTTP_GET,     get_api_probes_history);
    web::webserver.on("/api/probes/history",  HTTP_OPTIONS, cors_api_probes);

    web::webserver.on("/api/settings",        HTTP_GET,     get_api_settings);
    web::webserver.on("/api/settings",        HTTP_POST,    post_api_settings);
    web::webserver.on("/api/settings",        HTTP_OPTIONS, cors_api_settings);

    web::webserver.on("/api/alarm/mute",      HTTP_POST,    post_api_alarm_mute);
    web::webserver.on("/api/alarm/mute",      HTTP_OPTIONS, cors_api_alarm_mute);

    web::webserver.on("/api/info",            HTTP_GET,     get_api_info);

    web::webserver.on("/api/wifiscan",        HTTP_GET,     get_api_wifiscan);
}

void get_api_grill()
{
    config::json_handler.load_json_status(api_json_buffer);
    web::webserver.send(200, "application/json", api_json_buffer);
}

void get_api_probes(){
    config::json_handler.load_json_probes(api_json_buffer);
    web::webserver.send(200, "application/json", api_json_buffer);
}

void post_api_probes()
{
    if(web::webserver.hasArg("plain") == false) { web::webserver.send(400, "application/json", "{\"error\": \"empty body\"}"); return;}

    web::webserver.arg("plain").toCharArray(api_json_buffer, config::json_buffer_size);
    jsonResult result = config::json_handler.save_json_probes(api_json_buffer);

    if(!result.success){
        web::webserver.send(400, "application/json", "{\"error\": \"" + result.message + "\"}");
        return;
    }

    get_api_probes(); //Return current data if ok
}

void cors_api_probes(){
    web::webserver.send(200, "application/json", "");
    return;
}

void get_api_probes_history(){
    char* buffer = (char*) malloc(HISTORY_BUFFER_SIZE);
    if (buffer == nullptr) {
        web::webserver.send(503, "application/json", "{\"error\":\"out of memory\"}");
        return;
    }
    config::json_handler.load_json_history(buffer, HISTORY_BUFFER_SIZE);
    web::webserver.send(200, "application/json", buffer);
    free(buffer);
}

void get_api_settings(){
    config::json_handler.load_json_settings(api_json_buffer);
    web::webserver.send(200, "application/json", api_json_buffer);
}

void post_api_settings(){
    if(web::webserver.hasArg("plain") == false) { web::webserver.send(400, "application/json", "{\"error\": \"empty body\"}"); return;}

    web::webserver.arg("plain").toCharArray(api_json_buffer, config::json_buffer_size);
    jsonResult result = config::json_handler.save_json_settings(api_json_buffer);

    if(!result.success){
        web::webserver.send(400, "application/json", "{\"error\": \"" + result.message + "\"}");
        return;
    }

    get_api_settings(); //Return current data if ok
}

void cors_api_settings(){
    web::webserver.send(200, "application/json", "");
    return;
}

void post_api_alarm_mute(){
    config::alarm_mute = true;
    web::webserver.send(200, "application/json", "{\"success\": true}");
}

void cors_api_alarm_mute(){
    web::webserver.send(200, "application/json", "");
    return;
}

void get_api_info(){
    JsonDocument doc;
    doc["uuid"]             = config::grill_uuid;
    doc["name"]             = config::grill_name;
    doc["firmware_version"] = config::grill_firmware_version;
    doc["firmware"]         = config::grill_firmware_version; // Android app compat alias
    doc["mdns_hostname"]    = config::mdns_hostname;

    JsonArray caps = doc["capabilities"].to<JsonArray>();
    caps.add("probes");
    caps.add("mqtt");
    caps.add("opengrill");
    caps.add("history");
    caps.add("mdns");
    caps.add("alarm_mute");
    caps.add("eta");

    doc.shrinkToFit();
    serializeJson(doc, api_json_buffer, sizeof(api_json_buffer));
    web::webserver.send(200, "application/json", api_json_buffer);
}

void get_api_wifiscan(){
    config::json_handler.load_json_wifiscan(api_json_buffer);
    web::webserver.send(200, "application/json", api_json_buffer);
    return;
}
