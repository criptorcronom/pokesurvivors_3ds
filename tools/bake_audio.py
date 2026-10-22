#!/usr/bin/env python3
"""
PokéSurvivor 3DS - Audio Baking & WAV Generator Tool
Pre-renders clean 16-bit PCM WAV assets (32000 Hz) for Nintendo 3DS NDSP playback.
Converts real-time Web Audio API synthesis into passive RomFS assets.
"""

import os
import wave
import struct
import math

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
AUDIO_OUT_DIR = os.path.join(PROJECT_ROOT, "assets", "audio")

SAMPLE_RATE = 32000

def generate_tone(filename, duration, start_freq, end_freq, volume=0.5, channels=1):
    os.makedirs(AUDIO_OUT_DIR, exist_ok=True)
    filepath = os.path.join(AUDIO_OUT_DIR, filename)
    
    num_samples = int(duration * SAMPLE_RATE)
    with wave.open(filepath, "w") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(2) # 16-bit PCM
        wav.setframerate(SAMPLE_RATE)
        
        frames = bytearray()
        for i in range(num_samples):
            t = float(i) / SAMPLE_RATE
            progress = t / duration
            # Linear frequency sweep (chirp)
            freq = start_freq + (end_freq - start_freq) * progress
            # Exponential volume decay envelope
            env = math.exp(-3.5 * progress) * volume
            
            sample_val = int(32767.0 * env * math.sin(2.0 * math.pi * freq * t))
            sample_val = max(-32768, min(32767, sample_val))
            
            frame_bytes = struct.pack("<h", sample_val)
            for _ in range(channels):
                frames.extend(frame_bytes)
                
        wav.writeframes(frames)
    print(f"[+] Baked: {filename} ({duration}s, {start_freq}->{end_freq}Hz, {channels}ch)")

def generate_music_loop(filename, duration=8.0):
    os.makedirs(AUDIO_OUT_DIR, exist_ok=True)
    filepath = os.path.join(AUDIO_OUT_DIR, filename)
    
    num_samples = int(duration * SAMPLE_RATE)
    # Simple pleasant pentatonic chiptune progression
    notes = [261.63, 293.66, 329.63, 392.00, 440.00, 523.25]
    
    with wave.open(filepath, "w") as wav:
        wav.setnchannels(2) # Stereo
        wav.setsampwidth(2) # 16-bit PCM
        wav.setframerate(SAMPLE_RATE)
        
        frames = bytearray()
        for i in range(num_samples):
            t = float(i) / SAMPLE_RATE
            note_idx = int((t * 2.0)) % len(notes)
            bass_idx = int((t * 0.5)) % len(notes)
            
            lead_freq = notes[note_idx]
            bass_freq = notes[bass_idx] * 0.5
            
            lead = 0.25 * math.sin(2.0 * math.pi * lead_freq * t)
            bass = 0.20 * math.sin(2.0 * math.pi * bass_freq * t)
            
            left_val = int(32767.0 * (lead * 0.7 + bass * 0.8))
            right_val = int(32767.0 * (lead * 0.8 + bass * 0.7))
            
            frames.extend(struct.pack("<hh", left_val, right_val))
            
        wav.writeframes(frames)
    print(f"[+] Baked BGM: {filename} ({duration}s Stereo)")

def main():
    print("[*] Pre-rendering PokéSurvivor 3DS Audio Assets...")
    
    # SFX Clips
    generate_tone("hit.wav", 0.15, 220.0, 80.0, volume=0.8, channels=1)
    generate_tone("coin.wav", 0.20, 987.7, 1318.5, volume=0.6, channels=1)
    generate_tone("xp.wav", 0.12, 587.3, 880.0, volume=0.5, channels=1)
    generate_tone("levelup.wav", 0.65, 440.0, 880.0, volume=0.7, channels=2)
    generate_tone("faint.wav", 0.45, 300.0, 60.0, volume=0.7, channels=1)
    
    # BGM Loop
    generate_music_loop("biome_0.wav", duration=6.0)
    
    print("[+] Audio baking complete. Output saved to assets/audio/")

if __name__ == "__main__":
    main()
