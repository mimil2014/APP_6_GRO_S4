#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include <Oscil.h>
#include <tables/saw2048_int8.h>
#include <tables/square_no_alias_2048_int8.h>
#include <math.h>

#include "pcm_audio.hpp"
#include "song.hpp"
#include <limits.h>
#include <event_groups.h>


using Sawtooth = Oscil<SAW2048_NUM_CELLS, SAMPLE_RATE>;
using SquareWv = Oscil<SQUARE_NO_ALIAS_2048_NUM_CELLS, SAMPLE_RATE>;

#define PIN_SW1 2
#define PIN_SW2 3
#define PIN_RV1 A0
#define PIN_RV2 A1
#define PIN_RV3 A2
#define PIN_RV4 A3

// Variables
int potValue[4] = {0,0,0,0};
bool initMel = 0;
bool mel_ON = 0;
EventGroupHandle_t eventGroupButton;
bool initPWM = 0;
bool buttonRelease = 0;
bool initVCA = 0;
float q_org;
float f_org;
bool initNote = 0;
int8_t tempoMS = 0;
// Prototypes
void readPotentiometer(void *);
void ISR_button1(void);
void ISR_button2(void);
void waitButton(void *);
int8_t processVCF(int8_t vco);
int8_t processVCA(int8_t vcf);
// void melodie(void*);
void fillPCM(void *);
void createNote(void*);


SquareWv squarewv_;
Sawtooth sawtooth_;

void setNoteHz(float note)
{
    squarewv_.setFreq(note);
    sawtooth_.setFreq(note);
}

int8_t nextSample()
{
    // VCO
    int8_t vco = sawtooth_.next() + squarewv_.next();
    
    // VCF 
    int8_t vcf = processVCF(vco);
    // int8_t vcf = vco;

    // VCA (disabled)
    int8_t vca = processVCA(vcf);   
    //int8_t vca = vcf;

    int8_t output = vca;

    return output;
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(PIN_SW1, INPUT);
    
    Serial.begin(9600);

    // Oscillator
    squarewv_ = SquareWv(SQUARE_NO_ALIAS_2048_DATA);
    sawtooth_ = SquareWv(SAW2048_DATA);
    setNoteHz(110.0);

    pcmSetup();

    eventGroupButton = xEventGroupCreate();

    attachInterrupt(digitalPinToInterrupt(PIN_SW1),ISR_button1,CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_SW2),ISR_button2,CHANGE);

    xTaskCreate(readPotentiometer,"readPot",128,NULL,3,NULL);

    // xTaskCreate(waitButton,"waitButton",128,NULL,2,NULL);
    xTaskCreate(fillPCM,"melodie", 500, NULL, 2, NULL);
    xTaskCreate(createNote,"createNote", 128, NULL, 1, NULL);


    Serial.println("Synth prototype ready");
}

void loop()
{    
    // xEventGroupWaitBits(
    //                 eventGroupButton,
    //                 0x01,
    //                 pdTRUE,
    //                 pdFALSE,
    //                 portMAX_DELAY);
    //     // xTaskNotifyWait(0x00,ULONG_MAX,&ulNotifyValue,portMAX_DELAY);
    //     Serial.println("Waked up 2");
}

void readPotentiometer(void *)
{
    for(;;)
    {
        for(int i=0; i<4; i++)
        {
        potValue[i] = analogRead(i);
        // Serial.print(potValue[i]);
        }
        q_org = (potValue[0]*1.0f)/1023.0f;
        f_org = ((potValue[1]*(PI/2.0f))/1023.0f);
        tempoMS = ((60*250)/map(potValue[2],0,1023,60,240));
        //Serial.println(potValue[1]);
        vTaskDelay(100/portTICK_PERIOD_MS); // 10Hz -> 100ms
    }
}

void ISR_button1()
{
    if (digitalRead(PIN_SW1))
    {
        xEventGroupSetBitsFromISR(eventGroupButton, 0x01, pdFALSE);
        initPWM = 1;
        initVCA = 1;
    }
    else
    {
        buttonRelease = 1;
        // 
        // stopPlayback();
    }
}

void ISR_button2()
{
    if(digitalRead(PIN_SW2))
    {
        xEventGroupSetBitsFromISR(eventGroupButton,0x02,pdFALSE);
        initNote = 1;
        initVCA = 1;
    }
    else
    {
        // xEventGroupClearBitsFromISR(eventGroupButton,0x02);
        buttonRelease = 1;
    }
}

void waitButton(void *)
{
    for(;;)
    {
        // xEventGroupWaitBits(
        //             eventGroupButton,
        //             0x03,    // Check bit 0 and 1.
        //             pdTRUE,  // Clear bit on exit.
        //             pdTRUE, // Wait all the bits
        //             portMAX_DELAY); // no Timeout
        xEventGroupWaitBits(eventGroupButton, 0x01, pdFALSE, pdFALSE, portMAX_DELAY);
        Serial.println("Waked up 2");
    }
}

int8_t processVCF(int8_t vco)
{
    static int8_t y [3] = {0,0,0};
    int16_t result = 0;
    int8_t x = vco;

    for(int8_t i = 2; i > 0; i--)
    {
        if(i > 0)
        {
            y[i] = y[i-1];
        }
    }

    //float q_org = (potValue[1]*1.0f)/1023.0f;
    //float f_org = (potValue[2]*(4000.0f*((2.0f*PI)/8000.0f)))/1023.0f;
    int16_t q = q_org*256;
    int16_t f = f_org*256;   
    int16_t fb = q + q/(1+f);
    result = (f^2)*x - (2-2*f+f*fb-(f^2)*fb)*y[1] + (1-2*f+f*fb+(f^2)-(f^2)*fb)*y[2];
    y[0] = 0xFF & (result >> 8);

    return y[0];
}

int8_t processVCA(int8_t vcf)
{   
    static int16_t nb_echan = 0;
    static int16_t compteur = 0;
    static float gain = 0;
    if(initVCA)
    {
        initVCA = 0;
        nb_echan = map(potValue[3],1023,0,0,10000);
        compteur = 0;

    }

    if(buttonRelease)
    {
        if(compteur > nb_echan)
        {
            gain = 0;
            buttonRelease = 0;
            xEventGroupClearBitsFromISR(eventGroupButton, 0x01);
            xEventGroupClearBitsFromISR(eventGroupButton, 0x02);
            stopPlayback();
        }
        else
        {
            gain = 1.0 -(float(compteur) / float(nb_echan)); // 
            // Serial.println(gain);
            compteur ++;
        }

    }
    else
    {
        gain = 1;
    }

    return int8_t(gain * vcf);
}

void fillPCM(void *)
{
    uint16_t nb_sample; // 512 max
    int8_t note = 0;
    int8_t init = 0; 
    for(;;)
    {   
        xEventGroupWaitBits(eventGroupButton, 0x01, pdFALSE, pdFALSE, portMAX_DELAY);

        if(initPWM)
        {
            pcmSetup();
            note = 0;
            nb_sample = 0;
            initPWM = 0;
            init = 0;
        }

        if(init == 0)
        {
            nb_sample = ((128*tempoMS)/16)*song[note].duration;
            // Serial.print(nb_sample);
            // Serial.print("      ");
            // Serial.println(song[note].freq);
            setNoteHz(song[note].freq);
            init = 1;
        }

        if(!pcmBufferFull() && nb_sample != 0)
        {
            pcmAddSample(nextSample());
            nb_sample--;
            // Serial.println(nb_sample);
            if(nb_sample == 0)
            {

                // Serial.print("sample = 0");
                note++;

                if(note == 4)
                {
                    note = 0;
                }

                init = 0;
            }     
        }

        // taskYIELD();
    }
}

void createNote(void*)
{
    for(;;)
    {
        xEventGroupWaitBits(eventGroupButton, 0x02, pdFALSE, pdFALSE, portMAX_DELAY);
        if(initNote)
        {
            pcmSetup();
            initNote = 0;
            float freq = map(potValue[2],0,1023,1,4000);
            setNoteHz(freq);
        }
        

       if(!pcmBufferFull())
       {
           pcmAddSample(nextSample());
       }
    //    taskYIELD();
    }
}
    