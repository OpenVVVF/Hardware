/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    melody.c
  * @brief   Hedwig's Theme (Harry Potter) melody sequencer.
  *
  * Original Arduino melody by Robson Couto:
  * https://github.com/robsoncouto/arduino-songs
  *
  * Notes are transposed up by 3 octaves (x8) so that with the project's
  * PWM = note * 0.5 convention the carrier stays inside 800 Hz - 10 kHz.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "melody.h"

/* -------------------------------------------------------------------------- */
/*  Note definitions (equal-tempered, Hz)                                     */
/* -------------------------------------------------------------------------- */
#define NOTE_REST 0.0f
#define NOTE_B0   31.0f
#define NOTE_C1   33.0f
#define NOTE_CS1  35.0f
#define NOTE_D1   37.0f
#define NOTE_DS1  39.0f
#define NOTE_E1   41.0f
#define NOTE_F1   44.0f
#define NOTE_FS1  46.0f
#define NOTE_G1   49.0f
#define NOTE_GS1  52.0f
#define NOTE_A1   55.0f
#define NOTE_AS1  58.0f
#define NOTE_B1   62.0f
#define NOTE_C2   65.0f
#define NOTE_CS2  69.0f
#define NOTE_D2   73.0f
#define NOTE_DS2  78.0f
#define NOTE_E2   82.0f
#define NOTE_F2   87.0f
#define NOTE_FS2  93.0f
#define NOTE_G2   98.0f
#define NOTE_GS2  104.0f
#define NOTE_A2   110.0f
#define NOTE_AS2  117.0f
#define NOTE_B2   123.0f
#define NOTE_C3   131.0f
#define NOTE_CS3  139.0f
#define NOTE_D3   147.0f
#define NOTE_DS3  156.0f
#define NOTE_E3   165.0f
#define NOTE_F3   175.0f
#define NOTE_FS3  185.0f
#define NOTE_G3   196.0f
#define NOTE_GS3  208.0f
#define NOTE_A3   220.0f
#define NOTE_AS3  233.0f
#define NOTE_B3   247.0f
#define NOTE_C4   262.0f
#define NOTE_CS4  277.0f
#define NOTE_D4   294.0f
#define NOTE_DS4  311.0f
#define NOTE_E4   330.0f
#define NOTE_F4   349.0f
#define NOTE_FS4  370.0f
#define NOTE_G4   392.0f
#define NOTE_GS4  415.0f
#define NOTE_A4   440.0f
#define NOTE_AS4  466.0f
#define NOTE_B4   494.0f
#define NOTE_C5   523.0f
#define NOTE_CS5  554.0f
#define NOTE_D5   587.0f
#define NOTE_DS5  622.0f
#define NOTE_E5   659.0f
#define NOTE_F5   698.0f
#define NOTE_FS5  740.0f
#define NOTE_G5   784.0f
#define NOTE_GS5  831.0f
#define NOTE_A5   880.0f
#define NOTE_AS5  932.0f
#define NOTE_B5   988.0f
#define NOTE_C6   1047.0f
#define NOTE_CS6  1109.0f
#define NOTE_D6   1175.0f
#define NOTE_DS6  1245.0f
#define NOTE_E6   1319.0f
#define NOTE_F6   1397.0f
#define NOTE_FS6  1480.0f
#define NOTE_G6   1568.0f
#define NOTE_GS6  1661.0f
#define NOTE_A6   1760.0f
#define NOTE_AS6  1865.0f
#define NOTE_B6   1976.0f
#define NOTE_C7   2093.0f
#define NOTE_CS7  2217.0f
#define NOTE_D7   2349.0f
#define NOTE_DS7  2489.0f
#define NOTE_E7   2637.0f
#define NOTE_F7   2794.0f
#define NOTE_FS7  2960.0f
#define NOTE_G7   3136.0f
#define NOTE_GS7  3322.0f
#define NOTE_A7   3520.0f
#define NOTE_AS7  3729.0f
#define NOTE_B7   3951.0f
#define NOTE_C8   4186.0f
#define NOTE_CS8  4435.0f
#define NOTE_D8   4699.0f
#define NOTE_DS8  4978.0f

#define OCTAVE_SHIFT  6.0f
#define TEMPO         72

/* Whole note duration in ms: (60000 * 4) / tempo */
#define WHOLE_NOTE_MS ((60000U * 4U) / TEMPO)

/* -------------------------------------------------------------------------- */
/*  Helpers to build melody events                                            */
/* -------------------------------------------------------------------------- */
#define DUR(d)        ((uint16_t)(((uint32_t)WHOLE_NOTE_MS + ((d) / 2U)) / (d)))
#define DUR_DOT(d)    ((uint16_t)((((uint32_t)WHOLE_NOTE_MS * 3U) + ((d))) / ((d) * 2U)))
#define ON_TIME(t)    ((uint16_t)(((uint32_t)(t) * 9U + 5U) / 10U))
#define OFF_TIME(t)   ((uint16_t)((t) - ON_TIME(t)))

#define EV(note, total_ms)  { (note) * OCTAVE_SHIFT, ON_TIME(total_ms), OFF_TIME(total_ms) }
#define EV_REST(total_ms)   { NOTE_REST, 0, (total_ms) }

typedef struct
{
    float    note_hz;   /* 0.0f for REST */
    uint16_t on_ms;
    uint16_t off_ms;
} MelodyEvent_t;

/* -------------------------------------------------------------------------- */
/*  Hedwig's Theme                                                            */
/* -------------------------------------------------------------------------- */
static const MelodyEvent_t hedwig_theme[] =
{
    EV_REST(DUR(2)),
    EV(NOTE_D4, DUR(4)),
    EV(NOTE_G4, DUR_DOT(4)),
    EV(NOTE_AS4, DUR(8)),
    EV(NOTE_A4, DUR(4)),
    EV(NOTE_G4, DUR(2)),
    EV(NOTE_D5, DUR(4)),
    EV(NOTE_C5, DUR_DOT(2)),
    EV(NOTE_A4, DUR_DOT(2)),
    EV(NOTE_G4, DUR_DOT(4)),
    EV(NOTE_AS4, DUR(8)),
    EV(NOTE_A4, DUR(4)),
    EV(NOTE_F4, DUR(2)),
    EV(NOTE_GS4, DUR(4)),
    EV(NOTE_D4, DUR_DOT(1)),
    EV(NOTE_D4, DUR(4)),

    EV(NOTE_G4, DUR_DOT(4)),
    EV(NOTE_AS4, DUR(8)),
    EV(NOTE_A4, DUR(4)),
    EV(NOTE_G4, DUR(2)),
    EV(NOTE_D5, DUR(4)),
    EV(NOTE_F5, DUR(2)),
    EV(NOTE_E5, DUR(4)),
    EV(NOTE_DS5, DUR(2)),
    EV(NOTE_B4, DUR(4)),
    EV(NOTE_DS5, DUR_DOT(4)),
    EV(NOTE_D5, DUR(8)),
    EV(NOTE_CS5, DUR(4)),
    EV(NOTE_CS4, DUR(2)),
    EV(NOTE_B4, DUR(4)),
    EV(NOTE_G4, DUR_DOT(1)),
    EV(NOTE_AS4, DUR(4)),

    EV(NOTE_D5, DUR(2)),
    EV(NOTE_AS4, DUR(4)),
    EV(NOTE_D5, DUR(2)),
    EV(NOTE_AS4, DUR(4)),
    EV(NOTE_DS5, DUR(2)),
    EV(NOTE_D5, DUR(4)),
    EV(NOTE_CS5, DUR(2)),
    EV(NOTE_A4, DUR(4)),
    EV(NOTE_AS4, DUR_DOT(4)),
    EV(NOTE_D5, DUR(8)),
    EV(NOTE_CS5, DUR(4)),
    EV(NOTE_CS4, DUR(2)),
    EV(NOTE_D4, DUR(4)),
    EV(NOTE_D5, DUR_DOT(1)),
    EV_REST(DUR(4)),
    EV(NOTE_AS4, DUR(4)),

    EV(NOTE_D5, DUR(2)),
    EV(NOTE_AS4, DUR(4)),
    EV(NOTE_D5, DUR(2)),
    EV(NOTE_AS4, DUR(4)),
    EV(NOTE_F5, DUR(2)),
    EV(NOTE_E5, DUR(4)),
    EV(NOTE_DS5, DUR(2)),
    EV(NOTE_B4, DUR(4)),
    EV(NOTE_DS5, DUR_DOT(4)),
    EV(NOTE_D5, DUR(8)),
    EV(NOTE_CS5, DUR(4)),
    EV(NOTE_CS4, DUR(2)),
    EV(NOTE_AS4, DUR(4)),
    EV(NOTE_G4, DUR_DOT(1)),
};

#define MELODY_LEN (sizeof(hedwig_theme) / sizeof(hedwig_theme[0]))

/* -------------------------------------------------------------------------- */
/*  Playback state                                                            */
/* -------------------------------------------------------------------------- */
static volatile uint8_t  melody_active = 0;
static volatile uint16_t melody_index = 0;
static volatile uint16_t melody_timer_ms = 0;
static volatile uint8_t  melody_in_note = 1;  /* 1 = sounding, 0 = rest gap */
static volatile float    manual_note_hz = 1567.98f; /* G6, ~800 Hz PWM default */
static volatile float    current_note_hz = 1567.98f;

/* -------------------------------------------------------------------------- */
/*  Public API                                                                */
/* -------------------------------------------------------------------------- */
void Melody_Start(void)
{
    melody_active = 1;
    melody_index = 0;
    melody_in_note = 1;
    melody_timer_ms = hedwig_theme[0].on_ms;
    current_note_hz = hedwig_theme[0].note_hz;
}

void Melody_Stop(void)
{
    melody_active = 0;
}

void Melody_SetManualNote(float note_hz)
{
    manual_note_hz = note_hz;
    melody_active = 0;
    current_note_hz = note_hz;
}

void Melody_Update(uint16_t ms_elapsed)
{
    if (!melody_active)
    {
        current_note_hz = manual_note_hz;
        return;
    }

    while (ms_elapsed > 0)
    {
        if (ms_elapsed >= melody_timer_ms)
        {
            ms_elapsed -= melody_timer_ms;
            melody_timer_ms = 0;

            if (melody_in_note)
            {
                /* Switch to rest gap. */
                melody_in_note = 0;
                melody_timer_ms = hedwig_theme[melody_index].off_ms;
                current_note_hz = NOTE_REST;
            }
            else
            {
                /* Advance to next note. */
                melody_in_note = 1;
                melody_index++;
                if (melody_index >= MELODY_LEN)
                {
                    melody_index = 0; /* loop */
                }
                melody_timer_ms = hedwig_theme[melody_index].on_ms;
                current_note_hz = hedwig_theme[melody_index].note_hz;
            }
        }
        else
        {
            melody_timer_ms -= ms_elapsed;
            ms_elapsed = 0;
        }
    }
}

float Melody_GetCurrentNoteHz(void)
{
    return current_note_hz;
}

uint8_t Melody_IsActive(void)
{
    return melody_active;
}
