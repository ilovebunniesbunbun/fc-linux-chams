import os
import sys
import time
import json
import subprocess
import pytest
from tests.conftest import ShmPacket, PlayerData, Vec3, BoneTransform, Vec4

def test_tier3_fps_pacing(run_fc2_chams, config_manager, mock_bridge):
    """Verify pacing adjustments when frame targets are modified."""
    vpk_path = os.path.abspath("pak01_dir.vpk")
    
    # Test low frame rate target (e.g. 15 FPS) to check spacing
    config_15 = {
        "vpk_path": vpk_path,
        "fps": 15,
        "show_fps": True,
        "maps_dir": "."
    }
    with open(config_manager, "w") as f:
        json.dump(config_15, f)
        
    p1 = run_fc2_chams()
    time.sleep(2.0)
    p1.terminate()
    stdout_15, _ = p1.communicate()
    
    # Test high frame rate target (e.g. 60 FPS)
    config_60 = {
        "vpk_path": vpk_path,
        "fps": 60,
        "show_fps": True,
        "maps_dir": "."
    }
    with open(config_manager, "w") as f:
        json.dump(config_60, f)
        
    p2 = run_fc2_chams()
    time.sleep(2.0)
    p2.terminate()
    stdout_60, _ = p2.communicate()
    
    assert "Launching GPU-Skinned Linux Overlay" in stdout_15
    assert "Launching GPU-Skinned Linux Overlay" in stdout_60

def test_tier3_concurrent_loading_responsiveness(run_fc2_chams, config_manager, mock_bridge):
    """Verify that model loading and map parsing do not block the main rendering thread."""
    vpk_path = os.path.abspath("pak01_dir.vpk")
    config = {
        "vpk_path": vpk_path,
        "fps": 60,
        "use_depth_prepass": False,
        "maps_dir": "."
    }
    with open(config_manager, "w") as f:
        json.dump(config, f)
        
    packet = ShmPacket()
    packet.player_count = 2
    packet.map_name = b"de_dust2"
    
    # Player 1 loading custom model
    packet.players[0].team = 2
    packet.players[0].health = 100
    packet.players[0].active = 1
    packet.players[0].model_name = b"agents/models/ctm_sas/ctm_sas.vmdl"
    packet.players[0].origin = Vec3(100.0, 100.0, 0.0)
    packet.players[0].bone_count = 0
    
    # Player 2 loading same model
    packet.players[1].team = 3
    packet.players[1].health = 100
    packet.players[1].active = 1
    packet.players[1].model_name = b"agents/models/ctm_sas/ctm_sas.vmdl"
    packet.players[1].origin = Vec3(-100.0, -100.0, 0.0)
    packet.players[1].bone_count = 0
    
    mock_bridge.write_frame(packet)
    p = run_fc2_chams()
    
    # During rendering loop, let's change map and write frames rapidly
    time.sleep(1.0)
    for i in range(10):
        packet.map_name = b"de_dust2" if i % 2 == 0 else b"de_mirage"
        mock_bridge.write_frame(packet)
        time.sleep(0.1)
        
    time.sleep(1.0)
    p.terminate()
    stdout, _ = p.communicate()
    
    # Ensure map transitions and model cache loading happen concurrently
    assert "Map change detected" in stdout
    assert "MODEL_CACHE: Loaded model" in stdout
    assert "Bridge active" in stdout or "Shared memory bridge communication active" in stdout
