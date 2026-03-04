import json
import math

DURATION = 12 * 60 + 28  # 12 minutes 28 seconds = 748 seconds
FPS = 60
N_FRAMES = int(DURATION * FPS)

frames = []

for i in range(N_FRAMES):
    t = i / FPS
    # Swirl period: 30 seconds for a full rotation
    swirl_period = 30.0
    theta = 2 * math.pi * (t / swirl_period)
    phi = 2 * math.pi * (t / (swirl_period * 2))
    # Node 1.1: swirl in xyz
    x1 = math.cos(theta)
    y1 = math.sin(theta)
    z1 = math.sin(phi)
    # Node 2.1: offset phase
    x2 = math.cos(theta + math.pi)
    y2 = math.sin(theta + math.pi)
    z2 = math.sin(phi + math.pi)
    frame = {
        "time": round(t, 5),
        "nodes": [
            {"id": "1.1", "type": "audio_object", "cart": [x1, y1, z1]},
            {"id": "2.1", "type": "audio_object", "cart": [x2, y2, z2]}
        ]
    }
    frames.append(frame)

scene = {
    "version": "0.5",
    "timeUnit": "seconds",
    "frames": frames
}

with open("sourceData/LUSID_package/scene.lusid.json", "w") as f:
    json.dump(scene, f, indent=2)
