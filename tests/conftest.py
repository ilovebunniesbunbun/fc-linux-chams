import os
import sys
import time
import subprocess
import shutil
import mmap
import ctypes
import ctypes.util
import pytest

# Struct definitions representing the shared memory bridge in ctypes

class Vec3(ctypes.Structure):
    _fields_ = [
        ("x", ctypes.c_float),
        ("y", ctypes.c_float),
        ("z", ctypes.c_float)
    ]

class Vec4(ctypes.Structure):
    _fields_ = [
        ("x", ctypes.c_float),
        ("y", ctypes.c_float),
        ("z", ctypes.c_float),
        ("w", ctypes.c_float)
    ]

class BoneTransform(ctypes.Structure):
    _fields_ = [
        ("position", Vec3),
        ("rotation", Vec4)
    ]

class PlayerData(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("team", ctypes.c_int),
        ("health", ctypes.c_int),
        ("active", ctypes.c_int),
        ("has_defuser", ctypes.c_int),
        ("origin", Vec3),
        ("model_name", ctypes.c_char * 64),
        ("bone_count", ctypes.c_int),
        ("bones", BoneTransform * 128)
    ]

class InFlightProjectile(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("entity_handle", ctypes.c_uint32),
        ("type", ctypes.c_uint8),
        ("initial_position", Vec3),
        ("initial_velocity", Vec3),
        ("current_position", Vec3),
        ("spawn_time", ctypes.c_float),
        ("timer_start_time", ctypes.c_float),
        ("duration", ctypes.c_float),
        ("active", ctypes.c_uint8)
    ]

class InfernoData(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("entity_handle", ctypes.c_uint32),
        ("start_time", ctypes.c_float),
        ("duration", ctypes.c_float),
        ("fire_count", ctypes.c_int),
        ("fire_positions", Vec3 * 64),
        ("active", ctypes.c_uint8)
    ]

class ShmPacket(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("frame_index", ctypes.c_uint32),
        ("view_matrix", ctypes.c_float * 16),
        ("local_eye", Vec3),
        ("map_name", ctypes.c_char * 64),
        
        ("held_grenade_type", ctypes.c_uint8),
        ("pin_pulled", ctypes.c_uint8),
        ("throw_strength", ctypes.c_float),
        ("local_velocity", Vec3),
        ("local_angles", Vec3),
        
        ("projectile_count", ctypes.c_int),
        ("projectiles", InFlightProjectile * 8),

        ("inferno_count", ctypes.c_int),
        ("infernos", InfernoData * 4),
        
        ("player_count", ctypes.c_int),
        ("players", PlayerData * 64)
    ]

class MockBridge:
    """Python class to write mock frames to the POSIX shared memory /fc2_chams_shm_bridge
    and signal the named semaphore /fc2_chams_shm_sem using ctypes calls to libc.
    """
    def __init__(self, shm_path=None, sem_name=None):
        self.shm_size = ctypes.sizeof(ShmPacket)
        self.shm_path = shm_path or "/dev/shm/fc2_chams_shm_bridge_test"
        self.sem_name = sem_name or b"/fc2_chams_shm_sem_test"
        
        # Clean up old shm if any
        if os.path.exists(self.shm_path):
            try:
                os.unlink(self.shm_path)
            except Exception:
                pass

        # Open shared memory file
        self.fd = os.open(self.shm_path, os.O_CREAT | os.O_RDWR, 0o666)
        os.ftruncate(self.fd, self.shm_size)
        self.shm_buf = mmap.mmap(self.fd, self.shm_size, mmap.MAP_SHARED, mmap.PROT_WRITE)
        
        # Initialize ctypes semaphore functions
        self._init_semaphore_funcs()
        
        # Unlink any existing named semaphore from previous runs
        self.sem_lib.sem_unlink(self.sem_name)
        
        # Open the semaphore
        O_CREAT = 0x40
        self.sem = self.sem_lib.sem_open(self.sem_name, O_CREAT, 0o666, 0)
        if not self.sem or self.sem == -1:
            raise RuntimeError("Failed to open named POSIX semaphore via ctypes.")
        
        self.seq_num = 0

    def _init_semaphore_funcs(self):
        import ctypes.util
        self.sem_lib = None
        for libname in ['pthread', 'rt', 'c']:
            path = ctypes.util.find_library(libname)
            if path:
                try:
                    lib = ctypes.CDLL(path)
                    if hasattr(lib, 'sem_open'):
                        self.sem_lib = lib
                        break
                except Exception:
                    pass
        if not self.sem_lib:
            try:
                self.sem_lib = ctypes.CDLL('libpthread.so.0')
            except Exception:
                try:
                    self.sem_lib = ctypes.CDLL('libc.so.6')
                except Exception:
                    self.sem_lib = ctypes.CDLL(None)

        self.sem_lib.sem_open.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_uint32, ctypes.c_uint32]
        self.sem_lib.sem_open.restype = ctypes.c_void_p
        
        self.sem_lib.sem_post.argtypes = [ctypes.c_void_p]
        self.sem_lib.sem_post.restype = ctypes.c_int
        
        self.sem_lib.sem_close.argtypes = [ctypes.c_void_p]
        self.sem_lib.sem_close.restype = ctypes.c_int
        
        self.sem_lib.sem_unlink.argtypes = [ctypes.c_char_p]
        self.sem_lib.sem_unlink.restype = ctypes.c_int

    def write_frame(self, packet: ShmPacket):
        # 1. Update sequence number to odd (seqlock protocol)
        self.seq_num = (self.seq_num + 1) | 1
        packet.frame_index = self.seq_num
        
        # Write entire packet contents
        self.shm_buf.seek(0)
        self.shm_buf.write(bytes(packet))
        
        # 2. Update sequence number to even (seqlock protocol)
        self.seq_num += 1
        self.shm_buf.seek(0)
        self.shm_buf.write(ctypes.c_uint32(self.seq_num))
        
        # 3. Post to semaphore
        self.sem_lib.sem_post(self.sem)

    def close(self):
        if hasattr(self, 'shm_buf') and self.shm_buf:
            try:
                self.shm_buf.close()
            except Exception:
                pass
        if hasattr(self, 'fd') and self.fd >= 0:
            try:
                os.close(self.fd)
            except Exception:
                pass
        if os.path.exists(self.shm_path):
            try:
                os.unlink(self.shm_path)
            except Exception:
                pass
        if hasattr(self, 'sem') and self.sem and self.sem != -1:
            try:
                self.sem_lib.sem_close(self.sem)
            except Exception:
                pass
            try:
                self.sem_lib.sem_unlink(self.sem_name)
            except Exception:
                pass
            self.sem = None

@pytest.fixture
def mock_bridge():
    bridge = MockBridge()
    # Write a default dummy frame first to allow initialization
    p = ShmPacket()
    p.player_count = 0
    p.map_name = b"de_dust2"
    p.view_matrix[0] = 1.0
    p.view_matrix[5] = 1.0
    p.view_matrix[10] = 1.0
    p.view_matrix[15] = 1.0
    bridge.write_frame(p)
    yield bridge
    bridge.close()

@pytest.fixture
def config_manager():
    config_path = "/home/milo/Desktop/fc2-chams-rewrite/fc2-chams/overlay.json"
    backup_path = "/home/milo/Desktop/fc2-chams-rewrite/fc2-chams/overlay.json.bak"
    
    # Backup existing
    if os.path.exists(config_path):
        shutil.copy2(config_path, backup_path)
    else:
        # Create a default empty config
        with open(backup_path, 'w') as f:
            f.write("{}")

    yield config_path

    # Restore
    if os.path.exists(backup_path):
        shutil.copy2(backup_path, config_path)
        os.remove(backup_path)

@pytest.fixture
def run_fc2_chams():
    processes = []
    
    def _run(args=None, env=None, cwd="/home/milo/Desktop/fc2-chams-rewrite/fc2-chams"):
        binary = "./build/fc2_chams"
        if not os.path.exists(os.path.join(cwd, binary)):
            raise FileNotFoundError(f"Binary not found: {os.path.join(cwd, binary)}")
            
        cmd = [binary]
        if args:
            cmd.extend(args)
            
        # Detect if we should wrap in xvfb-run
        has_xvfb = shutil.which("xvfb-run") is not None
        use_xvfb = has_xvfb and ("DISPLAY" not in os.environ)
        
        if use_xvfb:
            cmd = ["xvfb-run", "--server-args=-screen 0 1280x1024x24"] + cmd
            
        run_env = os.environ.copy()
        run_env["FC2_SHM_NAME"] = "/fc2_chams_shm_bridge_test"
        run_env["FC2_SEM_NAME"] = "/fc2_chams_shm_sem_test"
        if env:
            run_env.update(env)
            
        p = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            cwd=cwd,
            env=run_env
        )
        processes.append(p)
        return p
        
    yield _run
    
    for p in processes:
        if p.poll() is None:
            p.terminate()
            try:
                p.wait(timeout=2)
            except subprocess.TimeoutExpired:
                p.kill()
