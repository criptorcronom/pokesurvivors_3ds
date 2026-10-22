#!/usr/bin/env python3
"""
PokéSurvivor 3DS - Texture Conversion Tool
Batch compiles Pokemon animation strips into PICA200 swizzled .t3x spritesheets
using devkitPro's tex3ds. Enforces the strict 1024x1024 dimension limit.
"""

import os
import sys
import subprocess
import shutil

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
ASSETS_DIR = os.path.join(PROJECT_ROOT, "pokesurvivor-main", "assets", "pokemon")
ROMFS_GFX_DIR = os.path.join(PROJECT_ROOT, "romfs", "gfx")

# Formats supported by tex3ds:
# rgba8888 (32bpp), rgba4444 (16bpp), rgba5551 (16bpp), rgb565 (16bpp), etc1a4 (8bpp)
TEXTURE_FORMAT = "rgba4444"

def get_tex3ds_path():
    devkitpro = os.environ.get("DEVKITPRO", "C:/devkitPro" if sys.platform == "win32" else "/opt/devkitpro")
    devkitarm = os.environ.get("DEVKITARM", os.path.join(devkitpro, "devkitARM"))
    tex3ds = os.path.join(devkitarm, "bin", "tex3ds.exe" if sys.platform == "win32" else "tex3ds")
    
    if os.path.isfile(tex3ds):
        return tex3ds
    which_tex3ds = shutil.which("tex3ds")
    if which_tex3ds:
        return which_tex3ds
    return None

def build_pokemon_sheet(tex3ds_bin, dex_id, output_path, is_shiny=False):
    poke_dir = os.path.join(ASSETS_DIR, str(dex_id))
    if is_shiny:
        poke_dir = os.path.join(poke_dir, "s")
    
    if not os.path.isdir(poke_dir):
        return False
    
    anim_files = ["Idle.png", "Walk.png", "Attack.png", "Hurt.png", "Faint.png"]
    input_pngs = []
    for anim in anim_files:
        png_path = os.path.join(poke_dir, anim)
        if os.path.isfile(png_path):
            input_pngs.append(png_path)
            
    if not input_pngs:
        return False

    cmd = [tex3ds_bin, "-o", output_path, "-f", TEXTURE_FORMAT, "-z", "auto"] + input_pngs
    print(f"[*] Compiling Dex #{dex_id} ({'Shiny' if is_shiny else 'Normal'}) -> {os.path.basename(output_path)}")
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[!] Error building {output_path}: {res.stderr}")
        return False
    return True

def main():
    tex3ds = get_tex3ds_path()
    if not tex3ds:
        print("[!] Error: tex3ds executable not found. Make sure devkitPro / devkitARM is installed.")
        sys.exit(1)

    os.makedirs(ROMFS_GFX_DIR, exist_ok=True)
    print(f"[+] Found tex3ds: {tex3ds}")
    print(f"[+] Output RomFS directory: {ROMFS_GFX_DIR}")

    # Build Player starter (Pikachu - Dex 25)
    player_out = os.path.join(ROMFS_GFX_DIR, "sheet_player.t3x")
    build_pokemon_sheet(tex3ds, 25, player_out)

    # Build initial wave biome sheet (Rattata, Pidgey, Caterpie)
    biome_0_out = os.path.join(ROMFS_GFX_DIR, "sheet_biome_0.t3x")
    build_pokemon_sheet(tex3ds, 19, biome_0_out)

    print("[+] Base spritesheet conversion complete.")

if __name__ == "__main__":
    main()
