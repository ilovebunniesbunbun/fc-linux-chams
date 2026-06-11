#!/usr/bin/env python3
import os
import sys
import time
import math
import json
import ctypes

# Add the current directory to sys.path so we can import from tests
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

from tests.conftest import ShmPacket, PlayerData, Vec3, BoneTransform, Vec4, MockBridge
from tests.mock_vpk_gen import create_mock_vpk

# Vector and Matrix Helper Math for Camera simulation
def perspective(fov_deg, aspect, near, far):
    fov_rad = math.radians(fov_deg)
    f = 1.0 / math.tan(fov_rad / 2.0)
    m = [0.0] * 16
    m[0] = f / aspect
    m[5] = f
    m[10] = (far + near) / (near - far)
    m[11] = -1.0
    m[14] = (2.0 * far * near) / (near - far)
    return m

def look_at(eye, target, up):
    # Calculate forward vector (z-axis)
    ez = [eye[0] - target[0], eye[1] - target[1], eye[2] - target[2]]
    dz = math.sqrt(ez[0]**2 + ez[1]**2 + ez[2]**2)
    z = [ez[0] / dz, ez[1] / dz, ez[2] / dz] if dz > 0 else [0.0, 0.0, 1.0]

    # Calculate right vector (x-axis) = up x z
    cx = [
        up[1] * z[2] - up[2] * z[1],
        up[2] * z[0] - up[0] * z[2],
        up[0] * z[1] - up[1] * z[0]
    ]
    dx = math.sqrt(cx[0]**2 + cx[1]**2 + cx[2]**2)
    x = [cx[0] / dx, cx[1] / dx, cx[2] / dx] if dx > 0 else [1.0, 0.0, 0.0]

    # Calculate up vector (y-axis) = z x x
    y = [
        z[1] * x[2] - z[2] * x[1],
        z[2] * x[0] - z[0] * x[2],
        z[0] * x[1] - z[1] * x[0]
    ]

    # Dot products for translation part
    dot_x = -(x[0] * eye[0] + x[1] * eye[1] + x[2] * eye[2])
    dot_y = -(y[0] * eye[0] + y[1] * eye[1] + y[2] * eye[2])
    dot_z = -(z[0] * eye[0] + z[1] * eye[1] + z[2] * eye[2])

    # Row-major matrix structure
    m = [0.0] * 16
    m[0] = x[0];  m[1] = x[1];  m[2] = x[2];  m[3] = dot_x
    m[4] = y[0];  m[5] = y[1];  m[6] = y[2];  m[7] = dot_y
    m[8] = z[0];  m[9] = z[1];  m[10] = z[2]; m[11] = dot_z
    m[12] = 0.0;  m[13] = 0.0;  m[14] = 0.0;  m[15] = 1.0
    return m

def multiply_matrices(a, b):
    out = [0.0] * 16
    for r in range(4):
        for c in range(4):
            val = 0.0
            for k in range(4):
                val += a[r * 4 + k] * b[k * 4 + c]
            out[r * 4 + c] = val
    return out

def main():
    print("=== FC2 CHAMS V2: Shared Memory & Lua Data Bridge Simulator ===")

    # 1. Ensure mock VPK exists locally
    vpk_filename = "pak01_dir.vpk"
    if not os.path.exists(vpk_filename):
        print(f"[*] Generating mock VPK at {vpk_filename}...")
        create_mock_vpk(vpk_filename)
        print("[+] Mock VPK created successfully.")
    else:
        print(f"[+] Mock VPK found at {vpk_filename}.")

    # 2. Inspect and update overlay.json configuration to point to local VPK if real CS2 VPK is not found
    config_path = "overlay.json"
    if os.path.exists(config_path):
        try:
            with open(config_path, "r") as f:
                config = json.load(f)
            
            # Detect if a real CS2 installation exists on the system
            home = os.path.expanduser("~")
            steam_vpk_candidates = [
                os.path.join(home, ".steam/steam/steamapps/common/Counter-Strike Global Offensive/game/csgo/pak01_dir.vpk"),
                os.path.join(home, ".steam/root/steamapps/common/Counter-Strike Global Offensive/game/csgo/pak01_dir.vpk"),
                os.path.join(home, ".local/share/Steam/steamapps/common/Counter-Strike Global Offensive/game/csgo/pak01_dir.vpk"),
                os.path.join(home, ".var/app/com.valvesoftware.Steam/.steam/steam/steamapps/common/Counter-Strike Global Offensive/game/csgo/pak01_dir.vpk"),
            ]
            has_real_vpk = any(os.path.exists(c) for c in steam_vpk_candidates)

            # Only override if the current setting is auto and we don't have CS2 installed, or if the current path is invalid
            current_vpk = config.get("vpk_path", "auto")
            needs_mock_override = False
            if current_vpk == "auto" and not has_real_vpk:
                needs_mock_override = True
            elif current_vpk != "auto" and not os.path.exists(current_vpk):
                needs_mock_override = True

            if needs_mock_override:
                config["vpk_path"] = os.path.abspath(vpk_filename)
                config["use_depth_prepass"] = False
                with open(config_path, "w") as f:
                    json.dump(config, f, indent=4)
                print(f"[+] No real CS2 VPK found. Overrode {config_path} to use mock VPK.")
            else:
                print(f"[+] Keeping current {config_path} settings (vpk_path='{current_vpk}').")
        except Exception as e:
            print(f"[-] Warning: Failed to read/update {config_path}: {e}")

    # 3. Open POSIX shared memory bridge
    print("[*] Initializing POSIX Shared Memory segment & Semaphore...")
    try:
        bridge = MockBridge()
        print("[+] Bridge successfully initialized.")
    except Exception as e:
        print(f"[-] ERROR: Failed to setup POSIX shared memory: {e}")
        print("    Ensure you have write permission to /dev/shm")
        return 1

    print("[*] Simulation loop running. Press Ctrl+C to stop.")
    print("    Launch './build/fc2_chams' in another terminal to connect.")

    # 4. Main simulation loop
    start_time = time.time()
    packet = ShmPacket()
    packet.map_name = b"de_dust2"

    try:
        while True:
            t = time.time() - start_time
            
            # Orbiting camera math
            radius = 300.0
            speed = 0.5
            angle = t * speed
            
            eye_x = radius * math.cos(angle)
            eye_y = radius * math.sin(angle)
            eye_z = 100.0 + 50.0 * math.sin(t)
            
            eye = [eye_x, eye_y, eye_z]
            target = [0.0, 0.0, 0.0]
            up = [0.0, 0.0, 1.0]

            # Compute matrices
            proj_mat = perspective(60.0, 1280.0 / 960.0, 10.0, 2000.0)
            view_mat = look_at(eye, target, up)
            vp_mat = multiply_matrices(proj_mat, view_mat)
            
            # Fill View Matrix (Row-major)
            for i in range(16):
                packet.view_matrix[i] = vp_mat[i]
                
            packet.local_eye = Vec3(eye[0], eye[1], eye[2])
            
            # Configure 3 simulated players
            packet.player_count = 3
            
            # Player 1 (Terrorist, Team 2) at origin (0, 0, 0)
            packet.players[0].team = 2
            packet.players[0].health = 100
            packet.players[0].active = 1
            packet.players[0].model_name = b"agents/models/ctm_sas/ctm_sas.vmdl"
            packet.players[0].origin = Vec3(0.0, 0.0, 0.0)
            packet.players[0].bone_count = 2
            
            # Bone positions relative to player origin
            # Bone 0: Root / base
            packet.players[0].bones[0].position = Vec3(0.0, 0.0, 0.0)
            packet.players[0].bones[0].rotation = Vec4(0.0, 0.0, 0.0, 1.0)
            # Bone 1: Head, moving up and down slightly
            packet.players[0].bones[1].position = Vec3(0.0, 0.0, 60.0 + 10.0 * math.sin(t * 3.0))
            packet.players[0].bones[1].rotation = Vec4(0.0, 0.0, 0.0, 1.0)
            
            # Player 2 (Counter-Terrorist, Team 3) offset
            packet.players[1].team = 3
            packet.players[1].health = 80
            packet.players[1].active = 1
            packet.players[1].model_name = b"agents/models/ctm_sas/ctm_sas.vmdl"
            packet.players[1].origin = Vec3(100.0, 50.0, 10.0)
            packet.players[1].bone_count = 2
            packet.players[1].bones[0].position = Vec3(100.0, 50.0, 10.0)
            packet.players[1].bones[0].rotation = Vec4(0.0, 0.0, 0.0, 1.0)
            packet.players[1].bones[1].position = Vec3(100.0, 50.0, 70.0)
            packet.players[1].bones[1].rotation = Vec4(0.0, 0.0, 0.0, 1.0)

            # Player 3 (Terrorist, Team 2) at another position
            packet.players[2].team = 2
            packet.players[2].health = 45
            packet.players[2].active = 1
            packet.players[2].model_name = b"agents/models/ctm_sas/ctm_sas.vmdl"
            packet.players[2].origin = Vec3(-80.0, -100.0, -10.0)
            packet.players[2].bone_count = 2
            packet.players[2].bones[0].position = Vec3(-80.0, -100.0, -10.0)
            packet.players[2].bones[0].rotation = Vec4(0.0, 0.0, 0.0, 1.0)
            packet.players[2].bones[1].position = Vec3(-80.0, -100.0, 50.0)
            packet.players[2].bones[1].rotation = Vec4(0.0, 0.0, 0.0, 1.0)

            # Write packet and signal semaphore
            bridge.write_frame(packet)
            
            # Update display info every half-second
            if int(t * 2) % 2 == 0 and int((t - 0.016) * 2) % 2 != 0:
                print(f"[SHM SIM] Frame {bridge.seq_num} sent. Camera at ({eye[0]:.1f}, {eye[1]:.1f}, {eye[2]:.1f}) looking at (0, 0, 0). 3 active players.")
                
            # Yield/sleep to match ~60 FPS update rate (16.6ms)
            time.sleep(0.0166)
            
    except KeyboardInterrupt:
        print("\n[*] Shutting down simulator cleanly...")
    finally:
        bridge.close()
        print("[+] Simulator shut down. Goodbye!")

if __name__ == "__main__":
    sys.exit(main())
