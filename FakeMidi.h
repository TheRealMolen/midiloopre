#pragma once

//#include <MIDI.h>

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
