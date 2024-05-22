#define FASTLED_INTERNAL

#include <Wire.h>
#include <I2CKeyPad8x8.h>
#include <FastLED.h>

#define KEYPAD_ROWS 8
#define KEYPAD_COLUMNS 8
#define KEYPADS_ROWS 2
#define KEYPADS_COLUMNS 2
#define VIRTUAL_MARGIN 8

#define LED_PIN 2
#define INTERRUPT_PIN 10

#define PLAYERS 3

const unsigned int visibleRows = KEYPAD_ROWS * KEYPADS_ROWS;
const unsigned int visibleColumns = KEYPAD_COLUMNS * KEYPADS_COLUMNS;
const unsigned int virtualRows = visibleRows + VIRTUAL_MARGIN * 2;
const unsigned int virtualColumns = visibleColumns + VIRTUAL_MARGIN * 2;
const unsigned int numLeds = visibleRows * visibleColumns;

I2CKeyPad8x8 keyPads[] = { I2CKeyPad8x8(0x20), I2CKeyPad8x8(0x21), I2CKeyPad8x8(0x23), I2CKeyPad8x8(0x22) };
unsigned int keyPadsLength = sizeof(keyPads) / sizeof(I2CKeyPad8x8);

unsigned long lastMillis = 0;
bool isPressed = false;
unsigned long lastPressedMillis = 0;
bool isPlaying = false;
unsigned long lastPlayingMillis = 0;
bool isSelectingPlayer = false;
unsigned long lastSelectingPlayerMillis = 0;
unsigned int playingPlayer = 0;
bool keepSelectingPlayer = true;
bool clearFilledBoxOn = true;

bool gens[PLAYERS][virtualRows][virtualColumns];
bool nextGen[virtualRows][virtualColumns];

bool ignoreKeypads = false;

CRGB leds[numLeds];
CRGB sourceLeds[numLeds];
CRGB targetLeds[numLeds];

fract8 loopBeat;
fract8 lastLoopBeat;

void setup() {
  Wire.begin();
  Wire.setClock(400000);

  for (unsigned int index = 0; index < keyPadsLength; index++) {
    if (!keyPads[index].begin()) {
      ignoreKeypads = true;

      break;
    }
  }

  memset8(leds, 0, sizeof(CRGB) * numLeds);
  memset8(sourceLeds, 0, sizeof(CRGB) * numLeds);
  memset8(targetLeds, 0, sizeof(CRGB) * numLeds);

  clearGens();

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, numLeds).setCorrection(TypicalLEDStrip);

  FastLED.setMaxPowerInVoltsAndMilliamps(5, 6000);
  //FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);

  pinMode(INTERRUPT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), pressed, FALLING);

  randomSeed(analogRead(0));
}

void clearGens() {
  memset8(gens, false, sizeof(bool) * (PLAYERS * virtualRows * virtualColumns));
}

bool getKey(unsigned int &row, unsigned int &column) {
  for (unsigned int index = 0; index < keyPadsLength; index++) {
    I2CKeyPad8x8 keyPad = keyPads[index];

    if (keyPad.isPressed()) {
      unsigned char key = keyPad.getKey();

      if (millis() - lastPressedMillis >= 250 || keyPad.getLastKey() != key) {
        row = (KEYPAD_ROWS - 1) - (key % 8) % KEYPAD_ROWS;
        column = (KEYPAD_COLUMNS - 1) - (key / 8) % KEYPAD_COLUMNS;

        row += index / 2 * 8;
        column += index % 2 * 8;

        lastPressedMillis = millis();

        return true;
      }
    }
  }

  return false;
}

bool testSelectingPlayer(unsigned int row, unsigned int column, unsigned int x, unsigned int y, unsigned int w, unsigned int h) {
  x += VIRTUAL_MARGIN;
  y += VIRTUAL_MARGIN;

  return column >= x && column < (x + w) && row >= y && row < (y + h);
}

bool testCurrentPlayer(unsigned int row, unsigned int column) {
  for (unsigned int player = 0; player < PLAYERS; player++) {
    if (testSelectingPlayer(row, column, player * 4 + player + 1, 1, 4, 4)) {
      playingPlayer = player;

      memset8(gens[player], false, sizeof(bool) * (virtualRows * virtualColumns));

      isPlaying = true;

      lastPlayingMillis = millis();

      return true;
    }
  }

  return false;
}

bool testRandomPlayer(unsigned int row, unsigned int column) {
  for (unsigned int player = 0; player < PLAYERS; player++) {
    if (testSelectingPlayer(row, column, player * 4 + player + 1, 6, 4, 4)) {
      for (unsigned int row = VIRTUAL_MARGIN; row < visibleRows + VIRTUAL_MARGIN; row++) {
        for (unsigned int column = VIRTUAL_MARGIN; column < visibleColumns + VIRTUAL_MARGIN; column++) {
          gens[player][row][column] = random(2);
        }
      }

      return true;
    }
  }

  return false;
}

void testKeys() {
  if (isPressed) {
    unsigned int row, column;

    if (getKey(row, column)) {
      isPressed = false;

      row += VIRTUAL_MARGIN;
      column += VIRTUAL_MARGIN;

      if (keepSelectingPlayer || isSelectingPlayer) {
        if (!testCurrentPlayer(row, column)) {
          if (!testRandomPlayer(row, column)) {
            if (testSelectingPlayer(row, column, 1, 11, 9, 4)) {
              for (unsigned int row = VIRTUAL_MARGIN; row < visibleRows + VIRTUAL_MARGIN; row++) {
                for (unsigned int column = VIRTUAL_MARGIN; column < visibleColumns + VIRTUAL_MARGIN; column++) {
                  for (unsigned int player = 0; player < PLAYERS; player++) {
                    gens[player][row][column] = random(2);
                  }
                }
              }
            } else if (testSelectingPlayer(row, column, 11, 11, 4, 4)) {
              clearGens();
            }
          }
        }

        isSelectingPlayer = false;
        keepSelectingPlayer = false;
      } else if (isPlaying) {
        gens[playingPlayer][row][column] = !gens[playingPlayer][row][column];

        lastPlayingMillis = millis();
      } else {
        clearFilledBoxOn = true;
        isSelectingPlayer = true;

        lastSelectingPlayerMillis = millis();
      }
    }
  }

  if (!keepSelectingPlayer && isSelectingPlayer && millis() - lastSelectingPlayerMillis >= 3000) {
    isSelectingPlayer = false;
  }

  if (isPlaying && millis() - lastPlayingMillis >= 7000) {
    isPlaying = false;
  }
}

void calculateNextGen() {
  for (unsigned int player = 0; player < PLAYERS; player++) {
    for (unsigned int row = 0; row < virtualRows; row++) {
      for (unsigned int column = 0; column < virtualColumns; column++) {
        unsigned int liveNeighbors = 0;

        for (int x_neighbour = -1; x_neighbour <= 1; x_neighbour++) {
          for (int y_neighbour = -1; y_neighbour <= 1; y_neighbour++) {
            if (x_neighbour == 0 && y_neighbour == 0) {
              continue;
            }

            int x = row + x_neighbour;
            int y = column + y_neighbour;

            if (x >= 0 && x < virtualRows && y >= 0 && y < virtualColumns && gens[player][x][y]) {
              liveNeighbors++;
            }
          }
        }

        if (gens[player][row][column]) {
          nextGen[row][column] = liveNeighbors == 2 || liveNeighbors == 3;
        } else {
          nextGen[row][column] = liveNeighbors == 3;
        }
      }
    }

    memcpy8(gens[player], nextGen, sizeof(bool) * (virtualRows * virtualColumns));
  }
}

CRGB playerColor(unsigned int player) {
  switch (player) {
    case 0:
      return CRGB::Red;
    case 1:
      return CRGB::Green;
    case 2:
      return CRGB::Blue;
    case 3:
      return CRGB::Red + CRGB::Green;
    case 4:
      return CRGB::Red + CRGB::Blue;
    case 5:
      return CRGB::Green + CRGB::Blue;
  }

  return CRGB::Red + CRGB::Green + CRGB::Blue;
}

unsigned int calculateIndex(unsigned int row, unsigned int column) {
  return (numLeds - 1) - ((row % 2) == 0 ? (row + 1) * visibleColumns - column - 1 : row * visibleColumns + column);
}

void createFilledBox(unsigned int x, unsigned int y, unsigned int w, unsigned int h, int player = -1, bool isRandom = true) {
  for (int row = y; row < y + h && row < visibleRows; row++) {
    for (int column = x; column < x + w && column < visibleColumns; column++) {
      unsigned int index = calculateIndex(row, column);

      targetLeds[index] = playerColor(player < 0 ? random(0, 7) : player);

      if (isRandom) {
        targetLeds[index].fadeToBlackBy(random(0x00, 0xFF));
      }
    }
  }
}

void createClearBox(unsigned int x, unsigned int y, unsigned int w, unsigned int h) {
  for (int row = y; row < y + h && row < visibleRows; row++) {
    for (int column = x; column < x + w && column < visibleColumns; column++) {
      unsigned int index = calculateIndex(row, column);

      targetLeds[index] = clearFilledBoxOn ? CRGB::White : CRGB::Black;
    }
  }

  clearFilledBoxOn = !clearFilledBoxOn;
}

void createMenu() {
  for (unsigned int player = 0; player < PLAYERS; player++) {
    createFilledBox(player * 4 + player + 1, 1, 4, 4, player, false);
  }

  for (unsigned int player = 0; player < PLAYERS; player++) {
    createFilledBox(player * 4 + player + 1, 6, 4, 4, player);
  }

  createFilledBox(1, 11, 9, 4);

  createClearBox(11, 11, 4, 4);
}

void combineGensLeds() {
  memcpy8(sourceLeds, targetLeds, sizeof(CRGB) * numLeds);
  memset8(targetLeds, 0, sizeof(CRGB) * numLeds);

  if (keepSelectingPlayer || isSelectingPlayer) {
    createMenu();
  } else {
    for (unsigned int row = 0; row < visibleRows; row++) {
      for (unsigned int column = 0; column < visibleColumns; column++) {
        unsigned int index = calculateIndex(row, column);

        if (isPlaying) {
          targetLeds[index] = playerColor(playingPlayer);

          if (!gens[playingPlayer][row + VIRTUAL_MARGIN][column + VIRTUAL_MARGIN]) {
            targetLeds[index].fadeToBlackBy(random(0xF0, 0xFF));
          }
        } else {
          for (unsigned int player = 0; player < PLAYERS; player++) {
            if (gens[player][row + VIRTUAL_MARGIN][column + VIRTUAL_MARGIN]) {
              targetLeds[index] += playerColor(player);
            }
          }
        }
      }
    }
  }
}

void loop() {
  if (!ignoreKeypads) {
    testKeys();
  }

  loopBeat = beat8(120);
  loopBeat = ease8InOutApprox(loopBeat);

  if (lastLoopBeat != loopBeat) {
    if (loopBeat < lastLoopBeat) {
      combineGensLeds();

      if (!keepSelectingPlayer && !isSelectingPlayer && !isPlaying) {
        calculateNextGen();
      }
    }

    lastLoopBeat = loopBeat;

    for (unsigned int index = 0; index < numLeds; index++) {
      CRGB color = targetLeds[index];

      leds[index] = color.lerp8(sourceLeds[index], 0xFF - loopBeat);
      leds[index] = sourceLeds[index].lerp8(color, loopBeat);
    }

    FastLED.show();
  }
}

void pressed() {
  isPressed = true;
}
