
#define D_REALMIDI


#include <MIDI.h>

#ifdef D_REALMIDI
MIDI_CREATE_DEFAULT_INSTANCE();
#define dbg(msg) do{}while(0)
#else

class FakeMidi
{
public:
  void begin(int channel) {
    Serial.begin(115200);
    Serial.println("FakeMIDI engage....");
  }
  
  void sendNoteOn(byte note, byte velocity, byte channel) {
    Serial.print("ON  ");
    Serial.print((int)note);
    Serial.print(" @ c");
    Serial.println((int)channel);
  }
  void sendNoteOff(byte note, byte velocity, byte channel) {
    Serial.print("OFF ");
    Serial.print((int)note);
    Serial.print(" @ c");
    Serial.println((int)channel);
  }

  char cmd;
  char note;
  
  bool read() {
    if (Serial.available() < 2) {
      return false;
    }
    cmd = Serial.read();
    note = Serial.read();
    Serial.print("read ");
    Serial.print(cmd);
    Serial.print(note);
    Serial.println("<");
    return true;
  }

  midi::MidiType getType() {
    if (cmd == 'p')
      return midi::NoteOn;
    if (cmd == 'o')
      return midi::NoteOff;
    return midi::PitchBend;
  }

  byte getData1() {
    return 0x3a + (note - 'a');
  }
  byte getData2() {
    return 100;
  }  
 
  void turnThruOff() {}
  
};
FakeMidi MIDI;

void dbg(const char* msg) {
  Serial.println(msg);
}
#endif


byte recording = 0;
byte recordWasDown = 0;
unsigned long lastRecordPress = 0;

#define DEBOUNCE_MS 50

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
unsigned int playbackTicksElapsed = 0;
unsigned long recordStartTimeUs = 0;
unsigned long lastTimeUs = 0;

const byte SendChannel = 6;


void midiClearAllNotes();


void startRecording() {
  dbg("Start recording");
  midiClearAllNotes();
  numNotesInBuf = 0;
  recordStartTimeUs = micros();
  recording = true;
  digitalWrite(9, true);
}

void stopRecording() {
  dbg("STOP recording");
  recording = false;
  playHead = 0;
  playbackTicksElapsed = 0;
  lastTimeUs = micros();
  recordBufferTicks = (lastTimeUs - recordStartTimeUs + (US_PER_TICK / 2)) / US_PER_TICK;
  digitalWrite(9, false);
  
#ifndef D_REALMIDI
  Serial.print("recbuf: [");
  for (int i=0; i<numNotesInBuf; ++i) {
    MidiNoteUpdate* n = recordBuf + i;
    Serial.print(n->tick);
    Serial.print(n->noteOn ? " on  " : " OFF ");
    Serial.print((int)n->note);
    Serial.print(", ");
  }
  Serial.println("]");
#endif
}

void recordNote(bool on, byte note, byte velocity) {
  dbg("recordNote");
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


#define NOTE_MEMORY 10
byte playingNotes[NOTE_MEMORY];
byte numPlayingNotes = 0;

void rememberNoteOn(byte note) {
  if (numPlayingNotes >= NOTE_MEMORY)
    return;
    
  for (byte i=0; i<numPlayingNotes; ++i) {
    if (playingNotes[i] == note)
      return;
  }

  playingNotes[numPlayingNotes] = note;
  ++numPlayingNotes;
}
void forgetNoteOn(byte note) {
  for (byte i=0; i<numPlayingNotes; ++i) {
    if (playingNotes[i] == note) {
      playingNotes[i] = playingNotes[numPlayingNotes-1];
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
  for (byte i=0; i<numPlayingNotes; ++i) {
    MIDI.sendNoteOff(playingNotes[i], 0, SendChannel);
  }
  numPlayingNotes = 0;
}



void setup() {
  MIDI.begin(MIDI_CHANNEL_OMNI);
 // MIDI.turnThruOff();
  pinMode(13, OUTPUT);
  pinMode(10, INPUT_PULLUP);
  pinMode(9, OUTPUT);

  lastTimeUs = micros();
}

void updateUI() {
  byte recordDown = !digitalRead(10);
  if (recordDown != recordWasDown) {
    unsigned long now = millis();
    if ((now - lastRecordPress) > DEBOUNCE_MS) {
      recordWasDown = recordDown;
      lastRecordPress = now;

      if (recordDown) {
        if (!recording)
          startRecording();
        else
          stopRecording();
      }
    }
  }
}

unsigned long tickProgressUs = 0;
void updatePlayback() {
  unsigned long now = micros();
  unsigned long deltaUs = now - lastTimeUs;
  lastTimeUs = now;
  tickProgressUs += deltaUs;
  while (tickProgressUs > US_PER_TICK) {
    tickProgressUs -= US_PER_TICK;

    ++playbackTicksElapsed;
    if (playbackTicksElapsed > recordBufferTicks) {
      playHead = 0;
      playbackTicksElapsed = 0;
    }

    while ((playHead < numNotesInBuf) && (recordBuf[playHead].tick == playbackTicksElapsed)) {
#ifndef D_REALMIDI
      Serial.print("play: usleft=");
      Serial.print(tickProgressUs);
      Serial.print(", dUs=");
      Serial.print(deltaUs);
      Serial.print(", elapsed=");
      Serial.print(playbackTicksElapsed);
      Serial.print(", playhead=");
      Serial.print(playHead);
      Serial.println();
#endif
      midiSend(&recordBuf[playHead]);
      ++playHead;
    }
  }
}


void handleMidi() {
  switch(MIDI.getType())
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

void loop() {
  updateUI();

  if (!recording)
    updatePlayback();

  if (MIDI.read())
    handleMidi();
}
