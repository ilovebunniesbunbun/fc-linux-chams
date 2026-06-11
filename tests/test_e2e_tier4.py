import os
import sys
import time
import json
import subprocess
import pytest
from tests.conftest import ShmPacket, PlayerData, Vec3, BoneTransform, Vec4

def test_tier4_game_session_simulation(run_fc2_chams, config_manager, mock_bridge):
    """
    Tier 4: Simulate a full Counter-Strike match session including map changes,
    round transitions, player health modifications, player death, and server disconnects.
    """
    vpk_path = os.path.abspath("pak01_dir.vpk")
    config = {
        "vpk_path": vpk_path,
        "fps": 60,
        "use_depth_prepass": False,
        "maps_dir": "."
    }
    with open(config_manager, "w") as f:
        json.dump(config, f)
        
    p = run_fc2_chams()
    time.sleep(1.0)
    
    # 1. Match Start: de_dust2 with 5 players
    print("[TEST] Step 1: Match starts on de_dust2 with 5 active players")
    packet = ShmPacket()
    packet.player_count = 5
    packet.map_name = b"de_dust2"
    for i in range(5):
        player = packet.players[i]
        player.team = 2 if i < 2 else 3  # 2 Terrorists, 3 Counter-Terrorists
        player.health = 100
        player.active = 1
        player.model_name = b"agents/models/ctm_sas/ctm_sas.vmdl"
        player.origin = Vec3(float(i * 100), float(i * 50), 0.0)
        player.bone_count = 0
        
    mock_bridge.write_frame(packet)
    time.sleep(0.5)
    
    # 2. Combat simulation: Players take damage, one dies
    print("[TEST] Step 2: Combat simulation - health changes and death")
    packet.players[0].health = 45   # Player 0 takes damage
    packet.players[1].health = 0    # Player 1 dies
    packet.players[1].active = 0
    packet.players[2].health = 80   # Player 2 takes damage
    
    mock_bridge.write_frame(packet)
    time.sleep(0.5)
    
    # 3. Round End / Map Transition to de_inferno
    print("[TEST] Step 3: Round ends, map transitions to de_inferno")
    packet.player_count = 0
    packet.map_name = b"de_inferno"
    
    mock_bridge.write_frame(packet)
    time.sleep(0.5)
    
    # 4. New round on de_inferno: Players respawn
    print("[TEST] Step 4: New round on de_inferno - all players active again")
    packet.player_count = 5
    for i in range(5):
        player = packet.players[i]
        player.team = 2 if i < 2 else 3
        player.health = 100
        player.active = 1
        player.model_name = b"agents/models/ctm_sas/ctm_sas.vmdl"
        player.origin = Vec3(float(i * 100 + 500), float(i * 50 - 200), 10.0)
        player.bone_count = 0
        
    mock_bridge.write_frame(packet)
    time.sleep(0.5)
    
    # 5. Player Disconnects / Server shutdown
    print("[TEST] Step 5: Server disconnects - clear geometries and players")
    packet.player_count = 0
    packet.map_name = b""
    
    mock_bridge.write_frame(packet)
    time.sleep(0.5)
    
    p.terminate()
    stdout, stderr = p.communicate()
    
    print("STDOUT:")
    print(stdout)
    
    # Verify events in stdout
    assert "Map change detected: de_dust2" in stdout
    assert "Map change detected: de_inferno" in stdout
    assert "Left map, clearing map geometries" in stdout
