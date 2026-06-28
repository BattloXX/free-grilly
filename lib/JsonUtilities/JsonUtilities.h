#pragma once

class Preferences;

struct jsonResult
{
    bool success;
    String message;
};

class JsonUtilities{

    public:
        void load_json_status(char *buffer);

        void load_json_settings(char *buffer);
        jsonResult save_json_settings(char* jsondata);

        void load_json_probes(char *buffer);
        jsonResult save_json_probes(char* jsondata);

        void load_opengrill_grill(char *buffer);
        jsonResult save_opengrill_grill(char* jsondata);

        void load_opengrill_probes(char *buffer);
        jsonResult save_opengrill_probes(char* jsondata);

        void load_json_wifiscan(char *buffer);

        /**
         * @brief Serialise the history ringbuffers for all 8 probes.
         *        Values are celsius × 10 (int16). buf_size should be >= 4096.
         */
        void load_json_history(char *buffer, size_t buf_size);
};
