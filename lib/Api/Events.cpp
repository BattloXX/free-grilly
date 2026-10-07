#include <Arduino.h>
#include <WiFi.h>
#include <cstring>

#include "Config.h"
#include "JsonUtilities.h"
#include "Web.h"

#include "Api.h"

namespace {
    WiFiClient events_client;
    unsigned long events_last_sent = 0;

    // "data: " + the GET /api/grill json (at most config::json_buffer_size) + "\n\n"
    char events_frame[6 + 3000 + 2];

    bool send_events_frame(){
        memcpy(events_frame, "data: ", 6);
        config::json_handler.load_json_status(events_frame + 6);
        size_t json_length = strlen(events_frame + 6);
        memcpy(events_frame + 6 + json_length, "\n\n", 2);

        size_t frame_length = 6 + json_length + 2;
        return events_client.write(reinterpret_cast<const uint8_t*>(events_frame), frame_length) == frame_length;
    }

    void stop_events_client(){
        events_client.stop();
        events_last_sent = 0;
    }
}

void get_api_events(){
    // One client keeps RAM bounded; this server handles requests on one task.
    stop_events_client();
    events_client = web::webserver.client();
    events_client.setTimeout(1); // WiFiClient uses seconds in this framework version.

    static const char headers[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";
    static const char retry[] = "retry: 3000\n\n";

    if(events_client.write(reinterpret_cast<const uint8_t*>(headers), sizeof(headers) - 1) != sizeof(headers) - 1
       || events_client.write(reinterpret_cast<const uint8_t*>(retry), sizeof(retry) - 1) != sizeof(retry) - 1
       || !send_events_frame()){
        stop_events_client();
        return;
    }
    events_last_sent = millis();
}

void loop_api_events(){
    if(!events_client.connected()){
        stop_events_client();
        return;
    }

    // Status changes every second, and the frame also keeps the connection alive.
    if((unsigned long)(millis() - events_last_sent) >= 1000){
        if(send_events_frame()){
            events_last_sent = millis();
        } else {
            stop_events_client();
        }
    }
}
