#include "Buzzer.h"

#include <Arduino.h>

#include "Config.h"
#include "Gpio.h"

Buzzer::Buzzer(){}

void Buzzer::beep(int beeps_amount, int duration){
    ledcAttach(gpio::buzzer, gpio::pwm_buzzer_frequency, gpio::pwm_buzzer_resolution);

    for (int beep = 0; beep < beeps_amount; beep++){
        ledcWriteTone(gpio::buzzer, Buzzer::beep_frequency);
        ledcWrite(gpio::buzzer, Buzzer::volume);
        delay(duration);
        ledcWriteTone(gpio::buzzer, 0);
        ledcWrite(gpio::buzzer, 0);
        delay(duration);
    }

    ledcDetach(gpio::buzzer);
}

void Buzzer::play_all_notes(){
    ledcAttach(gpio::buzzer, gpio::pwm_buzzer_frequency, gpio::pwm_buzzer_resolution);

    Serial.println(sizeof Buzzer::notes / sizeof Buzzer::notes[0]);

    for(int note = 0; note < 21; note++){
        Serial.println(Buzzer::notes[note]);
        ledcWriteTone(gpio::buzzer, Buzzer::notes[note]);
        delay(300);
    }

    ledcDetach(gpio::buzzer);
}

void Buzzer::play_cucaracha(){
    ledcAttach(gpio::buzzer, gpio::pwm_buzzer_frequency, gpio::pwm_buzzer_resolution);

    for(int note = 0; note < 34; note++){
        ledcWriteTone(gpio::buzzer, Buzzer::cucaracha_notes[note]);
        ledcWrite(gpio::buzzer, Buzzer::volume);
        delay(Buzzer::cucaracha_durations[note]);
        ledcWriteTone(gpio::buzzer, 0);
        ledcWrite(gpio::buzzer, 0);
        delay(Buzzer::cucaracha_wait[note]);
    }

    ledcDetach(gpio::buzzer);
}

void Buzzer::set_volume(int volume){

    if(config::beep_enabled == false){
        config::beep_volume = 0;
        volume = 0;
    }

    if(volume < 0 ){
        Buzzer::volume = 0;
        return;
    }

    if(volume > 5 ){
        Buzzer::volume = 250;
        return;
    }

    config::beep_volume = volume;
    Buzzer::volume = volume * 50;   
}