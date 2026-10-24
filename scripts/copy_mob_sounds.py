"""Convert the user's local Minecraft mob sounds into the game's layout:
assets/Sounds/mob/<name>/<event>N.wav (44.1 kHz mono s16, N = 1..4).

usage: copy_mob_sounds.py <.minecraft dir> <project dir>
"""
import json
import os
import subprocess
import sys

MC, PROJECT = sys.argv[1], sys.argv[2]

# event -> list of Minecraft sound paths (relative to minecraft/sounds/),
# following vanilla sounds.json assignments.
MAPPING = {
    "pig": {
        "ambient": ["mob/pig/say1", "mob/pig/say2", "mob/pig/say3"],
        "hurt": ["mob/pig/say1", "mob/pig/say2", "mob/pig/say3"],
        "death": ["mob/pig/death"],
        "step": ["mob/pig/step1", "mob/pig/step2", "mob/pig/step3", "mob/pig/step4"],
    },
    "cow": {
        "ambient": ["mob/cow/say1", "mob/cow/say2", "mob/cow/say3", "mob/cow/say4"],
        "hurt": ["mob/cow/hurt1", "mob/cow/hurt2", "mob/cow/hurt3"],
        "death": ["mob/cow/hurt1", "mob/cow/hurt2", "mob/cow/hurt3"],
        "step": ["mob/cow/step1", "mob/cow/step2", "mob/cow/step3", "mob/cow/step4"],
    },
    "sheep": {
        "ambient": ["mob/sheep/say1", "mob/sheep/say2", "mob/sheep/say3"],
        "hurt": ["mob/sheep/say1", "mob/sheep/say2", "mob/sheep/say3"],
        "death": ["mob/sheep/say1", "mob/sheep/say2", "mob/sheep/say3"],
        "step": ["mob/sheep/step1", "mob/sheep/step2", "mob/sheep/step3", "mob/sheep/step4"],
    },
    "chicken": {
        "ambient": ["mob/chicken/say1", "mob/chicken/say2", "mob/chicken/say3"],
        "hurt": ["mob/chicken/hurt1", "mob/chicken/hurt2"],
        "death": ["mob/chicken/hurt1", "mob/chicken/hurt2"],
        "step": ["mob/chicken/step1", "mob/chicken/step2"],
    },
    "zombie": {
        "ambient": ["mob/zombie/say1", "mob/zombie/say2", "mob/zombie/say3"],
        "hurt": ["mob/zombie/hurt1", "mob/zombie/hurt2"],
        "death": ["mob/zombie/death"],
        "step": ["mob/zombie/step1", "mob/zombie/step2", "mob/zombie/step3", "mob/zombie/step4"],
    },
    "skeleton": {
        "ambient": ["mob/skeleton/say1", "mob/skeleton/say2", "mob/skeleton/say3"],
        "hurt": ["mob/skeleton/hurt1", "mob/skeleton/hurt2", "mob/skeleton/hurt3", "mob/skeleton/hurt4"],
        "death": ["mob/skeleton/death"],
        "step": ["mob/skeleton/step1", "mob/skeleton/step2", "mob/skeleton/step3", "mob/skeleton/step4"],
        "attack": ["random/bow"],
    },
    "creeper": {
        "hurt": ["mob/creeper/say1", "mob/creeper/say2", "mob/creeper/say3", "mob/creeper/say4"],
        "death": ["mob/creeper/death"],
        "attack": ["random/fuse"],  # ignition hiss
    },
    "spider": {
        "ambient": ["mob/spider/say1", "mob/spider/say2", "mob/spider/say3", "mob/spider/say4"],
        "hurt": ["mob/spider/say1", "mob/spider/say2", "mob/spider/say3", "mob/spider/say4"],
        "death": ["mob/spider/death"],
        "step": ["mob/spider/step1", "mob/spider/step2", "mob/spider/step3", "mob/spider/step4"],
    },
}

index = json.load(open(os.path.join(MC, "assets/indexes/32.json")))["objects"]
written = 0
for mob, events in MAPPING.items():
    out_dir = os.path.join(PROJECT, "assets/Sounds/mob", mob)
    os.makedirs(out_dir, exist_ok=True)
    for event, sources in events.items():
        for n, src in enumerate(sources[:4], start=1):
            h = index["minecraft/sounds/" + src + ".ogg"]["hash"]
            ogg = os.path.join(MC, "assets/objects", h[:2], h)
            wav = os.path.join(out_dir, f"{event}{n}.wav")
            subprocess.run(
                ["ffmpeg", "-loglevel", "error", "-y", "-i", ogg, "-ac", "1", "-ar", "44100", "-sample_fmt", "s16", wav],
                check=True,
            )
            written += 1
print("wrote", written, "files")
