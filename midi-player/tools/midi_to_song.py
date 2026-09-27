#!/usr/bin/env python3
"""Convierte un .mid en song.h para el reproductor de buzzer del ESP32.

Un buzzer solo toca una nota a la vez, así que el script:
  1. Descarta la batería (canal 10), que en un buzzer suena a ruido.
  2. Opcionalmente se queda con una sola pista o canal (la melodía).
  3. Aplica "skyline": si suenan varias notas, toca la más aguda.
  4. Elimina notas cortísimas (típicas de MIDIs generados desde audio).
  5. Transpone por octavas para que quede en el rango donde el buzzer suena bien.

Uso:
  pip install mido
  python midi_to_song.py cancion.mid --title "Party" --artist "Bad Bunny" --list
  python midi_to_song.py cancion.mid --title "Party" --artist "Bad Bunny" --track 2 -o ../song.h
"""

import argparse
import bisect
import statistics
import sys
from pathlib import Path

import mido

DRUM_CHANNEL = 9  # canal 10 en numeración humana
MAX_DURATION_MS = 65535


def tempo_converter(mid):
    """Devuelve una función ticks absolutos -> segundos que respeta cambios de tempo."""
    changes = []
    for track in mid.tracks:
        tick = 0
        for msg in track:
            tick += msg.time
            if msg.type == "set_tempo":
                changes.append((tick, msg.tempo))
    changes.sort()
    if not changes or changes[0][0] != 0:
        changes.insert(0, (0, 500000))  # 120 BPM por defecto

    ticks, secs, tempos = [], [], []
    elapsed = 0.0
    for i, (tick, tempo) in enumerate(changes):
        if i:
            prev_tick, prev_tempo = changes[i - 1]
            elapsed += mido.tick2second(tick - prev_tick, mid.ticks_per_beat, prev_tempo)
        ticks.append(tick)
        secs.append(elapsed)
        tempos.append(tempo)

    def convert(tick):
        i = bisect.bisect_right(ticks, tick) - 1
        return secs[i] + mido.tick2second(tick - ticks[i], mid.ticks_per_beat, tempos[i])

    return convert


def list_tracks(mid):
    print(f"{len(mid.tracks)} pistas (tipo {mid.type}):")
    for i, track in enumerate(mid.tracks):
        notes = [m for m in track if m.type == "note_on" and m.velocity > 0]
        channels = sorted({m.channel + 1 for m in notes})
        if notes:
            pitches = [m.note for m in notes]
            info = f"{len(notes):5d} notas, canales {channels}, rango {min(pitches)}-{max(pitches)}"
        else:
            info = "sin notas"
        drums = "  (batería)" if channels == [DRUM_CHANNEL + 1] else ""
        print(f"  [{i}] {track.name!r:30s} {info}{drums}")


def collect_events(mid, track_idx, channel):
    to_sec = tempo_converter(mid)
    events = []  # (segundos, es_on, nota, velocity)
    for i, track in enumerate(mid.tracks):
        if track_idx is not None and i != track_idx:
            continue
        tick = 0
        for msg in track:
            tick += msg.time
            if msg.type not in ("note_on", "note_off") or msg.channel == DRUM_CHANNEL:
                continue
            if channel is not None and msg.channel != channel - 1:
                continue
            is_on = msg.type == "note_on" and msg.velocity > 0
            events.append((to_sec(tick), is_on, msg.note, msg.velocity))
    # Si coinciden en el tiempo, primero los apagados y luego los encendidos.
    events.sort(key=lambda e: (e[0], e[1]))
    return events


def skyline(events):
    """Convierte eventos polifónicos en segmentos monofónicos (nota, velocity, segundos)."""
    segments = []
    active = {}  # nota -> [conteo, velocity]
    cur_note, cur_vel, cur_start = 0, 0, None

    i = 0
    while i < len(events):
        t = events[i][0]
        retriggered = set()
        while i < len(events) and events[i][0] == t:
            _, is_on, note, vel = events[i]
            if is_on:
                entry = active.setdefault(note, [0, vel])
                entry[0] += 1
                entry[1] = vel
                retriggered.add(note)
            elif note in active:
                active[note][0] -= 1
                if active[note][0] <= 0:
                    del active[note]
            i += 1

        top = max(active) if active else 0
        if cur_start is None:
            if top:  # recorta el silencio inicial
                cur_note, cur_vel, cur_start = top, active[top][1], t
            continue
        if top != cur_note or top in retriggered:
            segments.append((cur_note, cur_vel, t - cur_start))
            cur_note = top
            cur_vel = active[top][1] if top else 0
            cur_start = t

    return segments


def clean(segments, min_ms, max_rest_ms):
    out = []
    for note, vel, dur in segments:
        ms = dur * 1000.0
        if ms < min_ms:
            if out:  # absorbe la notita en la anterior
                out[-1][2] += ms
            continue
        if out and note == 0 and out[-1][0] == 0:
            out[-1][2] += ms
            continue
        out.append([note, vel, ms])

    for seg in out:
        if seg[0] == 0:
            seg[2] = min(seg[2], max_rest_ms)
    while out and out[-1][0] == 0:
        out.pop()
    return out


def transpose(segments, target, forced):
    notes = [(n, d) for n, _, d in segments if n]
    if not notes:
        sys.exit("No quedaron notas. Prueba con otra --track o --channel (usa --list).")

    if forced is not None:
        shift = forced
    else:
        # Mediana ponderada por duración, redondeada a octavas completas.
        weighted = []
        for n, d in notes:
            weighted.extend([n] * max(1, int(d / 50)))
        shift = round((target - statistics.median(weighted)) / 12) * 12

    for seg in segments:
        if seg[0]:
            n = seg[0] + shift
            while n < 48:
                n += 12
            while n > 100:
                n -= 12
            seg[0] = n
    return shift


def trim(segments, start_s, max_s):
    out, pos = [], 0.0
    for note, vel, ms in segments:
        seg_start, seg_end = pos, pos + ms
        pos = seg_end
        if seg_end <= start_s * 1000:
            continue
        if max_s is not None and seg_start >= (start_s + max_s) * 1000:
            break
        lo = max(seg_start, start_s * 1000)
        hi = seg_end if max_s is None else min(seg_end, (start_s + max_s) * 1000)
        out.append([note, vel, hi - lo])
    while out and out[0][0] == 0:
        out.pop(0)
    return out


def c_string(text):
    return text.replace("\\", "\\\\").replace('"', '\\"')


def write_header(segments, title, artist, source, path):
    rows = []
    for note, vel, ms in segments:
        ms = int(round(ms))
        while ms > 0:  # durationMs es uint16_t
            chunk = min(ms, MAX_DURATION_MS)
            rows.append(f"{{{note}, {vel if note else 0}, {chunk}}}")
            ms -= chunk

    lines = [
        f"// Generado por tools/midi_to_song.py a partir de {source}",
        "#pragma once",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "// note: número MIDI (60 = Do central), 0 = silencio.",
        "struct SongNote {",
        "  uint8_t note;",
        "  uint8_t velocity;",
        "  uint16_t durationMs;",
        "};",
        "",
        f'const char SONG_TITLE[] = "{c_string(title)}";',
        f'const char SONG_ARTIST[] = "{c_string(artist)}";',
        "",
        "const SongNote SONG_NOTES[] = {",
    ]
    for i in range(0, len(rows), 6):
        lines.append("  " + ", ".join(rows[i:i + 6]) + ",")
    lines += [
        "};",
        "",
        "const size_t SONG_LENGTH = sizeof(SONG_NOTES) / sizeof(SONG_NOTES[0]);",
        "",
    ]
    Path(path).write_text("\n".join(lines), encoding="utf-8")
    return len(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("midi", help="archivo .mid de entrada")
    ap.add_argument("-o", "--output", default="song.h", help="archivo de salida (default: song.h)")
    ap.add_argument("--title", default="Sin titulo")
    ap.add_argument("--artist", default="")
    ap.add_argument("--list", action="store_true", help="muestra las pistas y termina")
    ap.add_argument("--track", type=int, help="usar solo esta pista (índice de --list)")
    ap.add_argument("--channel", type=int, help="usar solo este canal (1-16)")
    ap.add_argument("--min-ms", type=float, default=60, help="descarta notas más cortas (default: 60)")
    ap.add_argument("--max-rest-ms", type=float, default=1500, help="silencios más largos se recortan (default: 1500)")
    ap.add_argument("--target", type=int, default=79, help="nota MIDI central deseada (default: 79 = G5)")
    ap.add_argument("--transpose", type=int, help="semitonos fijos en vez de transponer automático")
    ap.add_argument("--start", type=float, default=0, help="segundo donde empieza el fragmento")
    ap.add_argument("--seconds", type=float, help="duración máxima del fragmento en segundos")
    args = ap.parse_args()

    mid = mido.MidiFile(args.midi)
    if args.list:
        list_tracks(mid)
        return

    events = collect_events(mid, args.track, args.channel)
    segments = clean(skyline(events), args.min_ms, args.max_rest_ms)
    segments = trim(segments, args.start, args.seconds)
    shift = transpose(segments, args.target, args.transpose)
    count = write_header(segments, args.title, args.artist, Path(args.midi).name, args.output)

    total_s = sum(ms for _, _, ms in segments) / 1000
    print(f"{count} notas, {total_s:.1f} s, transpuesto {shift:+d} semitonos -> {args.output}")
    # Cada SongNote ocupa 4 bytes en flash; un ESP32 tiene de sobra.
    print(f"Tamaño aproximado en flash: {count * 4 / 1024:.1f} KB")


if __name__ == "__main__":
    main()
