// Canción de ejemplo (Himno a la alegría, Beethoven — dominio público).
// Reemplaza este archivo con el que genera tools/midi_to_song.py a partir de tu .mid.
#pragma once
#include <stddef.h>
#include <stdint.h>

// note: número MIDI (60 = Do central), 0 = silencio.
struct SongNote {
  uint8_t note;
  uint8_t velocity;
  uint16_t durationMs;
};

const char SONG_TITLE[] = "Himno a la alegria";
const char SONG_ARTIST[] = "L. v. Beethoven";

const SongNote SONG_NOTES[] = {
  {76, 100, 500}, {76, 90, 500}, {77, 95, 500}, {79, 110, 500},
  {79, 110, 500}, {77, 95, 500}, {76, 90, 500}, {74, 85, 500},
  {72, 90, 500}, {72, 85, 500}, {74, 95, 500}, {76, 105, 500},
  {76, 110, 750}, {74, 90, 250}, {74, 100, 1000},
  {76, 100, 500}, {76, 90, 500}, {77, 95, 500}, {79, 110, 500},
  {79, 110, 500}, {77, 95, 500}, {76, 90, 500}, {74, 85, 500},
  {72, 90, 500}, {72, 85, 500}, {74, 95, 500}, {76, 105, 500},
  {74, 105, 750}, {72, 90, 250}, {72, 110, 1000},
};

const size_t SONG_LENGTH = sizeof(SONG_NOTES) / sizeof(SONG_NOTES[0]);
