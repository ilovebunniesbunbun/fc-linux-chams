import os
import sys
import time
import json
import subprocess
import pytest
from tests.conftest import ShmPacket, PlayerData, Vec3, BoneTransform, Vec4

def test_tier1_overlay_initialization(run_fc2_chams, config_manager, mock_bridge):
    """
    Tier 1: Verify correct process launch, configuration parsing, OpenGL initialization,
    and shared memory bridge connection.
    """
    vpk_path = os.path.abspath("pak01_dir.vpk")
    
    # Generate mock VPK
    from tests.mock_vpk_gen import create_mock_vpk
    create_mock_vpk(vpk_path)
    
    config = {
        "monitor_w": 1280,
        "monitor_h": 1024,
        "game_w": 1024,
        "game_h": 768,
        "scaling": "stretched",
        "fps": 60,
        "show_fps": True,
        "hyprland_support": False,
        "vpk_path": vpk_path,
        "use_depth_prepass": False,
        "use_bvh_fallback": True,
        "maps_dir": "."
    }
    
    with open(config_manager, "w") as f:
        json.dump(config, f, indent=4)
        
    # Write a packet to SHM
    packet = ShmPacket()
    packet.player_count = 1
    packet.map_name = b"de_dust2"
    packet.players[0].team = 2
    packet.players[0].health = 100
    packet.players[0].active = 1
    packet.players[0].model_name = b"agents/models/ctm_sas/ctm_sas.vmdl"
    packet.players[0].bone_count = 0
    packet.players[0].origin = Vec3(10.0, 20.0, 30.0)
    
    mock_bridge.write_frame(packet)
    
    # Launch the process
    p = run_fc2_chams()
    
    # Wait a bit to collect output
    time.sleep(3.0)
    
    p.terminate()
    try:
        stdout, stderr = p.communicate(timeout=2.0)
    except subprocess.TimeoutExpired:
        p.kill()
        stdout, stderr = p.communicate()
        
    print("STDOUT:")
    print(stdout)
    print("STDERR:")
    print(stderr)
    
    # Assert successful initialization and launch logs
    assert "Launching GPU-Skinned Linux Overlay" in stdout
    assert "Loaded modern OpenGL core functions" in stdout
    assert "Shared memory bridge communication active" in stdout
    assert "Loaded model successfully" in stdout
