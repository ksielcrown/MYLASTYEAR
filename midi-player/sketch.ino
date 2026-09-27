// Reproductor de melodías para ESP32: buzzer + tira WS2812B + OLED SSD1306.
//
// - Núcleo 0: tarea de audio con tiempos absolutos (vTaskDelayUntil, sin deriva).
// - Núcleo 1 (loop): LEDs a ~60 FPS y pantalla a ~10 FPS.
// La tarea de audio publica la nota actual en variables atómicas que lee loop().

#include <Arduino.h>
#include <Wire.h>
#include <atomic>
#include <FastLED.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "song.h"

// ---------- Pines ----------
constexpr uint8_t PIN_BUZZER = 25;
constexpr uint8_t PIN_LEDS = 13;
constexpr uint8_t PIN_SDA = 21;
constexpr uint8_t PIN_SCL = 22;

// ---------- Configuración ----------
constexpr uint16_t NUM_LEDS = 16;
constexpr uint8_t LED_BRIGHTNESS = 120;
constexpr uint8_t SCREEN_W = 128;
constexpr uint8_t SCREEN_H = 64;
constexpr uint8_t OLED_ADDR = 0x3C;
constexpr uint8_t ARTICULATION_PCT = 90;   // % de la duración que suena cada nota
constexpr uint32_t PAUSE_BETWEEN_LOOPS_MS = 2000;

// ---------- Estado compartido entre núcleos ----------
std::atomic<uint8_t> g_note{0};          // nota que suena ahora (0 = silencio)
std::atomic<uint8_t> g_velocity{0};
std::atomic<uint32_t> g_noteSeq{0};      // se incrementa en cada nota nueva
std::atomic<int32_t> g_index{-1};        // índice en SONG_NOTES
std::atomic<uint32_t> g_songPosMs{0};    // posición al inicio de la nota actual
std::atomic<uint32_t> g_noteStartMs{0};  // millis() al inicio de la nota actual
std::atomic<bool> g_playing{false};

CRGB leds[NUM_LEDS];
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

uint32_t g_totalMs = 0;
uint8_t g_lowNote = 127;
uint8_t g_highNote = 0;

// ---------- Utilidades ----------
float midiToFreq(uint8_t note) {
  return 440.0f * powf(2.0f, (static_cast<int>(note) - 69) / 12.0f);
}

void noteName(uint8_t note, char* out, size_t len) {
  static const char* const NAMES[12] = {"C", "C#", "D", "D#", "E", "F",
                                        "F#", "G", "G#", "A", "A#", "B"};
  if (note == 0) {
    snprintf(out, len, "--");
    return;
  }
  snprintf(out, len, "%s%d", NAMES[note % 12], note / 12 - 1);
}

void formatTime(uint32_t ms, char* out, size_t len) {
  uint32_t s = ms / 1000;
  snprintf(out, len, "%lu:%02lu", static_cast<unsigned long>(s / 60),
           static_cast<unsigned long>(s % 60));
}

void analyzeSong() {
  for (size_t i = 0; i < SONG_LENGTH; ++i) {
    g_totalMs += SONG_NOTES[i].durationMs;
    uint8_t n = SONG_NOTES[i].note;
    if (n) {
      g_lowNote = min(g_lowNote, n);
      g_highNote = max(g_highNote, n);
    }
  }
  if (g_lowNote >= g_highNote) {  // canción de una sola nota o vacía
    g_lowNote = 60;
    g_highNote = 84;
  }
}

// ---------- Audio (núcleo 0) ----------
void audioTask(void*) {
  for (;;) {
    g_playing = true;
    uint32_t pos = 0;
    TickType_t wake = xTaskGetTickCount();

    for (size_t i = 0; i < SONG_LENGTH; ++i) {
      const SongNote& n = SONG_NOTES[i];
      const uint32_t onMs = n.note ? n.durationMs * ARTICULATION_PCT / 100 : n.durationMs;

      g_index = static_cast<int32_t>(i);
      g_songPosMs = pos;
      g_noteStartMs = millis();
      g_velocity = n.velocity;
      g_note = n.note;
      g_noteSeq.fetch_add(1);

      if (n.note) {
        tone(PIN_BUZZER, static_cast<unsigned int>(midiToFreq(n.note) + 0.5f));
      } else {
        noTone(PIN_BUZZER);
      }
      vTaskDelayUntil(&wake, pdMS_TO_TICKS(onMs));

      if (onMs < n.durationMs) {  // pequeño hueco para que se distingan notas repetidas
        noTone(PIN_BUZZER);
        g_note = 0;
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(n.durationMs - onMs));
      }
      pos += n.durationMs;
    }

    noTone(PIN_BUZZER);
    g_note = 0;
    g_playing = false;
    g_songPosMs = g_totalMs;
    vTaskDelay(pdMS_TO_TICKS(PAUSE_BETWEEN_LOOPS_MS));
  }
}

// ---------- LEDs ----------
// Color = nota (Do rojo ... Si morado), posición = qué tan aguda es,
// ancho del destello = velocity. Luego se desvanece.
void updateLeds() {
  static uint32_t lastSeq = 0;
  fadeToBlackBy(leds, NUM_LEDS, 30);

  const uint8_t note = g_note;
  if (note) {
    const uint8_t hue = (note % 12) * 21;
    const int center = map(constrain(note, g_lowNote, g_highNote), g_lowNote, g_highNote,
                           0, NUM_LEDS - 1);
    const uint32_t seq = g_noteSeq;

    if (seq != lastSeq) {  // nota nueva: destello
      lastSeq = seq;
      const int span = map(g_velocity, 1, 127, 1, NUM_LEDS / 3);
      for (int d = -span; d <= span; ++d) {
        const int p = center + d;
        if (p < 0 || p >= NUM_LEDS) continue;
        const uint8_t v = 255 - abs(d) * 200 / (span + 1);
        leds[p] = CHSV(hue, 255, v);
      }
    }
    leds[center] = CHSV(hue, 200, 255);  // mientras suena, el centro sigue encendido
  }
  FastLED.show();
}

// ---------- Pantalla ----------
void updateDisplay() {
  static int16_t scrollX = 0;
  char buf[24];

  display.clearDisplay();
  display.setTextWrap(false);
  display.setTextColor(SSD1306_WHITE);

  // Título (tamaño 2, con scroll si no cabe)
  display.setTextSize(2);
  const int16_t titleW = static_cast<int16_t>(strlen(SONG_TITLE)) * 12;
  if (titleW <= SCREEN_W) {
    display.setCursor((SCREEN_W - titleW) / 2, 0);
    display.print(SONG_TITLE);
  } else {
    const int16_t gap = 36;
    display.setCursor(-scrollX, 0);
    display.print(SONG_TITLE);
    display.setCursor(-scrollX + titleW + gap, 0);
    display.print(SONG_TITLE);
    scrollX = (scrollX + 3) % (titleW + gap);
  }

  // Artista
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print(SONG_ARTIST);

  // Nota actual
  display.setCursor(0, 34);
  if (g_playing) {
    noteName(g_note, buf, sizeof(buf));
    display.print("Nota: ");
    display.print(buf);
  } else {
    display.print("Repitiendo...");
  }

  // Progreso
  uint32_t elapsed = g_songPosMs;
  if (g_playing) {
    const int32_t idx = g_index;
    const uint32_t inNote = millis() - g_noteStartMs;
    const uint32_t dur = idx >= 0 ? SONG_NOTES[idx].durationMs : 0;
    elapsed += min(inNote, dur);
  }
  elapsed = min(elapsed, g_totalMs);

  display.drawRect(0, 46, SCREEN_W, 6, SSD1306_WHITE);
  if (g_totalMs) {
    display.fillRect(1, 47, (SCREEN_W - 2) * elapsed / g_totalMs, 4, SSD1306_WHITE);
  }

  formatTime(elapsed, buf, sizeof(buf));
  display.setCursor(0, 56);
  display.print(buf);
  formatTime(g_totalMs, buf, sizeof(buf));
  display.setCursor(SCREEN_W - static_cast<int16_t>(strlen(buf)) * 6, 56);
  display.print(buf);

  display.display();
}

// ---------- Arduino ----------
void setup() {
  Serial.begin(115200);
  analyzeSong();

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("No se encontro la pantalla SSD1306");
  }

  FastLED.addLeds<WS2812B, PIN_LEDS, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.clear(true);

  Serial.printf("Reproduciendo \"%s\" - %s (%u notas, %lu ms)\n", SONG_TITLE, SONG_ARTIST,
                static_cast<unsigned>(SONG_LENGTH), static_cast<unsigned long>(g_totalMs));

  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 2, nullptr, 0);
}

void loop() {
  static uint32_t lastLeds = 0;
  static uint32_t lastDisplay = 0;
  const uint32_t now = millis();

  if (now - lastLeds >= 16) {
    lastLeds = now;
    updateLeds();
  }
  if (now - lastDisplay >= 100) {
    lastDisplay = now;
    updateDisplay();
  }
  delay(1);
}
