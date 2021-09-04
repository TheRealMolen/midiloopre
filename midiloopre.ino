#include <Adafruit_NeoPixel.h>
#include <MIDI.h>
#include "DebouncedInput.h"

#define PIN_UIPIXEL       11
#define PIN_BTN_REC       12

MIDI_CREATE_DEFAULT_INSTANCE();

DebouncedInput<PIN_BTN_REC> recordButton;
byte recording = 0;

#define US_PER_TICK 512   // half ms resolution == max loop len ~32seconds
#define MAX_US_PER_RECORDING (((unsigned long)US_PER_TICK) * ((unsigned long)0xffff))


struct MidiNoteUpdate {
  unsigned int tick;
  byte note : 7;
  byte noteOn : 1;
  byte velocity;    // top bit unused
};
#define MAX_NOTES 255
using RecordBufIndex = byte;
MidiNoteUpdate recordBuf[MAX_NOTES];
RecordBufIndex numNotesInBuf = 0;
RecordBufIndex playHead = 0;
unsigned int recordBufferTicks = 1;
unsigned int playbackUsPerTick = 0; // forces the ui to update on boot
unsigned int playbackTicksElapsed = 0;
unsigned long recordStartTimeUs = 0;
unsigned long lastTimeUs = 0;

Adafruit_NeoPixel uiPixel(1, 11, NEO_GRB + NEO_KHZ800);

const byte SendChannel = 6;


void midiClearAllNotes();


void startRecording() {
  midiClearAllNotes();
  numNotesInBuf = 0;
  recordStartTimeUs = micros();
  recording = true;
}

void stopRecording() {
  recording = false;
  playHead = 0;
  playbackTicksElapsed = 0;
  lastTimeUs = micros();
  recordBufferTicks = (lastTimeUs - recordStartTimeUs + (US_PER_TICK)) / US_PER_TICK;
}

void recordNote(bool on, byte note, byte velocity) {
  unsigned long usSinceStart = (micros() - recordStartTimeUs);
  if (usSinceStart >= MAX_US_PER_RECORDING) {
    stopRecording();
    return;
  }
  unsigned int ticksSinceStart = usSinceStart / US_PER_TICK;

  digitalWrite(13, on ? HIGH : LOW);

  MidiNoteUpdate* n = recordBuf + numNotesInBuf;
  n->tick = ticksSinceStart;
  n->noteOn = on;
  n->note = note;
  n->velocity = velocity;

  ++numNotesInBuf;
  if (numNotesInBuf >= MAX_NOTES)
    stopRecording();
}


#define NOTE_ON_MEMORY 10
byte playingNotes[NOTE_ON_MEMORY];
byte numPlayingNotes = 0;

void rememberNoteOn(byte note) {
  if (numPlayingNotes >= NOTE_ON_MEMORY)
    return;

  for (byte i = 0; i < numPlayingNotes; ++i) {
    if (playingNotes[i] == note)
      return;
  }

  playingNotes[numPlayingNotes] = note;
  ++numPlayingNotes;
}
void forgetNoteOn(byte note) {
  for (byte i = 0; i < numPlayingNotes; ++i) {
    if (playingNotes[i] == note) {
      playingNotes[i] = playingNotes[numPlayingNotes - 1];
      --numPlayingNotes;
      return;
    }
  }
}
void midiSend(const struct MidiNoteUpdate* note) {
  if (note->noteOn) {
    MIDI.sendNoteOn(note->note, note->velocity, SendChannel);
    rememberNoteOn(note->note);
  }
  else {
    MIDI.sendNoteOff(note->note, 0, SendChannel);
    forgetNoteOn(note->note);
  }
}
void midiClearAllNotes() {
  for (byte i = 0; i < numPlayingNotes; ++i) {
    MIDI.sendNoteOff(playingNotes[i], 0, SendChannel);
  }
  numPlayingNotes = 0;
}


void updateUI() {
  bool uiChanged = false;
  uint16_t nowMs = millis();

  // --- update recording state ---
  if (recordButton.update(nowMs)) {
    if (recordButton.isDown()) {
      if (!recording)
        startRecording();
      else
        stopRecording();

      uiChanged = true;
    } 
  }
  
  // --- update playback speed ---
  uint32_t playbackCol;
  {
    unsigned int oldUsPerTick = playbackUsPerTick;
    
    long speedInput = analogRead(0) - 512;   // range is [0,1023]
    speedInput >>= 3;
    speedInput = speedInput * speedInput * speedInput;
    long speedExp = speedInput >> 10;
    
    if (speedExp >= -1 && speedExp <= 1) {
      playbackUsPerTick = US_PER_TICK;
      playbackCol = uiPixel.ColorHSV(21845, 255, 200);
    }
    else if (speedExp < 0) {
      playbackUsPerTick = map(speedExp, -256, -1, 8 * US_PER_TICK, US_PER_TICK);
      playbackCol = uiPixel.ColorHSV(map(speedExp, -256, -1, 6000, 20000), 220, 230);
    }
    else {
      playbackUsPerTick = map(speedExp, 1, 244, US_PER_TICK, 32);
      playbackCol = uiPixel.ColorHSV(map(speedExp, 1, 244, 21900, 37000), 150, 255);
    }

    if (playbackUsPerTick != oldUsPerTick)
      uiChanged = true;
  }

  if (uiChanged) {
    uint32_t colour = recording ? uiPixel.Color(240, 30, 30) : playbackCol;
    uiPixel.setPixelColor(0, uiPixel.gamma32(colour));
    uiPixel.show();
  }
}

unsigned long tickProgressUs = 0;

void updatePlayback() {
  unsigned long now = micros();
  unsigned long deltaUs = now - lastTimeUs;
  lastTimeUs = now;
  tickProgressUs += deltaUs;
  while (tickProgressUs > playbackUsPerTick) {
    tickProgressUs -= playbackUsPerTick;

    ++playbackTicksElapsed;
    if (playbackTicksElapsed > recordBufferTicks) {
      playHead = 0;
      playbackTicksElapsed = 0;
    }

    while ((playHead < numNotesInBuf) && (recordBuf[playHead].tick <= playbackTicksElapsed)) {
      midiSend(&recordBuf[playHead]);
      ++playHead;
    }
  }
}


void handleMidi() {
  switch (MIDI.getType())
  {
    case midi::NoteOn:
      if (recording)
        recordNote(true, MIDI.getData1(), MIDI.getData2());
      break;

    case midi::NoteOff:
      if (recording)
        recordNote(false, MIDI.getData1(), MIDI.getData2());
      break;

    default:
      break;
  }
}




void setup() {
  MIDI.begin(MIDI_CHANNEL_OMNI);
  pinMode(13, OUTPUT);
  
  recordButton.init();
  
  uiPixel.begin();
  uiPixel.show();
  uiPixel.setBrightness(20);

  lastTimeUs = micros();
}

void loop() {
  updateUI();

  if (!recording)
    updatePlayback();

  if (MIDI.read())
    handleMidi();
}
