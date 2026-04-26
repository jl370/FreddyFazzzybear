#include <math.h>
#include "DFRobotDFPlayerMini.h"
#include <EEPROM.h>

HardwareSerial dfSerial(1);
DFRobotDFPlayerMini myDFPlayer;

const int DATA_PIN  = 4;  // DS
const int LATCH_PIN = 5;  // STCP
const int CLOCK_PIN = 6;  // SHCP
byte ledState = 0; // Holds the current state of all 8 outputs (00000000)

const uint8_t BTN_PINS[5]   = {9, 7, 10, 18, 19};
const uint8_t DF_BUSY_PIN   = 3;
const uint8_t ISD_REC_PIN   = 5;
const uint8_t ISD_PLAYE_PIN = 7;

const uint8_t RELAY_DF  = 20;
const uint8_t RELAY_ISD = 21;

// ── DFPlayer track numbers ─────────────────────────────────────────────────────

// Number words: index into this with the digit value
// Ones 0-9: tracks 1-10
const uint8_t TRK_ZERO     = 1;  // "zero"   (1)
// ones[n] = n + 1  for n in 0..9  → track 1="zero", 2="one", ... 10="nine"

// Teens 10-19: tracks 11-20
// teens[n] = n + 1 for n in 10..19 → track 11="ten", 12="eleven", ... 20="nineteen"

// Tens 20,30,...90: tracks 21-28
// tens[t] = t + 19  for t in 2..9  → track 21="twenty", 22="thirty", ... 28="ninety"

// Phrase words
const uint8_t TRK_RED          = 29;
const uint8_t TRK_YELLOW       = 30;
const uint8_t TRK_GREEN        = 31;
const uint8_t TRK_BLUE         = 32;
const uint8_t TRK_WHITE        = 33;

const uint8_t TRK_PRESS_THE    = 34; // "Press the"
const uint8_t TRK_BUTTON       = 35; // "button"
const uint8_t TRK_YOU_GOT      = 36; // "You got"
const uint8_t TRK_POINTS       = 37; // "points"
const uint8_t TRK_GO           = 38; // "Go!"
const uint8_t TRK_FAIL         = 39; // Fail / Freddy scream
const uint8_t TRK_HIGH_SCORE   = 40; // "New high score!"
const uint8_t TRK_PLAY_SIMON   = 41; // "to play simon says"
const uint8_t TRK_PLAY_VOICE   = 42; // "to record and playback your voice"
const uint8_t TRK_PLAY_MOLE    = 43; // "to play wack-a-mole"
const uint8_t TRK_INST_SIMON   = 44; // "Press the buttons in the same order they light up"
const uint8_t TRK_INST_HOLD    = 45; // "Hold the"
const uint8_t TRK_INST_REC     = 46; // "to record"
const uint8_t TRK_INST_PLAY    = 47; // "to playback your voice. and"
const uint8_t TRK_INST_MOLE    = 48; // "Press the buttons as fast as possible when they light up"
const uint8_t TRK_COIN         = 49; // Mario coin sound
const uint8_t TRK_INST_EXIT    = 50; // "to exit."

// ── EEPROM addresses ──────────────────────────────────────────────────────────
const int EEPROM_SIMON_HS  = 0; // 1 byte:  Simon high score (level)
const int EEPROM_MOLE_HS   = 2; // 2 bytes: Mole high score (hits), little-endian

// ── Other ────────────────────────────────────────────────────────────────────
const int SIMON_SHOW_MS    = 800;
const int SIMON_GAP_MS     = 150;
const int MOLE_DURATION_MS = 30000;
const int MOLE_TIMEOUT_MS  = 3000;
const int SIMON_MAX_LEN    = 100;
const uint8_t DF_VOLUME    = 29;
int currentMode = 0;

void setup() {
  //Serial.begin(115200);
  delay(2000);
  
  for (int i = 0; i < 5; i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
  }
  pinMode(LED_BUILTIN, OUTPUT);   digitalWrite(LED_BUILTIN, LOW);
  //pinMode(ISD_REC_PIN,   OUTPUT); digitalWrite(ISD_REC_PIN,   LOW);
  //pinMode(ISD_PLAYE_PIN, OUTPUT); digitalWrite(ISD_PLAYE_PIN, LOW);
  pinMode(DF_BUSY_PIN, INPUT);
  pinMode(DATA_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(RELAY_DF, OUTPUT);  digitalWrite(RELAY_DF, LOW);
  pinMode(RELAY_ISD, OUTPUT); digitalWrite(RELAY_ISD, LOW);
  updateShiftRegister();
  
  dfSerial.begin(9600, SERIAL_8N1, 2, 1); //RX, TX
  delay(3000);
  if (!myDFPlayer.begin(dfSerial)) {
    //Serial.println("Unable to begin. Check wiring and SD card!");
  } else {
    //Serial.println("myDFPlayer OK!");
    myDFPlayer.volume(DF_VOLUME);
    myDFPlayer.EQ(DFPLAYER_EQ_NORMAL);
  }
  
  if (!EEPROM.begin(EEPROM_SIMON_HS+EEPROM_MOLE_HS)) {
    //Serial.println("Failed to initialize EEPROM");
    return;
  }

  delay(500);
  digitalWrite(RELAY_DF, HIGH);

  playInstructions();
  currentMode = waitForAnyButton(0xFFFFFFFF);
  allLeds(LOW);
  unsigned long seed = micros();
  randomSeed(seed);
}

void loop() { 
  switch (currentMode) {
    case 0: simonSays();     break;
    case 2: whackAMole();    break;
    case 4: recordPlayback(); break;
  }

  playInstructions();
  currentMode = waitForAnyButton(0xFFFFFFFF);
  allLeds(LOW);
}

// ══════════════════════════════════════════════════════════════════════════════
// DFPLAYER HELPERS
// ══════════════════════════════════════════════════════════════════════════════

// Start a track; return immediately (non-blocking)
void playTrack(uint8_t track) {
  myDFPlayer.playMp3Folder(track);
}

// Block until whatever is currently playing finishes
void waitForAudio() {
  delay(200);
  while (digitalRead(DF_BUSY_PIN) == LOW) delay(10);
}

// Play a track and block until BUSY pin goes HIGH (done playing)
void playTrackBlocking(uint8_t track) {
  myDFPlayer.playMp3Folder(track);
  delay(250); // give myDFPlayer time to assert BUSY LOW
  while (digitalRead(DF_BUSY_PIN) == LOW) delay(10);
}

// ── Number speech system ───────────────────────────────────────────────────────
//
// Returns the track number for a ones digit (0-9)
uint8_t onesTrack(uint8_t digit) {
  return digit + 1; // 0→1, 1→2, ..., 9→10
}

// Returns the track number for a teen (10-19)
uint8_t teenTrack(uint8_t n) {
  return n + 1; // 10→11, 11→12, ..., 19→20
}

// Returns the track number for a tens-place word (20,30,...,90)
uint8_t tensTrack(uint8_t tens) {
  // tens=2→21, tens=3→22, ..., tens=9→28
  return tens + 19;
}

void sayColor(char color) {
  if (color == 'r') playTrackBlocking(TRK_RED);
  if (color == 'y') playTrackBlocking(TRK_YELLOW);
  if (color == 'g') playTrackBlocking(TRK_GREEN);
  if (color == 'b') playTrackBlocking(TRK_BLUE);
  if (color == 'w') playTrackBlocking(TRK_WHITE);
}

// Say any number 0-99 as a sequence of blocking audio clips
void sayNumber(int n) {
  n = constrain(n, 0, 99);

  if (n < 10) {
    playTrackBlocking(onesTrack(n));
  } else if (n < 20) {
    playTrackBlocking(teenTrack(n));
  } else {
    uint8_t tens = n / 10;
    uint8_t ones = n % 10;
    playTrackBlocking(tensTrack(tens));
    if (ones > 0) {
      playTrackBlocking(onesTrack(ones));
    }
  }
}

// Say "Press the [color] button"
void sayPressButton(char color) {
  playTrackBlocking(TRK_PRESS_THE);
  sayColor(color);
  playTrackBlocking(TRK_BUTTON);
}

// Say "You got <n> points"
void sayScore(int score) {
  playTrackBlocking(TRK_YOU_GOT);
  sayNumber(score);
  playTrackBlocking(TRK_POINTS);
}

// ══════════════════════════════════════════════════════════════════════════════
// LED HELPERS
// ══════════════════════════════════════════════════════════════════════════════

void updateShiftRegister() {
  digitalWrite(LATCH_PIN, LOW);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, ledState);
  digitalWrite(LATCH_PIN, HIGH);
}

void allLeds(uint8_t state) {
  if (state == HIGH) ledState = ledState | 0b00011111 ; // Turn on first 5 bits
  else ledState = ledState & 0b11100000;
  updateShiftRegister();
}

void lightButton(int btn, int ms) {
  bitSet(ledState, btn); // Set the bit corresponding to the button (0-4)
  updateShiftRegister();
  delay(ms);
  bitClear(ledState, btn); // Clear it
  updateShiftRegister();
}

void flashAll(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    allLeds(HIGH); delay(onMs);
    allLeds(LOW);  delay(offMs);
  }
}

// ══════════════════════════════════════════════════════════════════════════════
// BUTTON HELPERS
// ══════════════════════════════════════════════════════════════════════════════

// Returns 0-4 on first press detected, -1 on timeout.
// Pass 0xFFFFFFFF to wait forever.
int waitForAnyButton(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (timeoutMs == 0xFFFFFFFF || (millis() - start < timeoutMs)) {
    for (int j = 0; j < 5; j++) {
      if (digitalRead(BTN_PINS[j]) == LOW) {
        delay(30);
        waitForRelease(BTN_PINS[j]);
        return j;
      }
    }
  }
  return -1;
}

// Returns true if a specific button is pressed within timeoutMs
bool waitForButton(int btn, unsigned long timeoutMs) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (digitalRead(BTN_PINS[btn]) == LOW) {
      delay(30);
      waitForRelease(BTN_PINS[btn]);
      return true;
    }
  }
  return false;
}

bool isHeld(int btn) {
  return digitalRead(BTN_PINS[btn]) == LOW;
}

void waitForRelease(uint8_t pin) {
  while (digitalRead(pin) == LOW) delay(10);
  delay(30);
}

// ══════════════════════════════════════════════════════════════════════════════
// SPEAK LONG PHRASES
// ══════════════════════════════════════════════════════════════════════════════

void playInstructions() {
  bitSet(ledState, 0); 
  updateShiftRegister();
  sayPressButton('r');
  playTrackBlocking(TRK_PLAY_SIMON);
  
  bitSet(ledState, 2); 
  updateShiftRegister();
  sayPressButton('g');
  playTrackBlocking(TRK_PLAY_MOLE);

  bitSet(ledState, 4); 
  updateShiftRegister();
  sayPressButton('w');
  playTrackBlocking(TRK_PLAY_VOICE);
}

void playCountdown() {
  // "Three!" 
  sayNumber(3);
  delay(100);

  // "Two!" 
  sayNumber(2);
  delay(100);

  // "One!"
  sayNumber(1);
  delay(100);

  // "Go!" — all LEDs
  allLeds(HIGH);
  playTrackBlocking(TRK_GO);
  allLeds(LOW);
}

// ══════════════════════════════════════════════════════════════════════════════
// SIMON SAYS
// ══════════════════════════════════════════════════════════════════════════════

void simonSays() {
  uint8_t sequence[SIMON_MAX_LEN];
  int level = 0;

  playTrackBlocking(TRK_INST_SIMON);
  playCountdown();
  delay(400);

  while (level < SIMON_MAX_LEN) {
    sequence[level] = random(0, 5);
    level++;

    // ── Playback phase ──────────────────────────────────────────────────────
    // LED on-time shrinks with level: 800ms → floor 400ms
    int showMs = max(400, SIMON_SHOW_MS - (level * 4));

    for (int i = 0; i < level; i++) {
      lightButton(sequence[i], showMs);
      delay(SIMON_GAP_MS);
    }

    // ── Input phase ─────────────────────────────────────────────────────────
    for (int i = 0; i < level; i++) {
      unsigned long inputTimeout = max(2000UL, 5000UL - (level * 50UL));
      int pressed = waitForAnyButton(inputTimeout);

      if (pressed == -1 || pressed != sequence[i]) {
        playTrackBlocking(TRK_FAIL);
        delay(200);
        flashAll(4, 80, 80);
        simonGameOver(level - 1);
        return;
      }

      lightButton(pressed, 200); // visual feedback
    }

    // Round passed
    delay(300);
    flashAll(1, 300, 300);
  }

  // Beat all 100 levels
  playTrackBlocking(TRK_HIGH_SCORE);
  flashAll(5, 150, 150);
  simonGameOver(SIMON_MAX_LEN);
}

void simonGameOver(int score) {
  uint8_t hs = EEPROM.read(EEPROM_SIMON_HS);

  // Announce "You reached level <n>"
  delay(300);
  sayScore(score);

  if ((uint8_t)score > hs) {
    EEPROM.write(EEPROM_SIMON_HS, (uint8_t)score);
    delay(300);
    playTrackBlocking(TRK_HIGH_SCORE);
    flashAll(5, 150, 150);
  }
}

// ══════════════════════════════════════════════════════════════════════════════
// WHACK-A-MOLE
// ══════════════════════════════════════════════════════════════════════════════

void whackAMole() {
  int count   = 0;
  int lastBtn = -1;

  playTrackBlocking(TRK_INST_MOLE);
  playCountdown();
  delay(400);

  unsigned long roundStart = millis();

  while (millis() - roundStart < (unsigned long)MOLE_DURATION_MS) {
    // Random button, no consecutive repeats
    int btn;
    do { btn = random(0, 5); } while (btn == lastBtn);
    lastBtn = btn;

    // Mole window shrinks with score (floor 600ms), capped by remaining time
    unsigned long moleTimeout = max(600UL, (unsigned long)MOLE_TIMEOUT_MS - (count * 50UL));
    unsigned long remaining   = (unsigned long)MOLE_DURATION_MS - (millis() - roundStart);
    if (remaining < 150) break;
    moleTimeout = min(moleTimeout, remaining);

    // Light the mole LED and announce "Press button X" non-blocking
    bitSet(ledState, btn); 
    updateShiftRegister();

    bool hit = waitForButton(btn, moleTimeout);
    bitClear(ledState, btn); 
    updateShiftRegister();

    if (hit) {
      count++;
      playTrack(TRK_COIN);
    }
    // Missed mole: silently move on
  }

  allLeds(LOW);
  delay(500);
  myDFPlayer.stop();
  moleGameOver(count);
}

void moleGameOver(int count) {
  int hs = (int)EEPROM.read(EEPROM_MOLE_HS) | ((int)EEPROM.read(EEPROM_MOLE_HS + 1) << 8);
  bool newHS = (count > hs && count > 0);

  if (newHS) {
    EEPROM.write(EEPROM_MOLE_HS,     (uint8_t)(count & 0xFF));
    EEPROM.write(EEPROM_MOLE_HS + 1, (uint8_t)((count >> 8) & 0xFF));
  }

  delay(300);
  if (count == 0) {
    playTrackBlocking(TRK_FAIL);
    flashAll(3, 100, 100);
  } else {
    sayScore(count); // "You got <n> points"
  }

  if (newHS) {
    delay(300);
    playTrackBlocking(TRK_HIGH_SCORE);
    flashAll(5, 150, 150);
  }
}

// ══════════════════════════════════════════════════════════════════════════════
// ISD1820 HELPERS
// ══════════════════════════════════════════════════════════════════════════════

void isdPlayOnce() {
  //digitalWrite(ISD_PLAYE_PIN, HIGH);
  bitSet(ledState, ISD_PLAYE_PIN); 
  updateShiftRegister();
  delay(400);
  //digitalWrite(ISD_PLAYE_PIN, LOW);
  bitClear(ledState, ISD_PLAYE_PIN); 
  updateShiftRegister();
}

// ══════════════════════════════════════════════════════════════════════════════
// ISD1820 RECORD / PLAYBACK MODE
//
//   Button 0  HOLD  → record while held; auto-plays back on release
//   Button 2  HOLD  → return to menu
//   Button 4  TAP   → toggle looping playback (LED 4 lit while looping)
// ══════════════════════════════════════════════════════════════════════════════

void recordPlayback() {
  playTrackBlocking(TRK_INST_HOLD);
  bitSet(ledState, 0); 
  updateShiftRegister();
  sayColor('r');
  bitClear(ledState, 0); 
  updateShiftRegister();
  playTrackBlocking(TRK_BUTTON);

  playTrackBlocking(TRK_INST_REC);
  bitSet(ledState, 4); 
  updateShiftRegister();
  sayPressButton('w');
  bitClear(ledState, 4); 
  updateShiftRegister();
  playTrackBlocking(TRK_INST_PLAY);

  playTrackBlocking(TRK_INST_HOLD);
  bitSet(ledState, 2); 
  updateShiftRegister();
  sayColor('g');
  bitClear(ledState, 2); 
  updateShiftRegister();
  playTrackBlocking(TRK_BUTTON);
  playTrackBlocking(TRK_INST_EXIT);

  digitalWrite(RELAY_DF, LOW);
  delay(200);
  digitalWrite(RELAY_ISD, HIGH);

  while (true) {

    // ── Button 0 held → RECORD ──────────────────────────────────────────────
    if (isHeld(0)) {
      delay(30); // debounce
      
      //digitalWrite(ISD_REC_PIN, HIGH);
      bitSet(ledState, ISD_REC_PIN);
      bitSet(ledState, 0); // recording indicator
      updateShiftRegister();

      unsigned long recStart = millis();
      while (isHeld(0) && (millis() - recStart < 10000)) delay(5); // hold to record, hard stop at 10s

      //digitalWrite(ISD_REC_PIN, LOW);
      bitClear(ledState, ISD_REC_PIN);
      bitClear(ledState, 0);
      updateShiftRegister();

      unsigned long recTime = millis() - recStart;
      if (recTime > 100) {
        delay(100);
        isdPlayOnce(); // auto-play immediately after release
        lightButton(4, 200);
        delay(recTime / 4 + 500); // delay so people hop off the button
      }
      continue;
    }

    // ── Button 4 tap → ONE-SHOT PLAYBACK ────────────────────────────────────
    if (isHeld(4)) {
      delay(30);
      waitForRelease(BTN_PINS[4]);
      delay(100);
      isdPlayOnce();
      lightButton(4, 200);
      delay(500);
      continue;
    }

    // ── Buttons 2 held for 1s → EXIT ────────────────────
    if (isHeld(2)) {
      unsigned long exitStart = millis();
      bool cancelled = false;

      bitSet(ledState, 2); 
      updateShiftRegister();

      while (millis() - exitStart < 1000) {
        if (!isHeld(2)) { cancelled = true; allLeds(LOW); break; }
        delay(10);
      }
      if (!cancelled) {
        allLeds(LOW);
        digitalWrite(RELAY_DF, HIGH);
        delay(200);
        digitalWrite(RELAY_ISD, LOW);
        return; // back to loop()
      }
    }

    delay(5);
  }
}
