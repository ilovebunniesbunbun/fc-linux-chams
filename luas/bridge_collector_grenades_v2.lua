--[[
    @title
        FC2 Chams Shared Memory Collector Bridge (Grenade Trajectories & Warnings)
    @description
        Zero-GC data collector. Maps POSIX shared memory and streams 
        view matrices, skeletal bone arrays, held/in-flight grenades,
        and active ground infernos to the external renderer.
--]]

local ffi = require("ffi")
local bit = require("bit")
local modules = require("modules")
local players = require("players")

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
        float timer_start_time;
        float duration;
        uint8_t active;
    };

    struct inferno_data_t {
        uint32_t entity_handle;
        float start_time;
        float duration;
        int fire_count;
        struct vec3_t fire_positions[64];
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
        struct vec3_t local_angles;
        
        int projectile_count;
        struct in_flight_projectile_t projectiles[8];

        int inferno_count;
        struct inferno_data_t infernos[4];
        
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
local TICK_INTERVAL = 1.0 / 64.0

local last_log_time = 0

local bridge = {
    enabled = true,
    debug = false,
    debug_speed = 5000,
    use_pattern_scan = true,
    shm_fd = -1,
    shm_ptr = nil,
    shm_sem = nil,
    client_module = nil,
    view_matrix_addr = nil,
    cs2_pid = nil
}

-- Schema offsets
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

-- Grenade Warning & Inferno specific schemas
local m_bFireIsBurning = nil
local m_fireCount = nil
local m_firePositions = nil
local m_hOwnerEntity = nil
local m_hThrower = nil
local m_bIsIncGrenade = nil
local m_nExplodeEffectTickBegin = nil
local m_bDidSmokeEffect = nil
local m_nSmokeEffectTickBegin = nil
local m_nDecoyShotTick = nil

-- Grenade Type Lookup Maps
local GRENADE_CLASS_MAP = {
    C_HEGrenade = 1,
    C_Flashbang = 2,
    C_SmokeGrenade = 3,
    C_MolotovGrenade = 4,
    C_IncendiaryGrenade = 4,
    C_DecoyGrenade = 5,
}

local GRENADE_ID_MAP = {
    [43] = 2, -- Flashbang
    [44] = 1, -- HE
    [45] = 3, -- Smoke
    [46] = 4, -- Molotov
    [47] = 5, -- Decoy
    [48] = 4, -- Incendiary
}

local PROJECTILE_CLASS_MAP = {
    C_HEGrenadeProjectile = 1,
    C_FlashbangProjectile = 2,
    C_SmokeGrenadeProjectile = 3,
    C_MolotovProjectile = 4,
    C_DecoyProjectile = 5,
}

-- Reuse local structures to avoid allocations (Zero-GC)
local local_iov = ffi.new("struct iovec[1]")
local remote_iov = ffi.new("struct iovec[1]")
local raw_bones_buffer = ffi.new("struct { float pos[3]; float scale; float rot[4]; } [128]")
local local_packet = ffi.new("struct shm_packet_t")

-- State cache for warning countdowns and durations
local cache = {
    active_fires = {},   -- entity_addr -> start_time
    thrower_to_inc = {}, -- owner_handle -> is_incendiary
    active_decoys = {}   -- entity_addr -> start_tick
}

local function entity_key(entity)
    if entity.get_index then
        local idx = entity:get_index()
        if idx then return tostring(idx) end
    end
    local addr = rawget(entity, "address") or rawget(entity, "ptr") or rawget(entity, "handle")
    if addr then return tostring(addr) end
    return tostring(entity)
end

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
        if addr.address then return to_raw_address(addr.address) end
    end
    if type(addr) == "userdata" or type(addr) == "cdata" then
        local ok, val = pcall(ffi.cast, "uintptr_t", addr)
        if ok and val then return tonumber(val) end
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
                    local displacement = start_addr:read(MEM_INT, offset + 3)
                    if displacement and displacement ~= 0 and math.abs(displacement) < 0x7FFFFFFF then
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

    -- Inferno & Timer warning specific schemas
    m_bFireIsBurning = source2:get_schema("C_Inferno", "m_bFireIsBurning")
    m_fireCount = source2:get_schema("C_Inferno", "m_fireCount")
    m_firePositions = source2:get_schema("C_Inferno", "m_firePositions")
    m_hOwnerEntity = source2:get_schema("C_BaseEntity", "m_hOwnerEntity")
    m_hThrower = source2:get_schema("C_BaseGrenade", "m_hThrower")
    m_bIsIncGrenade = source2:get_schema("C_MolotovProjectile", "m_bIsIncGrenade")
    m_nExplodeEffectTickBegin = source2:get_schema("C_BaseCSGrenadeProjectile", "m_nExplodeEffectTickBegin")
    m_bDidSmokeEffect = source2:get_schema("C_SmokeGrenadeProjectile", "m_bDidSmokeEffect")
    m_nSmokeEffectTickBegin = source2:get_schema("C_SmokeGrenadeProjectile", "m_nSmokeEffectTickBegin")
    m_nDecoyShotTick = source2:get_schema("C_DecoyProjectile", "m_nDecoyShotTick")
    
    return m_pGameSceneNode ~= nil and m_modelState ~= nil
end

local function find_cs2_pid()
    local process = fantasy.engine.process()
    if not process then return nil end
    local proc_list = process:list()
    if not proc_list then return nil end
    for _, proc in pairs(proc_list) do
        if proc.name == "cs2" then return proc.id end
    end
    return nil
end

function bridge.initialize_shm()
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
    bridge.shm_ptr.projectile_count = 0
    bridge.shm_ptr.inferno_count = 0

    rt.my_sem_unlink(SEM_NAME)
    bridge.shm_sem = rt.my_sem_open(SEM_NAME, O_CREAT, tonumber("0666", 8), 0)
    if bridge.shm_sem == SEM_FAILED then
        fantasy.log("FC2 CHAMS: WARNING - Failed to create POSIX semaphore, falling back to polling.")
        bridge.shm_sem = nil
    else
        fantasy.log("FC2 CHAMS: POSIX semaphore created for low-latency signaling.")
    end

    fantasy.log("FC2 CHAMS: Zero-Copy shared memory initialized successfully.")
    
    local process = fantasy.engine.process()
    if process then
        local client_name = "game/csgo/bin/linuxsteamrt64/libclient.so"
        bridge.client_module = process:get_module(client_name)
        if not bridge.client_module then
            fantasy.log("FC2 CHAMS: Warning - libclient.so could not be cached.")
        else
            fantasy.log("FC2 CHAMS: Cached libclient.so module base.")
            if bridge.use_pattern_scan then
                local sig1 = "41 B9 01 00 00 00 48 8D 35 ? ? ? ? 41 89 C0 BA 53 03 00 00"
                local target_addr = nil
                local found_seg = -1
                for seg_idx = 1, 6 do
                    local pattern_addr1 = process:pattern("libclient.so", sig1, seg_idx, true, false, 0)
                    if pattern_addr1 and pattern_addr1:is_valid() and not pattern_addr1:is_zero() then
                        local lea_inst_addr = pattern_addr1:add(6)
                        local raw_lea = to_raw_address(lea_inst_addr)
                        fantasy.log("FC2 CHAMS: Found Stage 1 log LEA in segment " .. seg_idx .. " at address 0x" .. string.format("%X", raw_lea))
                        local resolved_vm = scan_forward_for_view_matrix(lea_inst_addr)
                        if resolved_vm then
                            target_addr = resolved_vm
                            found_seg = seg_idx
                            break
                        end
                    end
                end
                if target_addr then
                    bridge.view_matrix_addr = target_addr
                    fantasy.log("FC2 CHAMS: Resolved absolute ViewMatrix address at segment " .. found_seg .. ": " .. tostring(target_addr))
                end
            end
        end
    end

    init_schemas()
    bridge.cs2_pid = find_cs2_pid()
    if bridge.cs2_pid then
        fantasy.log("FC2 CHAMS: Resolved CS2 process ID: " .. tostring(bridge.cs2_pid))
    end
    return true
end

local function read_view_matrix()
    local matrix_read = false
    if bridge.cs2_pid then
        local target_addr = nil
        if bridge.view_matrix_addr then
            target_addr = to_raw_address(bridge.view_matrix_addr)
        elseif bridge.client_module then
            local fallback_offset = 0x3AA2F40
            local client_base = to_raw_address(bridge.client_module)
            if client_base then target_addr = client_base + fallback_offset end
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

local function update_map_name()
    local map_name = ""
    local globals = modules.source2 and modules.source2:get_globals()
    if globals and globals.map_directory and globals.map_directory ~= "<empty>" and globals.map_directory ~= "" then
        map_name = globals.map_directory:gsub("^.*[/\\]", "")
    end
    if map_name ~= last_map_name then
        last_map_name = map_name
        model_name_cache = {}
        cache.active_fires = {}
        cache.thrower_to_inc = {}
        cache.active_decoys = {}
    end
    ffi.copy(local_packet.map_name, map_name, math.min(#map_name, 63))
    local_packet.map_name[math.min(#map_name, 63)] = 0
end

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

-- Update local player held grenade state (Using 32-bit handle resolution)
local function update_held_grenade(lp)
    local_packet.held_grenade_type = 0
    local_packet.pin_pulled = 0
    local_packet.throw_strength = 0.0
    local_packet.local_velocity.x = 0.0
    local_packet.local_velocity.y = 0.0
    local_packet.local_velocity.z = 0.0
    local_packet.local_angles.x = 0.0
    local_packet.local_angles.y = 0.0
    local_packet.local_angles.z = 0.0

    if not lp then return end
    
    local pawn = lp:get_pawn()
    if not pawn or not pawn:is_valid() then return end
    
    if not m_pWeaponServices or not m_hActiveWeapon then return end
    
    local weapon_services = pawn:read(MEM_ADDRESS, m_pWeaponServices)
    if not weapon_services or not weapon_services:is_valid() then return end
    
    local success, weapon_handle = pcall(function()
        return weapon_services:read(MEM_INT, m_hActiveWeapon)
    end)
    if not success or not weapon_handle or weapon_handle == 0 or weapon_handle == -1 then return end
    
    local weapon = modules.entity_list:from_handle(weapon_handle)
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
        if m_bPinPulled then pin_pulled = weapon:read(MEM_BOOL, m_bPinPulled) end
        
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

            local angles = modules.entity_list:get_viewangles()
            if angles then
                local_packet.local_angles.x = angles.x
                local_packet.local_angles.y = angles.y
                local_packet.local_angles.z = angles.z
            end
        end
    end
end

-- Retrieve and update active in-flight projectiles with timers
local function collect_projectiles()
    local entities = modules.entity_list:get_entities()
    local count = 0
    local curtime = nil
    local globals = modules.source2 and modules.source2:get_globals()
    if globals then
        if globals.tick_count and globals.tick_count > 0 then
            curtime = globals.tick_count * 0.015625
        else
            curtime = globals.curtime
        end
    end
    if not curtime then
        curtime = fantasy.time() / 1000.0
    end

    local seen_decoys = {}
    
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
                
                local entity_handle = 0
                local p_identity = entity:read(MEM_ADDRESS, m_pEntity or 0x10)
                if p_identity and p_identity:is_valid() then
                    entity_handle = p_identity:read(MEM_INT, 0x10) or 0
                else
                    entity_handle = entity:get_index() or 0
                end
                
                -- Filter out invalid handles (entry index 0xFFFF or 0x7FFF)
                local ent_index = bit.band(entity_handle, 0xFFFF)
                if entity_handle == 0 or ent_index == 0xFFFF or ent_index == 0x7FFF then
                    goto continue
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
                
                local current_origin = nil
                if m_pGameSceneNode and m_vecAbsOrigin then
                    pcall(function()
                        local scene_node = entity:read(MEM_ADDRESS, m_pGameSceneNode)
                        if scene_node and scene_node:is_valid() then
                            current_origin = scene_node:read(MEM_VECTOR, m_vecAbsOrigin)
                        end
                    end)
                end
                
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
                    spawn_time = curtime
                end
                proj_pkt.spawn_time = spawn_time

                -- Resolve Timers (Smoke bloom, Decoy shooting)
                proj_pkt.timer_start_time = 0.0
                proj_pkt.duration = 0.0

                if class_name == "C_SmokeGrenadeProjectile" and m_bDidSmokeEffect and m_nSmokeEffectTickBegin then
                    local smoke_active = entity:read(MEM_BOOL, m_bDidSmokeEffect)
                    if smoke_active then
                        local smoke_tick = entity:read(MEM_INT, m_nSmokeEffectTickBegin)
                        if smoke_tick and smoke_tick > 0 then
                            proj_pkt.timer_start_time = smoke_tick * TICK_INTERVAL
                            proj_pkt.duration = 18.0
                        end
                    end
                elseif class_name == "C_DecoyProjectile" and m_nDecoyShotTick then
                    local decoy_tick = entity:read(MEM_INT, m_nDecoyShotTick)
                    if decoy_tick and decoy_tick > 0 then
                        local entity_addr = entity_key(entity)
                        seen_decoys[entity_addr] = true
                        if not cache.active_decoys[entity_addr] then
                            cache.active_decoys[entity_addr] = decoy_tick
                        end
                        proj_pkt.timer_start_time = cache.active_decoys[entity_addr] * TICK_INTERVAL
                        proj_pkt.duration = 15.0
                    end
                elseif class_name == "C_MolotovProjectile" and m_hThrower and m_bIsIncGrenade then
                    -- Map Molotov thrower handle to Incendiary type check
                    local thrower_handle = entity:read(MEM_INT, m_hThrower)
                    if thrower_handle and thrower_handle > 0 then
                        local is_inc = entity:read(MEM_BOOL, m_bIsIncGrenade)
                        cache.thrower_to_inc[thrower_handle] = is_inc
                    end
                end
                
                count = count + 1
            end
            ::continue::
        end
    end
    
    for i = count, 7 do
        local_packet.projectiles[i].active = 0
    end
    local_packet.projectile_count = count

    -- Prune Decoy cache
    local new_decoys = {}
    for addr, tick in pairs(cache.active_decoys) do
        if seen_decoys[addr] then
            new_decoys[addr] = tick
        end
    end
    cache.active_decoys = new_decoys
end

-- Collect active C_Inferno ground fire instances
local function collect_infernos()
    local entities = modules.entity_list:get_entities()
    local count = 0
    local curtime = nil
    local globals = modules.source2 and modules.source2:get_globals()
    if globals then
        if globals.tick_count and globals.tick_count > 0 then
            curtime = globals.tick_count * 0.015625
        else
            curtime = globals.curtime
        end
    end
    if not curtime then
        curtime = fantasy.time() / 1000.0
    end

    local seen_fires = {}

    if entities and m_bFireIsBurning and m_fireCount and m_firePositions then
        for _, entity in pairs(entities) do
            if count >= 4 then break end
            if not entity then goto continue end

            local _, class_name = entity:get_class()
            if class_name == "C_Inferno" then
                local entity_addr = entity_key(entity)
                seen_fires[entity_addr] = true

                local is_burning = entity:read(MEM_BOOL, m_bFireIsBurning)
                if not is_burning then goto continue end

                local inf_pkt = local_packet.infernos[count]
                inf_pkt.active = 1
                
                local entity_handle = 0
                local p_identity = entity:read(MEM_ADDRESS, m_pEntity or 0x10)
                if p_identity and p_identity:is_valid() then
                    entity_handle = p_identity:read(MEM_INT, 0x10) or 0
                else
                    entity_handle = entity:get_index() or 0
                end
                
                -- Filter out invalid handles (entry index 0xFFFF or 0x7FFF)
                local ent_index = bit.band(entity_handle, 0xFFFF)
                if entity_handle == 0 or ent_index == 0xFFFF or ent_index == 0x7FFF then
                    goto continue
                end
                inf_pkt.entity_handle = entity_handle

                -- Determine Incendiary vs Molotov duration
                local is_inc = false
                local owner_handle = entity:read(MEM_INT, m_hOwnerEntity)
                if owner_handle and owner_handle > 0 then
                    is_inc = cache.thrower_to_inc[owner_handle] == true
                end
                inf_pkt.duration = is_inc and 5.5 or 7.0

                -- Cache starting time
                if not cache.active_fires[entity_addr] then
                    cache.active_fires[entity_addr] = curtime
                end
                inf_pkt.start_time = cache.active_fires[entity_addr]

                -- Read active fire flame coordinates
                local fire_count = entity:read(MEM_INT, m_fireCount) or 0
                inf_pkt.fire_count = math.min(fire_count, 64)
                for i = 0, inf_pkt.fire_count - 1 do
                    local fp = entity:read(MEM_VECTOR, m_firePositions + (i * 12))
                    if fp then
                        inf_pkt.fire_positions[i].x = fp.x
                        inf_pkt.fire_positions[i].y = fp.y
                        inf_pkt.fire_positions[i].z = fp.z
                    else
                        inf_pkt.fire_positions[i].x = 0
                        inf_pkt.fire_positions[i].y = 0
                        inf_pkt.fire_positions[i].z = 0
                    end
                end

                count = count + 1
            end
            ::continue::
        end
    end

    for i = count, 3 do
        local_packet.infernos[i].active = 0
    end
    local_packet.inferno_count = count

    -- Prune active fires cache
    local new_fires = {}
    for addr, start_t in pairs(cache.active_fires) do
        if seen_fires[addr] then
            new_fires[addr] = start_t
        end
    end
    cache.active_fires = new_fires
end

local function resolve_model_name(pawn, pawn_addr, scene_node)
    local model_name = ""
    if pawn_addr then model_name = model_name_cache[pawn_addr] end

    if not model_name or model_name == "" then
        if not scene_node or not scene_node:is_valid() then
            scene_node = pawn:read(MEM_ADDRESS, m_pGameSceneNode)
        end
        if scene_node and scene_node:is_valid() then
            pcall(function()
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

                            local scene_node = nil
                            local pawn_addr = pawn.address
                            if pawn_addr then active_pawns_this_frame[pawn_addr] = true end
                            
                            local model_name
                            model_name, scene_node = resolve_model_name(pawn, pawn_addr, scene_node)

                            if not scene_node or not scene_node:is_valid() then
                                pcall(function() scene_node = pawn:read(MEM_ADDRESS, m_pGameSceneNode) end)
                            end
                            update_bone_cache(player_pkt, scene_node)

                            ffi.copy(player_pkt.model_name, model_name, math.min(#model_name, 63))
                            player_pkt.model_name[math.min(#model_name, 63)] = 0

                            count = count + 1
                        end
                    end
                end
            end
        end
    end

    for i = count, 63 do
        local_packet.players[i].active = 0
    end
    local_packet.player_count = count

    for cached_addr, _ in pairs(model_name_cache) do
        if not active_pawns_this_frame[cached_addr] then
            model_name_cache[cached_addr] = nil
        end
    end
end

local function log_debug_info(current_time)
    last_log_time = current_time
    local parts = {}
    local curtime
    local globals = modules.source2 and modules.source2:get_globals()
    if globals then
        if globals.tick_count and globals.tick_count > 0 then
            curtime = globals.tick_count * 0.015625
        else
            curtime = globals.curtime
        end
    end
    if not curtime then
        curtime = current_time / 1000.0
    end

    table.insert(parts, string.format("FC2 GRENADES Bridge Debug - Frame Index: %d", bridge.shm_ptr.frame_index))
    table.insert(parts, string.format("  Player Count: %d | Projectile Count: %d | Inferno Count: %d", 
        bridge.shm_ptr.player_count, bridge.shm_ptr.projectile_count, bridge.shm_ptr.inferno_count))
    table.insert(parts, string.format("  Local Eye: [ %.4f, %.4f, %.4f ]",
        local_packet.local_eye.x, local_packet.local_eye.y, local_packet.local_eye.z))
    table.insert(parts, string.format("  Map Name: %s", ffi.string(local_packet.map_name)))
    
    if local_packet.held_grenade_type > 0 then
        table.insert(parts, string.format("  Held Grenade: Type: %d, Pin Pulled: %d, Throw Strength: %.3f, Velocity: [ %.3f, %.3f, %.3f ], Angles: [ %.4f, %.4f, %.4f ]",
            local_packet.held_grenade_type, local_packet.pin_pulled, local_packet.throw_strength,
            local_packet.local_velocity.x, local_packet.local_velocity.y, local_packet.local_velocity.z,
            local_packet.local_angles.x, local_packet.local_angles.y, local_packet.local_angles.z))
    else
        table.insert(parts, "  Held Grenade: None (or Pin Not Pulled)")
    end
    
    for i = 0, local_packet.projectile_count - 1 do
        local proj = local_packet.projectiles[i]
        local time_left = 0.0
        if proj.timer_start_time > 0 then
            time_left = math.max(0.0, proj.duration - (curtime - proj.timer_start_time))
        end
        table.insert(parts, string.format("    Proj %d: Type: %d, Handle: %u (0x%08X), InitPos: [ %.3f, %.3f, %.3f ], CurrPos: [ %.3f, %.3f, %.3f ], Vel: [ %.3f, %.3f, %.3f ], Spawn Time: %.3f, TimerStart: %.3f, TimeLeft: %.2f (Max: %.1f)",
            i, proj.type, proj.entity_handle, proj.entity_handle,
            proj.initial_position.x, proj.initial_position.y, proj.initial_position.z,
            proj.current_position.x, proj.current_position.y, proj.current_position.z,
            proj.initial_velocity.x, proj.initial_velocity.y, proj.initial_velocity.z,
            proj.spawn_time, proj.timer_start_time, time_left, proj.duration))
    end

    for i = 0, local_packet.inferno_count - 1 do
        local inf = local_packet.infernos[i]
        local time_left = 0.0
        if inf.start_time > 0 then
            time_left = math.max(0.0, inf.duration - (curtime - inf.start_time))
        end
        table.insert(parts, string.format("    Inferno %d: Handle: %u (0x%08X), Start: %.3f, TimeLeft: %.2f (Max: %.1f), Flames: %d",
            i, inf.entity_handle, inf.entity_handle, inf.start_time, time_left, inf.duration, inf.fire_count))
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

function bridge.on_worker(is_calibrated)
    if not is_calibrated or not bridge.enabled or not bridge.shm_ptr then return end

    if not bridge.cs2_pid then
        bridge.cs2_pid = find_cs2_pid()
    end

    read_view_matrix()
    update_map_name()

    local localplayer = modules.entity_list:get_localplayer()
    local lp = localplayer and players.to_player(localplayer)
    update_local_player(lp)
    
    local lp_team = lp and lp:get_team()
    collect_active_players(lp, lp_team)

    update_held_grenade(lp)
    collect_projectiles()
    collect_infernos()

    -- Seqlock: Write to Shared Memory
    bridge.shm_ptr.frame_index = bit.bor(bridge.shm_ptr.frame_index + 1, 1)
    ffi.copy(bridge.shm_ptr.view_matrix, local_packet.view_matrix, SHM_SIZE - 4)
    bridge.shm_ptr.frame_index = bridge.shm_ptr.frame_index + 1

    if bridge.shm_sem then
        rt.my_sem_post(bridge.shm_sem)
    end

    local current_time = fantasy.time()
    if bridge.debug and (current_time - last_log_time >= 100) then
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
