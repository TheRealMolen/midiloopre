#pragma once

#define DEBOUNCE_MS 80

template<int Pin>
class DebouncedInput {
public:
  void init() {
    pinMode(Pin, INPUT_PULLUP);
    m_state = 0;
    update(millis());
  }
  
  bool isDown() const   { return m_state & 1; }

  // returns true if state changed
  bool update(uint16_t nowMillis) {
    bool isDownNow = !digitalRead(Pin);
    bool wasDown = isDown();
    if (isDownNow != wasDown) {
      if ((nowMillis - lastPressMillis()) > DEBOUNCE_MS) {
        setState(isDownNow, nowMillis);
        return true;
      }
    }
    return false;
  }
  
private:
  uint16_t lastPressMillis() const { return m_state >> 1; }
  void setState(bool isDownNow, uint16_t nowMillis) {
    m_state = (nowMillis << 1) | isDownNow;
  }

  uint8_t m_state; // bottom bit is isDown, remainder are the last press time in milliseconds  
};
