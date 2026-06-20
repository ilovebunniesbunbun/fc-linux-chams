--[[
    @title
        FC2 Chams Shared Memory Collector Bridge
    @description
        Zero-GC data collector. Maps POSIX shared memory and streams 
        view matrices and skeletal bone arrays to the external renderer.
--]]

local ffi = require("ffi")
local bit = require("bit")
local modules = require("modules")
local players = require("players")
-- sig_test loading commented out for production

-- 1. Declare POSIX Shared Memory API, process_vm_readv, and target structs
ffi.cdef[[
    typedef int mode_t;
    typedef long off_t;
    
    int my_shm_open(const char *name, int oflag, mode_t mode) asm("shm_open");
    int my_shm_unlink(const char *name) asm("shm_unlink");
    int my_ftruncate(int fd, off_t length) asm("ftruncate");
    void *my_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) asm("mmap");
    int my_munmap(void *addr, size_t length) asm("munmap");
    int my_close(int fd) asm("close");

    typedef void sem_t;
    sem_t *my_sem_open(const char *name, int oflag, mode_t mode, unsigned int value) asm("sem_open");
    int my_sem_post(sem_t *sem) asm("sem_post");
    int my_sem_close(sem_t *sem) asm("sem_close");
    int my_sem_unlink(const char *name) asm("sem_unlink");

    struct iovec {
        void   *iov_base;
        size_t  iov_len;
    };
    typedef long ssize_t;
    ssize_t process_vm_readv(int pid, const struct iovec *local_iov, unsigned long liovcnt, const struct iovec *remote_iov, unsigned long riovcnt, unsigned long flags);

    #pragma pack(push, 1)
    struct vec3_t {
        float x, y, z;
    };

    struct vec4_t {
        float x, y, z, w;
    };

    struct in_flight_projectile_t {
        uint32_t entity_handle;
        uint8_t type;
        struct vec3_t initial_position;
        struct vec3_t initial_velocity;
        struct vec3_t current_position;
        float spawn_time;
        uint8_t active;
    };

    struct bone_transform_t {
        struct vec3_t position;
        struct vec4_t rotation;
    };

    struct player_data_t {
        int team;
        int health;
        int active;
        int has_defuser;
        struct vec3_t origin;
        char model_name[64];
        int bone_count;
        struct bone_transform_t bones[128];
    };

    struct shm_packet_t {
        uint32_t frame_index;
        float view_matrix[16];
        struct vec3_t local_eye;
        char map_name[64];
        
        uint8_t held_grenade_type;
        uint8_t pin_pulled;
        float throw_strength;
        struct vec3_t local_velocity;
        
        int projectile_count;
        struct in_flight_projectile_t projectiles[8];
        
        int player_count;
        struct player_data_t players[64];
    };
    #pragma pack(pop)
]]

local rt
do
    local ok, res = pcall(ffi.load, "rt")
    if ok then
        rt = res
    else
        ok, res = pcall(ffi.load, "librt.so.1")
        if ok then
            rt = res
        else
            rt = ffi.C
        end
    end
end
local C = ffi.C

-- Constants
local O_CREAT = 64
local O_RDWR = 2
local PROT_READ = 1
local PROT_WRITE = 2
local MAP_SHARED = 1
local SHM_NAME = "/fc2_chams_shm_bridge"
local SEM_NAME = "/fc2_chams_shm_sem"
local SHM_SIZE = ffi.sizeof("struct shm_packet_t")
local SEM_FAILED = ffi.cast("sem_t*", -1)

local last_log_time = 0

local bridge = {
    enabled = true,
    debug = true, -- Set to true to enable verbose frame logging
    debug_speed = 5000,
    use_pattern_scan = true, -- Set to true to enable ViewMatrix pattern scanning
    shm_fd = -1,
    shm_ptr = nil,
    shm_sem = nil,
    client_module = nil,
    view_matrix_addr = nil,
    cs2_pid = nil
}

-- Schema offsets (resolved dynamically)
local m_pGameSceneNode = nil
local m_modelState = nil
local m_ModelName = nil
local m_lifeState = nil
local m_iTeamNum = nil
local m_iHealth = nil

local m_pWeaponServices = nil
local m_hActiveWeapon = nil
local m_bPinPulled = nil
local m_flThrowStrength = nil
local m_vecAbsVelocity = nil
local m_vInitialPosition = nil
local m_vInitialVelocity = nil
local m_flSimulationTime = nil
local m_vSmokeDetonationPos = nil
local m_vecAbsOrigin = nil
local m_pEntity = nil

-- Grenade Type Lookup Maps
local GRENADE_CLASS_MAP = {
    C_HEGrenade = 1,          -- GRENADE_HE
    C_Flashbang = 2,          -- GRENADE_FLASH
    C_SmokeGrenade = 3,        -- GRENADE_SMOKE
    C_MolotovGrenade = 4,      -- GRENADE_MOLOTOV
    C_IncendiaryGrenade = 4,   -- GRENADE_MOLOTOV
    C_DecoyGrenade = 5,        -- GRENADE_DECOY
}

local GRENADE_ID_MAP = {
    [43] = 2, -- Flashbang -> GRENADE_FLASH
    [44] = 1, -- HE -> GRENADE_HE
    [45] = 3, -- Smoke -> GRENADE_SMOKE
    [46] = 4, -- Molotov -> GRENADE_MOLOTOV
    [47] = 5, -- Decoy -> GRENADE_DECOY
    [48] = 4, -- Incendiary -> GRENADE_MOLOTOV
}

local PROJECTILE_CLASS_MAP = {
    C_HEGrenadeProjectile = 1,
    C_FlashbangProjectile = 2,
    C_SmokeGrenadeProjectile = 3,
    C_MolotovProjectile = 4,
    C_DecoyProjectile = 5,
}

-- Reuse local IOV structures and buffer to prevent garbage collection allocations
local local_iov = ffi.new("struct iovec[1]")
local remote_iov = ffi.new("struct iovec[1]")
local raw_bones_buffer = ffi.new("struct { float pos[3]; float scale; float rot[4]; } [128]")
local local_packet = ffi.new("struct shm_packet_t")

local function read_raw_bytes(pid, address, size, dest_buffer)
    local_iov[0].iov_base = dest_buffer
    local_iov[0].iov_len = size
    remote_iov[0].iov_base = ffi.cast("void*", address)
    remote_iov[0].iov_len = size
    local bytes_read = C.process_vm_readv(pid, local_iov, 1, remote_iov, 1, 0)
    return bytes_read == size
end

local function to_raw_address(addr)
    if not addr then return nil end
    if type(addr) == "number" then return addr end
    
    if type(addr) == "table" then
        if addr.address then
            return to_raw_address(addr.address)
        end
    end
    
    if type(addr) == "userdata" or type(addr) == "cdata" then
        local ok, val = pcall(ffi.cast, "uintptr_t", addr)
        if ok and val then
            return tonumber(val)
        end
    end
    
    return addr
end

local function scan_forward_for_view_matrix(start_addr)
    if not start_addr or not start_addr.is_valid or not start_addr:is_valid() then
        return nil
    end
    for offset = 0, 4000 do
        local b1 = start_addr:read(MEM_BYTE, offset)
        if b1 == 0x4C then
            local b2 = start_addr:read(MEM_BYTE, offset + 1)
            if b2 == 0x8D then
                local b3 = start_addr:read(MEM_BYTE, offset + 2)
                if b3 == 0x05 then
                    -- Found LEA R8 instruction: 4C 8D 05 [disp32]
                    local displacement = start_addr:read(MEM_INT, offset + 3)
                    if displacement and displacement ~= 0 and math.abs(displacement) < 0x7FFFFFFF then
                        -- Resolve RIP: target = (inst_addr + 7) + displacement
                        return start_addr:add(offset + 7):add(displacement)
                    end
                end
            end
        end
    end
    return nil
end

local last_map_name = ""
local model_name_cache = {}

-- Resolve schema offsets
local function init_schemas()
    local source2 = modules.source2
    if not source2 then return false end
    m_pGameSceneNode = source2:get_schema("C_BaseEntity", "m_pGameSceneNode")
    m_modelState = source2:get_schema("CSkeletonInstance", "m_modelState")
    m_ModelName = source2:get_schema("CModelState", "m_ModelName")
    if not m_ModelName or m_ModelName <= 0 then
        m_ModelName = 0xA8
    end
    m_lifeState = source2:get_schema("C_BaseEntity", "m_lifeState")
    m_iTeamNum = source2:get_schema("C_BaseEntity", "m_iTeamNum")
    m_iHealth = source2:get_schema("C_BaseEntity", "m_iHealth")
    
    m_pWeaponServices = source2:get_schema("C_BasePlayerPawn", "m_pWeaponServices")
    m_hActiveWeapon = source2:get_schema("CPlayer_WeaponServices", "m_hActiveWeapon")
    m_bPinPulled = source2:get_schema("C_BaseCSGrenade", "m_bPinPulled")
    m_flThrowStrength = source2:get_schema("C_BaseCSGrenade", "m_flThrowStrength")
    
    m_vecAbsVelocity = source2:get_schema("C_BaseEntity", "m_vecAbsVelocity")
    if not m_vecAbsVelocity or m_vecAbsVelocity <= 0 then
        m_vecAbsVelocity = source2:get_schema("C_BaseEntity", "m_vecVelocity")
    end
    
    m_vInitialPosition = source2:get_schema("C_BaseCSGrenadeProjectile", "m_vInitialPosition")
    m_vInitialVelocity = source2:get_schema("C_BaseCSGrenadeProjectile", "m_vInitialVelocity")
    m_flSimulationTime = source2:get_schema("C_BaseEntity", "m_flSimulationTime")
    m_vSmokeDetonationPos = source2:get_schema("C_SmokeGrenadeProjectile", "m_vSmokeDetonationPos")
    m_vecAbsOrigin = source2:get_schema("CGameSceneNode", "m_vecAbsOrigin")
    m_pEntity = source2:get_schema("CEntityInstance", "m_pEntity")
    
    return m_pGameSceneNode ~= nil and m_modelState ~= nil
end

-- Find CS2 process ID
local function find_cs2_pid()
    local process = fantasy.engine.process()
    if not process then return nil end
    local proc_list = process:list()
    if not proc_list then return nil end
    for _, proc in pairs(proc_list) do
        if proc.name == "cs2" then
            return proc.id
        end
    end
    return nil
end

-- 2. Map POSIX Shared Memory segment
function bridge.initialize_shm()
    -- Attempt unlink first to clear any stale allocations
    rt.my_shm_unlink(SHM_NAME)

    bridge.shm_fd = rt.my_shm_open(SHM_NAME, O_CREAT + O_RDWR, tonumber("0666", 8))
    if bridge.shm_fd < 0 then
        fantasy.log("FC2 CHAMS: Failed to create POSIX shared memory segment")
        return false
    end

    C.my_ftruncate(bridge.shm_fd, SHM_SIZE)

    local addr = C.my_mmap(nil, SHM_SIZE, PROT_READ + PROT_WRITE, MAP_SHARED, bridge.shm_fd, 0)
    if addr == ffi.cast("void*", -1) then
        C.my_close(bridge.shm_fd)
        bridge.shm_fd = -1
        fantasy.log("FC2 CHAMS: Failed to map memory block")
        return false
    end

    bridge.shm_ptr = ffi.cast("struct shm_packet_t*", addr)
    bridge.shm_ptr.frame_index = 0
    bridge.shm_ptr.player_count = 0
    -- Open or create named POSIX semaphore for microsecond wakeup signaling
    rt.my_sem_unlink(SEM_NAME)
    bridge.shm_sem = rt.my_sem_open(SEM_NAME, O_CREAT, tonumber("0666", 8), 0)
    if bridge.shm_sem == SEM_FAILED then
        fantasy.log("FC2 CHAMS: WARNING - Failed to create POSIX semaphore, falling back to polling.")
        bridge.shm_sem = nil
    else
        fantasy.log("FC2 CHAMS: POSIX semaphore created for low-latency signaling.")
    end

    fantasy.log("FC2 CHAMS: Zero-Copy shared memory initialized successfully.")
    
    -- Cache client module for view matrix reading
    local process = fantasy.engine.process()
    if process then
        local client_name = "game/csgo/bin/linuxsteamrt64/libclient.so"
        bridge.client_module = process:get_module(client_name)
        if not bridge.client_module then
            fantasy.log("FC2 CHAMS: Warning - libclient.so could not be cached.")
        else
            fantasy.log("FC2 CHAMS: Cached libclient.so module base.")
            
            if bridge.use_pattern_scan then
                -- Resilient scan anchored on compiler warning log references to "./view.cpp" at line 851.
                -- Stage 1: Find the unique warning log instruction referencing "./view.cpp"
                -- Pattern: mov r9d, 1     | lea rsi, [rip + "./view.cpp"] | mov r8d, eax | mov edx, 851
                -- Bytes:   41 B9 01 00 00 00 | 48 8D 35 [disp32]            | 41 89 C0   | BA 53 03 00 00
                local sig1 = "41 B9 01 00 00 00 48 8D 35 ? ? ? ? 41 89 C0 BA 53 03 00 00"
                local target_addr = nil
                local found_seg = -1
                
                for seg_idx = 1, 6 do
                    local pattern_addr1 = process:pattern("libclient.so", sig1, seg_idx, true, false, 0)
                    if pattern_addr1 and pattern_addr1:is_valid() and not pattern_addr1:is_zero() then
                        local lea_inst_addr = pattern_addr1:add(6)
                        local raw_lea = to_raw_address(lea_inst_addr)
                        fantasy.log("FC2 CHAMS: Found Stage 1 log LEA in segment " .. seg_idx .. " at address 0x" .. string.format("%X", raw_lea))
                        
                        -- Stage 2: Scan forward from the log LEA for the first ViewMatrix LEA
                        local resolved_vm = scan_forward_for_view_matrix(lea_inst_addr)
                        if resolved_vm then
                            target_addr = resolved_vm
                            found_seg = seg_idx
                            break
                        else
                            fantasy.log("FC2 CHAMS: Failed to find Stage 2 forward from Stage 1 in segment " .. seg_idx)
                        end
                    end
                end

                if target_addr then
                    bridge.view_matrix_addr = target_addr
                    fantasy.log("FC2 CHAMS: Resolved absolute ViewMatrix address at segment " .. found_seg .. ": " .. tostring(target_addr))
                else
                    fantasy.log("FC2 CHAMS: Failed to resolve ViewMatrix signature scan. Check if signature is outdated.")
                end
            end
        end
    end

    init_schemas()
    bridge.cs2_pid = find_cs2_pid()
    if not bridge.cs2_pid then
        fantasy.log("FC2 CHAMS: CS2 Process ID not found. Chams mesh parsing will be disabled until CS2 runs.")
    else
        fantasy.log("FC2 CHAMS: Resolved CS2 process ID: " .. tostring(bridge.cs2_pid))
    end

    return true
end

-- 3. High-Speed Collection Loop
-- Helper: Query view matrix from the CS2 memory space or fallback client_module pointers
local function read_view_matrix()
    local matrix_read = false
    if bridge.cs2_pid then
        local target_addr = nil
        if bridge.view_matrix_addr then
            target_addr = to_raw_address(bridge.view_matrix_addr)
        elseif bridge.client_module then
            local fallback_offset = 0x3AA2F40
            local client_base = to_raw_address(bridge.client_module)
            if client_base then
                target_addr = client_base + fallback_offset
            end
        end
        if target_addr then
            matrix_read = read_raw_bytes(bridge.cs2_pid, target_addr, 64, local_packet.view_matrix)
        end
    end

    if not matrix_read then
        if bridge.view_matrix_addr then
            for i = 0, 15 do
                local_packet.view_matrix[i] = bridge.view_matrix_addr:read(MEM_FLOAT, i * 4)
            end
        elseif bridge.client_module then
            local fallback_offset = 0x3AA2F40
            for i = 0, 15 do
                local_packet.view_matrix[i] = bridge.client_module:read(MEM_FLOAT, fallback_offset + i * 4)
            end
        end
    end
end

-- Helper: Retrieve active map name, update SHM packet, and invalidate player model caches if map changes
local function update_map_name()
    local map_name = ""
    local globals = modules.source2 and modules.source2:get_globals()
    if globals and globals.map_directory and globals.map_directory ~= "<empty>" and globals.map_directory ~= "" then
        map_name = globals.map_directory:gsub("^.*[/\\]", "")
    end
    if map_name ~= last_map_name then
        last_map_name = map_name
        model_name_cache = {}
    end
    ffi.copy(local_packet.map_name, map_name, math.min(#map_name, 63))
    local_packet.map_name[math.min(#map_name, 63)] = 0
end

-- Helper: Update local player eye location in local packet
local function update_local_player(lp)
    local eye = lp and lp:get_eye_position()
    if eye then
        local_packet.local_eye.x = eye.x
        local_packet.local_eye.y = eye.y
        local_packet.local_eye.z = eye.z
    else
        local_packet.local_eye.x = 0
        local_packet.local_eye.y = 0
        local_packet.local_eye.z = 0
    end
end

-- Helper: Update local player held grenade state
local function update_held_grenade(lp)
    local_packet.held_grenade_type = 0
    local_packet.pin_pulled = 0
    local_packet.throw_strength = 0.0
    local_packet.local_velocity.x = 0.0
    local_packet.local_velocity.y = 0.0
    local_packet.local_velocity.z = 0.0

    if not lp then return end
    
    local pawn = lp:get_pawn()
    if not pawn or not pawn:is_valid() then return end
    
    if not m_pWeaponServices or not m_hActiveWeapon then return end
    
    local weapon_services = pawn:read(MEM_ADDRESS, m_pWeaponServices)
    if not weapon_services or not weapon_services:is_valid() then return end
    
    local success, weapon_handle = pcall(function()
        return weapon_services:read(MEM_ADDRESS, m_hActiveWeapon)
    end)
    if not success or not weapon_handle or not weapon_handle:is_valid() then return end
    
    local weapon = modules.entity_list:get_entity(weapon_handle.address)
    if not weapon then return end
    
    local _, class_name = weapon:get_class()
    local held_type = GRENADE_CLASS_MAP[class_name]
    
    if not held_type then
        local weapon_wrapped = players.to_weapon(weapon)
        if weapon_wrapped then
            local id = weapon_wrapped:get_id()
            held_type = GRENADE_ID_MAP[id]
        end
    end
    
    if held_type then
        local pin_pulled = false
        if m_bPinPulled then
            pin_pulled = weapon:read(MEM_BOOL, m_bPinPulled)
        end
        
        if pin_pulled then
            local_packet.held_grenade_type = held_type
            local_packet.pin_pulled = 1
            
            if m_flThrowStrength then
                local_packet.throw_strength = weapon:read(MEM_FLOAT, m_flThrowStrength) or 0.0
            end
            
            if m_vecAbsVelocity then
                local vel = pawn:read(MEM_VECTOR, m_vecAbsVelocity)
                if vel then
                    local_packet.local_velocity.x = vel.x
                    local_packet.local_velocity.y = vel.y
                    local_packet.local_velocity.z = vel.z
                end
            end
        end
    end
end

-- Helper: Retrieve and update active in-flight projectiles
local function collect_projectiles()
    local entities = modules.entity_list:get_entities()
    local count = 0
    
    if entities then
        for _, entity in pairs(entities) do
            if count >= 8 then break end
            if not entity then goto continue end
            
            local _, class_name = entity:get_class()
            local proj_type = PROJECTILE_CLASS_MAP[class_name]
            
            if proj_type then
                local proj_pkt = local_packet.projectiles[count]
                proj_pkt.active = 1
                proj_pkt.type = proj_type
                
                -- Resolve unique 32-bit entity handle (CHandle) from CEntityIdentity
                local entity_handle = 0
                local p_identity = entity:read(MEM_ADDRESS, m_pEntity or 0x10)
                if p_identity and p_identity:is_valid() then
                    entity_handle = p_identity:read(MEM_INT, 0x10) or 0
                else
                    entity_handle = entity:get_index() or 0
                end
                proj_pkt.entity_handle = entity_handle
                
                if m_vInitialPosition then
                    local pos = entity:read(MEM_VECTOR, m_vInitialPosition)
                    if pos then
                        proj_pkt.initial_position.x = pos.x
                        proj_pkt.initial_position.y = pos.y
                        proj_pkt.initial_position.z = pos.z
                    else
                        proj_pkt.initial_position.x = 0.0
                        proj_pkt.initial_position.y = 0.0
                        proj_pkt.initial_position.z = 0.0
                    end
                end
                
                if m_vInitialVelocity then
                    local vel = entity:read(MEM_VECTOR, m_vInitialVelocity)
                    if vel then
                        proj_pkt.initial_velocity.x = vel.x
                        proj_pkt.initial_velocity.y = vel.y
                        proj_pkt.initial_velocity.z = vel.z
                    else
                        proj_pkt.initial_velocity.x = 0.0
                        proj_pkt.initial_velocity.y = 0.0
                        proj_pkt.initial_velocity.z = 0.0
                    end
                end
                
                -- Read current origin via GameSceneNode -> m_vecAbsOrigin
                local current_origin = nil
                if m_pGameSceneNode and m_vecAbsOrigin then
                    pcall(function()
                        local scene_node = entity:read(MEM_ADDRESS, m_pGameSceneNode)
                        if scene_node and scene_node:is_valid() then
                            current_origin = scene_node:read(MEM_VECTOR, m_vecAbsOrigin)
                        end
                    end)
                end
                
                -- Fallback to detonation position
                if not current_origin and class_name == "C_SmokeGrenadeProjectile" and m_vSmokeDetonationPos then
                    current_origin = entity:read(MEM_VECTOR, m_vSmokeDetonationPos)
                end
                
                if current_origin then
                    proj_pkt.current_position.x = current_origin.x
                    proj_pkt.current_position.y = current_origin.y
                    proj_pkt.current_position.z = current_origin.z
                else
                    proj_pkt.current_position.x = proj_pkt.initial_position.x
                    proj_pkt.current_position.y = proj_pkt.initial_position.y
                    proj_pkt.current_position.z = proj_pkt.initial_position.z
                end
                
                local spawn_time = 0.0
                if m_flSimulationTime then
                    spawn_time = entity:read(MEM_FLOAT, m_flSimulationTime) or 0.0
                end
                if spawn_time == 0.0 then
                    local globals = modules.source2 and modules.source2:get_globals()
                    if globals and globals.curtime then
                        spawn_time = globals.curtime
                    else
                        spawn_time = fantasy.time() / 1000.0
                    end
                end
                proj_pkt.spawn_time = spawn_time
                
                count = count + 1
            end
            
            ::continue::
        end
    end
    
    for i = count, 7 do
        local_packet.projectiles[i].active = 0
    end
    
    local_packet.projectile_count = count
end

-- Helper: Traverses pointers to resolve dynamic model names from memory or retrieves from cache
local function resolve_model_name(pawn, pawn_addr, scene_node)
    local model_name = ""
    if pawn_addr then
        model_name = model_name_cache[pawn_addr]
    end

    if not model_name or model_name == "" then
        if not scene_node or not scene_node:is_valid() then
            scene_node = pawn:read(MEM_ADDRESS, m_pGameSceneNode)
        end
        if scene_node and scene_node:is_valid() then
            pcall(function()
                -- 1. Read Model Name via 0x210 display path
                local model_handle = scene_node:read(MEM_ADDRESS, 0x210)
                if model_handle and model_handle:is_valid() then
                    local cmodel = model_handle:read(MEM_ADDRESS, 0)
                    if cmodel and cmodel:is_valid() then
                        for off = 8, 0x180, 8 do
                            local p = cmodel:read(MEM_ADDRESS, off)
                            if p and p:is_valid() then
                                local s = p:read(MEM_STRING, 0, 128)
                                if s and #s >= 12 and #s <= 320 then
                                    local sl = s:lower()
                                    if sl:find("ctm_") or sl:find("tm_") or sl:find("characters/") or sl:find("models/") or sl:find("%.vmdl") then
                                        model_name = s
                                        break
                                    end
                                end
                            end
                        end
                    end
                end

                -- Fallback to model_state + 168 if display path failed
                if model_name == "" then
                    local model_state = scene_node:add(m_modelState)
                    if model_state and model_state:is_valid() then
                        local name_symbol = model_state:read(MEM_ADDRESS, 168)
                        if name_symbol and name_symbol:is_valid() then
                            local s = name_symbol:read(MEM_STRING, 0, 128)
                            if s and #s >= 12 and #s <= 320 then
                                local sl = s:lower()
                                if sl:find("ctm_") or sl:find("tm_") or sl:find("characters/") or sl:find("models/") or sl:find("%.vmdl") then
                                    model_name = s
                                end
                            end
                        end
                    end
                end
            end)

            if model_name ~= "" and pawn_addr then
                model_name_cache[pawn_addr] = model_name
            end
        end
    end

    return model_name, scene_node
end

-- Helper: Retrieve and populate raw bones from memory space (low-overhead process_vm_readv fallback to direct reads)
local function update_bone_cache(player_pkt, scene_node)
    player_pkt.bone_count = 0
    if scene_node and scene_node:is_valid() then
        pcall(function()
            local bone_cache_ptr = scene_node:read(MEM_ADDRESS, m_modelState + 0x80)
            if bone_cache_ptr and bone_cache_ptr:is_valid() then
                local bones_read = false
                if bridge.cs2_pid then
                    local raw_bone_addr = to_raw_address(bone_cache_ptr)
                    if raw_bone_addr then
                        bones_read = read_raw_bytes(bridge.cs2_pid, raw_bone_addr, 4096, raw_bones_buffer)
                    end
                end

                if bones_read then
                    player_pkt.bone_count = 128
                    for b = 0, 127 do
                        player_pkt.bones[b].position.x = raw_bones_buffer[b].pos[0]
                        player_pkt.bones[b].position.y = raw_bones_buffer[b].pos[1]
                        player_pkt.bones[b].position.z = raw_bones_buffer[b].pos[2]
                        player_pkt.bones[b].rotation.x = raw_bones_buffer[b].rot[0]
                        player_pkt.bones[b].rotation.y = raw_bones_buffer[b].rot[1]
                        player_pkt.bones[b].rotation.z = raw_bones_buffer[b].rot[2]
                        player_pkt.bones[b].rotation.w = raw_bones_buffer[b].rot[3]
                    end
                else
                    player_pkt.bone_count = 128
                    for b = 0, 127 do
                        local offset = b * 32
                        local pos = bone_cache_ptr:read(MEM_VECTOR, offset)
                        if pos then
                            player_pkt.bones[b].position.x = pos.x
                            player_pkt.bones[b].position.y = pos.y
                            player_pkt.bones[b].position.z = pos.z
                        else
                            player_pkt.bones[b].position.x = 0
                            player_pkt.bones[b].position.y = 0
                            player_pkt.bones[b].position.z = 0
                        end
                        player_pkt.bones[b].rotation.x = bone_cache_ptr:read(MEM_FLOAT, offset + 16) or 0
                        player_pkt.bones[b].rotation.y = bone_cache_ptr:read(MEM_FLOAT, offset + 20) or 0
                        player_pkt.bones[b].rotation.z = bone_cache_ptr:read(MEM_FLOAT, offset + 24) or 0
                        player_pkt.bones[b].rotation.w = bone_cache_ptr:read(MEM_FLOAT, offset + 28) or 1
                    end
                end
            end
        end)
    end
end

-- Helper: Retrieve details for all active enemy players
local function collect_active_players(lp, lp_team)
    local enemies = lp and modules.entity_list:get_enemies()
    local count = 0
    local active_pawns_this_frame = {}

    if enemies and m_pGameSceneNode and m_lifeState and m_iTeamNum and m_iHealth then
        for _, enemy_entity in pairs(enemies) do
            if count >= 64 then break end
            local enemy = players.to_player(enemy_entity)
            if enemy then
                local pawn = enemy:get_pawn()
                if pawn and pawn:is_valid() then
                    local life_state = pawn:read(MEM_BYTE, m_lifeState)
                    local health = pawn:read(MEM_INT, m_iHealth) or 0
                    if life_state == 0 and health > 0 then
                        local team = enemy_entity:read(MEM_INT, m_iTeamNum)
                        if not lp_team or team ~= lp_team then
                            local player_pkt = local_packet.players[count]
                            player_pkt.team = team
                            player_pkt.health = health
                            player_pkt.active = 1
                            player_pkt.has_defuser = enemy:has_defuser() and 1 or 0

                            local dims = enemy_entity:get_box_dimensions()
                            if dims and dims.origin then
                                player_pkt.origin.x = dims.origin.x
                                player_pkt.origin.y = dims.origin.y
                                player_pkt.origin.z = dims.origin.z
                            else
                                player_pkt.origin.x = 0
                                player_pkt.origin.y = 0
                                player_pkt.origin.z = 0
                            end

                            -- Read dynamic model name (from cache or pointer traversal)
                            local scene_node = nil
                            local pawn_addr = pawn.address
                            if pawn_addr then
                                active_pawns_this_frame[pawn_addr] = true
                            end
                            local model_name
                            model_name, scene_node = resolve_model_name(pawn, pawn_addr, scene_node)

                            -- Read Bone Cache
                            if not scene_node or not scene_node:is_valid() then
                                pcall(function()
                                    scene_node = pawn:read(MEM_ADDRESS, m_pGameSceneNode)
                                end)
                            end
                            update_bone_cache(player_pkt, scene_node)

                            -- Copy model name
                            ffi.copy(player_pkt.model_name, model_name, math.min(#model_name, 63))
                            player_pkt.model_name[math.min(#model_name, 63)] = 0

                            count = count + 1
                        end
                    end
                end
            end
        end
    end

    -- Mark remaining slots as inactive
    for i = count, 63 do
        local_packet.players[i].active = 0
    end

    local_packet.player_count = count

    -- Invalidate cache for any pawns that are no longer active
    for cached_addr, _ in pairs(model_name_cache) do
        if not active_pawns_this_frame[cached_addr] then
            model_name_cache[cached_addr] = nil
        end
    end
end

-- Helper: Render and write out structured debug log formatting
local function log_debug_info(current_time)
    last_log_time = current_time
    local parts = {}
    table.insert(parts, string.format("FC2 CHAMS Bridge Debug Log - Frame Index: %d", bridge.shm_ptr.frame_index))
    table.insert(parts, string.format("  Player Count: %d", bridge.shm_ptr.player_count))
    table.insert(parts, string.format("  Local Eye: [ %.4f, %.4f, %.4f ]",
        local_packet.local_eye.x, local_packet.local_eye.y, local_packet.local_eye.z))
    table.insert(parts, string.format("  Map Name: %s", ffi.string(local_packet.map_name)))
    
    if local_packet.held_grenade_type > 0 then
        table.insert(parts, string.format("  Held Grenade: Type: %d, Pin Pulled: %d, Throw Strength: %.3f, Velocity: [ %.3f, %.3f, %.3f ]",
            local_packet.held_grenade_type,
            local_packet.pin_pulled,
            local_packet.throw_strength,
            local_packet.local_velocity.x,
            local_packet.local_velocity.y,
            local_packet.local_velocity.z))
    else
        table.insert(parts, "  Held Grenade: None (or Pin Not Pulled)")
    end
    
    table.insert(parts, string.format("  Projectile Count: %d", local_packet.projectile_count))
    for i = 0, local_packet.projectile_count - 1 do
        local proj = local_packet.projectiles[i]
        table.insert(parts, string.format("    Projectile %d: Type: %d, Handle: %u (0x%08X), InitPos: [ %.3f, %.3f, %.3f ], CurrPos: [ %.3f, %.3f, %.3f ], Vel: [ %.3f, %.3f, %.3f ], Spawn Time: %.3f",
            i, proj.type, proj.entity_handle, proj.entity_handle,
            proj.initial_position.x, proj.initial_position.y, proj.initial_position.z,
            proj.current_position.x, proj.current_position.y, proj.current_position.z,
            proj.initial_velocity.x, proj.initial_velocity.y, proj.initial_velocity.z,
            proj.spawn_time))
    end
    
    table.insert(parts, "  View Matrix:")
    for r = 0, 3 do
        local idx = r * 4
        table.insert(parts, string.format("    [ %12.6f, %12.6f, %12.6f, %12.6f ]", 
            local_packet.view_matrix[idx],
            local_packet.view_matrix[idx + 1],
            local_packet.view_matrix[idx + 2],
            local_packet.view_matrix[idx + 3]))
    end

    for i = 0, bridge.shm_ptr.player_count - 1 do
        local p = bridge.shm_ptr.players[i]
        table.insert(parts, string.format("  Player %d:", i))
        table.insert(parts, string.format("    Team: %d, Health: %d, Active: %d, HasDefuser: %d", p.team, p.health, p.active, p.has_defuser))
        table.insert(parts, string.format("    Origin: [ %.4f, %.4f, %.4f ]", p.origin.x, p.origin.y, p.origin.z))
        table.insert(parts, string.format("    Model: %s", ffi.string(p.model_name)))
        table.insert(parts, string.format("    Bones sent: %d", p.bone_count))
    end
    
    fantasy.log("{}", table.concat(parts, "\n"))
end

-- 3. High-Speed Collection Loop
function bridge.on_worker(is_calibrated)
    if not is_calibrated or not bridge.enabled or not bridge.shm_ptr then
        return
    end

    -- Try to find CS2 PID if we don't have it yet
    if not bridge.cs2_pid then
        bridge.cs2_pid = find_cs2_pid()
    end

    -- Update views, map, and local player info
    read_view_matrix()
    update_map_name()

    local localplayer = modules.entity_list:get_localplayer()
    local lp = localplayer and players.to_player(localplayer)
    update_local_player(lp)
    
    local lp_team = lp and lp:get_team()
    collect_active_players(lp, lp_team)

    -- Update held grenade state & active projectiles
    update_held_grenade(lp)
    collect_projectiles()

    -- Seqlock: Signal renderer by incrementing frame index twice around write copy
    -- 1. Increment sequence counter to odd
    bridge.shm_ptr.frame_index = bit.bor(bridge.shm_ptr.frame_index + 1, 1)

    -- 2. Fast copy of all data fields (except frame_index)
    ffi.copy(bridge.shm_ptr.view_matrix, local_packet.view_matrix, SHM_SIZE - 4)

    -- 3. Increment sequence counter to even
    bridge.shm_ptr.frame_index = bridge.shm_ptr.frame_index + 1

    -- Wake up the C++ overlay client immediately via POSIX semaphore
    if bridge.shm_sem then
        rt.my_sem_post(bridge.shm_sem)
    end

    -- Debug logging every 5 seconds (only when debug flag is true)
    local current_time = fantasy.time()
    if bridge.debug and (current_time - last_log_time >= 200) then
        log_debug_info(current_time)
    end
end

function bridge.on_solution_calibrated(info)
    if info.gameid == GAME_CS2 and not bridge.shm_ptr then
        bridge.initialize_shm()
    end
end

function bridge.on_scripts_reloading()
    if bridge.shm_sem then
        rt.my_sem_close(bridge.shm_sem)
        bridge.shm_sem = nil
    end
    rt.my_sem_unlink(SEM_NAME)
    if bridge.shm_ptr then
        C.my_munmap(bridge.shm_ptr, SHM_SIZE)
        bridge.shm_ptr = nil
    end
    if bridge.shm_fd >= 0 then
        C.my_close(bridge.shm_fd)
        bridge.shm_fd = -1
    end
    rt.my_shm_unlink(SHM_NAME)
end

local proc = fantasy.engine.process()
if proc and proc:get_game() == GAME_CS2 and fantasy.is_calibrated() then
    bridge.initialize_shm()
end

return bridge
