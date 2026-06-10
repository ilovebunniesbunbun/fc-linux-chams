import os
import sys
import time
import json
import math
import subprocess
import pytest
from tests.conftest import ShmPacket, PlayerData, Vec3, BoneTransform, Vec4

def test_tier2_max_players(run_fc2_chams, config_manager, mock_bridge):
    """Verify application handles maximum capacity of players (64 players)."""
    vpk_path = os.path.abspath("pak01_dir.vpk")
    config = {
        "vpk_path": vpk_path,
        "fps": 60,
        "maps_dir": "."
    }
    with open(config_manager, "w") as f:
        json.dump(config, f)
        
    packet = ShmPacket()
    packet.player_count = 64
    packet.map_name = b"de_dust2"
    
    for i in range(64):
        packet.players[i].team = 2 if i % 2 == 0 else 3
        packet.players[i].health = 100
        packet.players[i].active = 1
        packet.players[i].model_name = b"agents/models/ctm_sas/ctm_sas.vmdl"
        packet.players[i].origin = Vec3(float(i * 10), 0.0, 0.0)
        packet.players[i].bone_count = 0

    mock_bridge.write_frame(packet)
    p = run_fc2_chams()
    time.sleep(2.0)
    
    p.terminate()
    stdout, _ = p.communicate()
    assert "Launching GPU-Skinned Linux Overlay" in stdout

def test_tier2_zero_players(run_fc2_chams, config_manager, mock_bridge):
    """Verify application handles empty player lists."""
    vpk_path = os.path.abspath("pak01_dir.vpk")
    config = {
        "vpk_path": vpk_path,
        "fps": 60,
        "maps_dir": "."
    }
    with open(config_manager, "w") as f:
        json.dump(config, f)
        
    packet = ShmPacket()
    packet.player_count = 0
    packet.map_name = b"de_dust2"
    
    mock_bridge.write_frame(packet)
    p = run_fc2_chams()
    time.sleep(1.5)
    
    p.terminate()
    stdout, _ = p.communicate()
    assert "Launching GPU-Skinned Linux Overlay" in stdout

def test_tier2_rapid_map_changes(run_fc2_chams, config_manager, mock_bridge):
    """Verify that rapid map transitions do not crash the map-parsing or BVH loader threads."""
    vpk_path = os.path.abspath("pak01_dir.vpk")
    config = {
        "vpk_path": vpk_path,
        "fps": 60,
        "maps_dir": "."
    }
    with open(config_manager, "w") as f:
        json.dump(config, f)
        
    packet = ShmPacket()
    packet.player_count = 1
    packet.map_name = b"de_dust2"
    packet.players[0].team = 2
    packet.players[0].health = 100
    packet.players[0].active = 1
    packet.players[0].model_name = b"agents/models/ctm_sas/ctm_sas.vmdl"
    packet.players[0].origin = Vec3(0.0, 0.0, 0.0)
    packet.players[0].bone_count = 0
    
    mock_bridge.write_frame(packet)
    p = run_fc2_chams()
    time.sleep(1.0)
    
    # Rapid map switching
    for new_map in [b"de_mirage", b"", b"de_inferno", b"de_dust2"]:
        packet.map_name = new_map
        mock_bridge.write_frame(packet)
        time.sleep(0.2)
        
    time.sleep(1.0)
    p.terminate()
    stdout, _ = p.communicate()
    assert "Map change detected" in stdout

def test_tier2_corrupt_config(run_fc2_chams, config_manager, mock_bridge):
    """Verify that corrupted configuration files do not crash the application."""
    # Write invalid JSON to overlay.json
    with open(config_manager, "w") as f:
        f.write("invalid json: { foo = bar }")
        
    p = run_fc2_chams()
    time.sleep(2.0)
    p.terminate()
    stdout, _ = p.communicate()
    
    # It should fallback to default config or complain but run
    assert "Launching GPU-Skinned Linux Overlay" in stdout

def test_tier2_missing_vpk(run_fc2_chams, config_manager, mock_bridge):
    """Verify application behavior when VPK file is missing or path is invalid."""
    config = {
        "vpk_path": "/nonexistent/path/pak01_dir.vpk",
        "fps": 60,
        "maps_dir": "."
    }
    with open(config_manager, "w") as f:
        json.dump(config, f)
        
    packet = ShmPacket()
    packet.player_count = 1
    packet.map_name = b"de_dust2"
    packet.players[0].team = 2
    packet.players[0].health = 100
    packet.players[0].active = 1
    packet.players[0].model_name = b"agents/models/ctm_sas/ctm_sas.vmdl"
    packet.players[0].origin = Vec3(0.0, 0.0, 0.0)
    packet.players[0].bone_count = 0
    
    mock_bridge.write_frame(packet)
    p = run_fc2_chams()
    time.sleep(2.0)
    
    p.terminate()
    stdout, _ = p.communicate()
    # Should report warning or fallback to default path discovery
    assert "Launching GPU-Skinned Linux Overlay" in stdout
    # In model_cache, it logs failure to load model
    assert "MODEL_CACHE: Failed to load model" in stdout

def test_tier2_malformed_player_data(run_fc2_chams, config_manager, mock_bridge):
    """Verify that out-of-bounds player positions, huge matrices, or bad characters are handled."""
    vpk_path = os.path.abspath("pak01_dir.vpk")
    config = {
        "vpk_path": vpk_path,
        "fps": 60,
        "maps_dir": "."
    }
    with open(config_manager, "w") as f:
        json.dump(config, f)
        
    packet = ShmPacket()
    packet.player_count = 1
    packet.map_name = b"de_dust2"
    packet.players[0].team = 2
    packet.players[0].health = 99999  # extreme health
    packet.players[0].active = 1
    
    # Path traversal attack model name
    packet.players[0].model_name = b"../../../../etc/passwd"
    packet.players[0].origin = Vec3(float('nan'), float('inf'), -1e20)  # malformed coords
    packet.players[0].bone_count = 0
    
    mock_bridge.write_frame(packet)
    p = run_fc2_chams()
    time.sleep(2.0)
    
    p.terminate()
    stdout, _ = p.communicate()
    assert "Launching GPU-Skinned Linux Overlay" in stdout
