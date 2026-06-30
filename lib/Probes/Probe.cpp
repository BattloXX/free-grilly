#include <Arduino.h>
#include <SPI.h>
#include <math.h>
#include <chrono>

#include "Config.h"
#include "Buzzer.h"
#include "Probe.h"
#include "Gpio.h"
#include "Grill.h"

Probe::Probe(int number, int reference_kohm, int reference_celcius, int reference_beta) {
    Probe::number = number;
    Probe::reference_kohm = reference_kohm;
    Probe::reference_celcius = reference_celcius;
    Probe::reference_beta = reference_beta;
    
    // Default type
    Probe::set_type("grilleye_iris");
}

uint16_t Probe::read_adc_value() {
    Probe::select_probe(Probe::number);
    
    vTaskDelay(ADC_READ_DELAY_MS);
    
    digitalWrite(gpio::hspi_probes_cs, HIGH);
    delayMicroseconds(1);
    digitalWrite(gpio::hspi_probes_cs, LOW);
    delayMicroseconds(1);

    return SPI.transfer16(0x0000);
}

void Probe::select_probe(int probe_number) {

    switch (probe_number) {
        case 1:
            digitalWrite(gpio::mux_selector_a, LOW);
            digitalWrite(gpio::mux_selector_b, LOW);
            digitalWrite(gpio::mux_selector_c, HIGH);
            break;
        case 2:
            digitalWrite(gpio::mux_selector_a, LOW);
            digitalWrite(gpio::mux_selector_b, HIGH);
            digitalWrite(gpio::mux_selector_c, HIGH);
            break;
        case 3:
            digitalWrite(gpio::mux_selector_a, HIGH);
            digitalWrite(gpio::mux_selector_b, HIGH);
            digitalWrite(gpio::mux_selector_c, HIGH);
            break;
        case 4:
            digitalWrite(gpio::mux_selector_a, HIGH);
            digitalWrite(gpio::mux_selector_b, LOW);
            digitalWrite(gpio::mux_selector_c, HIGH);
            break;
        case 5:
            digitalWrite(gpio::mux_selector_a, LOW);
            digitalWrite(gpio::mux_selector_b, HIGH);
            digitalWrite(gpio::mux_selector_c, LOW);
            break;
        case 6:
            digitalWrite(gpio::mux_selector_a, HIGH);
            digitalWrite(gpio::mux_selector_b, HIGH);
            digitalWrite(gpio::mux_selector_c, LOW);
            break;
        case 7:
            digitalWrite(gpio::mux_selector_a, HIGH);
            digitalWrite(gpio::mux_selector_b, LOW);
            digitalWrite(gpio::mux_selector_c, LOW);
            break;
        case 8:
            digitalWrite(gpio::mux_selector_a, LOW);
            digitalWrite(gpio::mux_selector_b, LOW);
            digitalWrite(gpio::mux_selector_c, LOW);
            break;
    }
}

float Probe::read_adc_voltage() {
    
    uint16_t adc_value = Probe::read_adc_value();

    if (adc_value > ADC_PROBE_DISCONNECTED_VALUE) {
        return nanf(""); // If disconnected return NaN
    }

    // Calculate the voltage using linear scaling
    float voltage = (static_cast<float>(adc_value) / 65534.0) * ADC_REFERENCE_VOLTAGE;
    return voltage;
}

float Probe::calculate_temperature() {
    uint16_t adc_value = Probe::read_adc_value();

    if (adc_value > ADC_PROBE_DISCONNECTED_VALUE) {
        Probe::celcius    = 0;
        Probe::fahrenheit = 32;
        Probe::connected  = false;
        
        return nanf(""); // If disconnected return NaN
    }

    // Calculate the voltage using linear scaling
    float voltage = (static_cast<float>(adc_value) / 65534.0) * ADC_REFERENCE_VOLTAGE;

    float ref_kelvin = 1 / (reference_celcius + 273.15);
    float ref_beta = 1 / static_cast<float>(reference_beta);
    float ref_volt = (voltage * ADC_REFERENCE_KOHM) / (reference_kohm * (ADC_BASE_VOLTAGE - voltage));
    float log_volt = log(ref_volt);

    float temperature = (1 / (ref_kelvin + ref_beta * log_volt)) - 273.15;

    // Store temp in public property
    Probe::celcius     = temperature;
    Probe::fahrenheit  = (temperature * 1.8) + 32;

    Probe::temperature = 0;

    if(config::temperature_unit == "celcius"){
        Probe::temperature = Probe::celcius;
    }
    
    if(config::temperature_unit == "fahrenheit"){
        Probe::temperature = Probe::fahrenheit;
    }
    if (Probe::connected == false){Probe::connected_time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
    Probe::connected  = true;

    return temperature;
}

void Probe::check_temperature_status(){

    Probe::alarm      = false;

    // If the target temperature is 0.0 we do not beep. This prevents free-grilly
    // from beeping every time you boot or when you connect a new probe
    if(Probe::connected && Probe::target_temperature != 0.0){

        //* Target temperature mode
        if(Probe::minimum_temperature == 0.0){

            //* Ready temperature beep + alarm
            if(Probe::temperature >= Probe::target_temperature && Probe::has_beeped == false){
                Probe::has_beeped = true;
                Probe::alarm      = true;
            }

            //* Reset the alarm and beep if the temperature drops way too low
            if(Probe::temperature < (Probe::target_temperature - Probe::TEMP_HYSTERISIS_OFFSET) && Probe::has_beeped == true){
                Probe::has_beeped = false;
            }

            //* Only run if we need to beep before we reach the temperature
            if(config::beep_degrees_before > 0){

                //* Almost ready temperature beep
                if(Probe::temperature >= (Probe::target_temperature - config::beep_degrees_before) && Probe::has_beeped_before == false){
                    Probe::has_beeped_before = true;   
                    
                    grill::buzzer.beep(3, 400);
                }

                //* Reset the beep if the almost temperature drops way too low
                if(Probe::temperature < (Probe::target_temperature - config::beep_degrees_before - Probe::TEMP_HYSTERISIS_OFFSET) && Probe::has_beeped_before == true){
                    Probe::has_beeped_before = false;
                }
            }

        } else {
            //* Temperature range mode

            //* Alarm and Beep if outside
            if((Probe::temperature < Probe::minimum_temperature || Probe::temperature > Probe::target_temperature ) && Probe::has_beeped_outside == false){
                Probe::has_beeped_outside = true;
                Probe::alarm              = true;
            }

            //* Reset the beep and alarm if inside
            if((Probe::temperature > (Probe::minimum_temperature + Probe::TEMP_HYSTERISIS_OFFSET) && Probe::temperature < (Probe::target_temperature - Probe::TEMP_HYSTERISIS_OFFSET) ) && Probe::has_beeped_outside == true){
                Probe::has_beeped_outside = false;
            }
        }
    }
}

void Probe::set_name(String probe_name){
    
    Probe::name = probe_name;
}

void Probe::set_type(String probe_type, int reference_kohm, int reference_celcius, int reference_beta){
    
    if(probe_type == "grilleye_iris"){
        Probe::reference_beta    = 4250;
        Probe::reference_celcius = 25;
        Probe::reference_kohm    = 100;
        Probe::type              = "grilleye_iris";
        return;
    }

    if(probe_type == "ikea_fantast"){
        Probe::reference_beta    = 4250;
        Probe::reference_celcius = 25;
        Probe::reference_kohm    = 230;
        Probe::type              = "ikea_fantast";
        return;
    }
    
    if(probe_type == "maverick_et733"){
        Probe::reference_beta    = 4250;
        Probe::reference_celcius = 25;
        Probe::reference_kohm    = 200;
        Probe::type              = "maverick_et733";
        return;
    }

    if(probe_type == "weber_igrill"){
        Probe::reference_beta    = 3830;
        Probe::reference_celcius = 25;
        Probe::reference_kohm    = 100;
        Probe::type              = "weber_igrill";
        return;
    }
    
    Probe::reference_beta    = reference_beta;
    Probe::reference_celcius = reference_celcius;
    Probe::reference_kohm    = reference_kohm;
    Probe::type              = "custom";
    return;
}

void Probe::set_temperature(float target_temperature, float minimum_temperature){

    Probe::target_temperature = target_temperature;
    Probe::minimum_temperature = minimum_temperature;

    // If we switch mode from target to range we re-enable the beeps
    if(minimum_temperature == 0.0){
        Probe::has_beeped        = false;
        Probe::has_beeped_before = false;
    } else {
        Probe::has_beeped_outside = false;
    }
}

// ***********************************
// * History ringbuffer
// ***********************************

void Probe::push_history(float temp_c){
    history[history_head] = (int16_t)(temp_c * 10.0f);
    history_head = (history_head + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE) history_count++;
}

void Probe::push_coarse(float temp_c){
    // When the coarse buffer is full, halve its resolution: keep every 2nd
    // chronological sample and double the interval. Memory stays fixed; the
    // covered time window doubles each compaction.
    if (coarse_count >= COARSE_SIZE) {
        int16_t tmp[COARSE_SIZE];
        int base = (coarse_head - coarse_count + COARSE_SIZE) % COARSE_SIZE;
        int kept = 0;
        for (int i = 0; i < coarse_count; i += 2) {
            int idx     = (base + i) % COARSE_SIZE;
            tmp[kept++]  = coarse[idx];
        }
        for (int i = 0; i < kept; i++) coarse[i] = tmp[i];
        coarse_head      = kept;
        coarse_count     = kept;
        coarse_interval_s *= 2;
    }

    coarse[coarse_head] = (int16_t)(temp_c * 10.0f);
    coarse_head = (coarse_head + 1) % COARSE_SIZE;
    if (coarse_count < COARSE_SIZE) coarse_count++;
}

long Probe::seconds_to_target() const {
    if (!connected || target_temperature <= 0.0f || history_count < 6) return -1;

    const int N = (history_count < 20) ? history_count : 20;

    // Collect the last N samples in chronological order (oldest → index 0, newest → index N-1)
    float sum_x = 0, sum_y = 0, sum_xx = 0, sum_xy = 0;
    int base = (history_head - history_count + HISTORY_SIZE) % HISTORY_SIZE;

    for (int i = 0; i < N; i++){
        // Index of the (history_count - N + i)-th chronological sample
        int offset = (history_count - N + i);
        int idx    = (base + offset) % HISTORY_SIZE;
        float x = (float)i;
        float y = history[idx] / 10.0f;
        sum_x  += x;
        sum_y  += y;
        sum_xx += x * x;
        sum_xy += x * y;
    }

    float denom = (float)N * sum_xx - sum_x * sum_x;
    if (fabsf(denom) < 0.001f) return -1;

    float slope = ((float)N * sum_xy - sum_x * sum_y) / denom; // °C per sample interval

    if (slope <= 0.01f) return -1; // temperature not rising toward target

    float latest_temp = history[((history_head - 1) % HISTORY_SIZE + HISTORY_SIZE) % HISTORY_SIZE] / 10.0f;
    float remaining   = target_temperature - latest_temp;
    if (remaining <= 0.0f) return 0;

    float samples_needed = remaining / slope;
    return (long)(samples_needed * HISTORY_INTERVAL_S);
}

int Probe::get_history(int16_t* out, int max_count) const {
    if (history_count == 0 || max_count <= 0) return 0;
    int count = (history_count < max_count) ? history_count : max_count;
    int base  = (history_head - history_count + HISTORY_SIZE) % HISTORY_SIZE;

    for (int i = 0; i < count; i++){
        int idx = (base + i) % HISTORY_SIZE;
        out[i]  = history[idx];
    }
    return count;
}

int Probe::get_coarse(int16_t* out, int max_count) const {
    if (coarse_count == 0 || max_count <= 0) return 0;
    int count = (coarse_count < max_count) ? coarse_count : max_count;
    int base  = (coarse_head - coarse_count + COARSE_SIZE) % COARSE_SIZE;

    for (int i = 0; i < count; i++){
        int idx = (base + i) % COARSE_SIZE;
        out[i]  = coarse[idx];
    }
    return count;
}