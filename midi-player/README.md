# Reproductor MIDI con buzzer, tira LED y OLED (ESP32)

Toca una melodía en un buzzer pasivo, ilumina una tira WS2812B al ritmo de las notas y
muestra en un OLED el título, el artista, la nota actual y una barra de progreso.

## Simular en Wokwi

1. Entra a <https://wokwi.com> → **New Project** → **ESP32**.
2. Reemplaza el contenido de `sketch.ino` y `diagram.json` con los de esta carpeta.
3. Botón **▾** junto a las pestañas → **New file…** → `song.h`, y pega su contenido.
4. **Library Manager** → agrega `FastLED`, `Adafruit SSD1306` y `Adafruit GFX Library`
   (o crea `libraries.txt` con el de esta carpeta).
5. ▶ Play. Para oír el buzzer, dale clic al icono de bocina del simulador.

## Conexiones

| Componente | Pin del componente | ESP32 |
|---|---|---|
| Buzzer pasivo | + (pin 2) | GPIO 25 |
| Buzzer pasivo | − (pin 1) | GND |
| Tira WS2812B (16 LEDs) | DIN | GPIO 13 |
| Tira WS2812B | VCC | 5V (VIN) |
| Tira WS2812B | GND | GND |
| OLED SSD1306 128×64 (I2C, 0x3C) | SDA | GPIO 21 |
| OLED SSD1306 | SCL | GPIO 22 |
| OLED SSD1306 | VCC | 3V3 |
| OLED SSD1306 | GND | GND |

Todas las tierras (GND) van juntas.

### En el hardware real, además

- Resistencia de **330 Ω** en serie entre GPIO 13 y DIN de la tira.
- Capacitor de **1000 µF** entre 5V y GND de la tira.
- Con más de ~15 LEDs, aliméntalos con una fuente de 5V aparte (y une su GND con el del ESP32).
- Un buzzer piezo pequeño va directo al GPIO; uno magnético necesita un transistor (2N2222 + 1 kΩ en la base).

## Poner tu propia canción

Un buzzer solo toca **una nota a la vez**. Si le pasas un MIDI completo (batería, bajo,
acordes, voz) suena horrible. `tools/midi_to_song.py` lo limpia:

```bash
pip install mido
python tools/midi_to_song.py cancion.mid --list            # ver qué pistas trae
python tools/midi_to_song.py cancion.mid --track 2 \
    --title "Mi cancion" --artist "Artista" -o song.h
```

Opciones útiles: `--start 45 --seconds 30` (solo el coro), `--transpose 12` (subir una octava),
`--min-ms 90` (quitar más notas basura), `--channel 1`.

### Si solo tienes el MP3

1. Separa la voz del resto con [Demucs](https://github.com/adefossez/demucs):
   `pip install demucs && demucs --two-stems=vocals cancion.mp3`
2. Convierte **solo la voz** (`vocals.wav`) a MIDI con
   [Basic Pitch](https://basicpitch.spotify.com) (web) o `pip install basic-pitch`.
3. Pasa ese MIDI por `tools/midi_to_song.py` (prueba `--min-ms 90` o más).

> Este repo es público: no subas canciones con copyright (`.mp3`, `.mid` ni el `song.h`
> generado). El `song.h` incluido es el *Himno a la alegría* (dominio público).
