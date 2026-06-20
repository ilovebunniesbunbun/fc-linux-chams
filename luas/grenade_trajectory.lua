--[[
    @title
        Grenade Trajectory v1.1

    @author
        rico

    @description
        Predicts and renders grenade trajectories.
        Shows the predicted path, bounce points, and detonation
        location for all grenade types while holding or after
        throwing. Includes real-time enemy impact prediction.
        Uses lib_raytrace for world collision
        detection and bounce simulation.

        Supports: HE, Flash, Smoke, Molotov/Incendiary, Decoy.

    @requires
        raytrace (lib_raytrace)
--]]
local modules   = require( "modules" )
local players   = require( "players" )
local keys      = require( "keys" )
local raytrace  = require( "raytrace" )

-- ============================================================
-- configuration
-- ============================================================
local grenade_trajectory = {
    enabled = true,

    -- trajectory line
    line_color       = "AAB0DCCB",
    line_thickness   = 2,
    line_gradient    = true,

    -- bounce markers
    show_bounces     = true,
    bounce_color     = "C3C8D7FF",
    bounce_size      = 2,

    -- detonation marker
    detonate_color   = "8C96EBFF",
    detonate_size    = 4,

    -- per-type colors
    per_type_colors  = false,
    color_he         = "BE8C8CC8",
    color_flash      = "C8C396C8",
    color_smoke      = "96B9A5C8",
    color_molotov    = "C39B82C8",
    color_decoy      = "A0A5B9C8",

    -- in-flight tracking
    track_in_flight  = true,
    local_only       = true,
    fade_duration    = 0.3,

    -- internal state
    cache = {
        gameid               = nil,
        schemas              = {},
        sv_gravity           = 800.0,
        was_holding          = false,
        last_throw_time      = 0,
        last_held_calc_time  = 0,
        held_affected_count  = 0,
        in_flight            = {},
        weapon_name          = "",
        rt_ready             = false,
    },
}

-- ============================================================
-- constants
-- ============================================================
local TICK_INTERVAL     = 1.0 / 64.0
local GRAVITY_SCALE     = 0.4
local ELASTICITY        = 0.45
local MAX_TICKS         = 1024
local TICKS_PER_POINT   = 4
local THROW_COOLDOWN    = 150               -- ms
local MISSING_GRACE     = 0.5               -- seconds before marking lost grenade as detonated
local BINARY_SEARCH_ITERS = 8               -- precision iterations for hit point finding
local SCAN_THROTTLE_MS  = 100               -- ms between enemy impact checks (Performance Saver)

-- damage radius
local RADIUS_HE_SQR = 350 * 350
local RADIUS_MOLOTOV_SQR = 150 * 150
local RADIUS_FLASH_SQR = 1000 * 1000

-- grenade weapon names
local NADE_HE      = "weapon_hegrenade"
local NADE_FLASH   = "weapon_flashbang"
local NADE_SMOKE   = "weapon_smokegrenade"
local NADE_MOLOTOV = "weapon_molotov"
local NADE_INC     = "weapon_incgrenade"
local NADE_DECOY   = "weapon_decoy"

-- grenade class names for entity scanning
local PROJECTILE_CLASSES = {
    ["C_HEGrenadeProjectile"]       = NADE_HE,
    ["C_FlashbangProjectile"]       = NADE_FLASH,
    ["C_SmokeGrenadeProjectile"]    = NADE_SMOKE,
    ["C_MolotovProjectile"]         = NADE_MOLOTOV,
    ["C_DecoyProjectile"]           = NADE_DECOY,
    -- base class fallback
    ["C_BaseCSGrenadeProjectile"]   = nil,
}

-- ============================================================
-- vector helpers
-- ============================================================
local function vec3( x, y, z )
    return vector:new( x or 0, y or 0, z or 0 )
end

local function vec_copy( v )
    return vec3( v.x, v.y, v.z )
end

local function vec_dot( a, b )
    return a.x * b.x + a.y * b.y + a.z * b.z
end

local function vec_length_sqr( v )
    return v.x * v.x + v.y * v.y + v.z * v.z
end

local function vec_length( v )
    return math.sqrt( vec_length_sqr( v ) )
end

local function vec_normalized( v )
    local len = vec_length( v )
    if len < 0.00001 then return vec3( 0, 0, 0 ) end
    return vec3( v.x / len, v.y / len, v.z / len )
end

local function vec_lerp( a, b, t )
    return vec3(
        a.x + ( b.x - a.x ) * t,
        a.y + ( b.y - a.y ) * t,
        a.z + ( b.z - a.z ) * t
    )
end

local function angle_to_directions( pitch, yaw )
    local p = math.rad( pitch )
    local y = math.rad( yaw )
    local sp, cp = math.sin( p ), math.cos( p )
    local sy, cy = math.sin( y ), math.cos( y )

    local forward = vec3( cp * cy, cp * sy, -sp )
    local right   = vec3( sy, -cy, 0 )
    local up      = vec3( sp * cy, sp * sy, cp )
    return forward, right, up
end

-- ============================================================
-- color helpers
-- ============================================================
local function hex_color( hex )
    return color:new( hex )
end

local function get_type_color( self, weapon_name )
    if not self.per_type_colors then
        return hex_color( self.line_color )
    end
    local map = {
        [NADE_HE]      = self.color_he,
        [NADE_FLASH]   = self.color_flash,
        [NADE_SMOKE]   = self.color_smoke,
        [NADE_MOLOTOV] = self.color_molotov,
        [NADE_INC]     = self.color_molotov,
        [NADE_DECOY]   = self.color_decoy,
    }
    return hex_color( map[weapon_name] or self.line_color )
end

-- ============================================================
-- schema helpers
-- ============================================================
local function get_schema( cache, class_name, field_name )
    local key = class_name .. "." .. field_name
    if not cache.schemas[key] then
        cache.schemas[key] = modules.source2:get_schema( class_name, field_name )
    end
    return cache.schemas[key]
end

-- ============================================================
-- enemy impact
-- ============================================================
local function count_affected_enemies( cache, detonation_pos, weapon_name, use_raytrace )
    if not use_raytrace or not detonation_pos or not weapon_name then return 0 end

    local radius_sqr, is_flash = 0, false
    if weapon_name == NADE_HE then radius_sqr = RADIUS_HE_SQR
    elseif weapon_name == NADE_MOLOTOV or weapon_name == NADE_INC then radius_sqr = RADIUS_MOLOTOV_SQR
    elseif weapon_name == NADE_FLASH then radius_sqr, is_flash = RADIUS_FLASH_SQR, true
    else return 0 end

    local lp_raw = modules.entity_list:get_localplayer()
    if not lp_raw then return 0 end
    local lp_pawn = players.to_player(lp_raw):get_pawn()
    if not lp_pawn then return 0 end
    local local_team = lp_pawn:read(MEM_INT, get_schema(cache, "C_BaseEntity", "m_iTeamNum"))
    if not local_team then return 0 end

    local affected_count = 0
    local entities = modules.entity_list:get_entities()
    if not entities then return 0 end

    for _, entity in pairs(entities) do
        if entity then
            local class_info = { entity:get_class() }
            if class_info[2] == "C_CSPlayerPawn" then
                local team = entity:read(MEM_INT, get_schema(cache, "C_BaseEntity", "m_iTeamNum"))
                local life_state = entity:read(MEM_BYTE, get_schema(cache, "C_BaseEntity", "m_lifeState"))

                if team and team ~= local_team and life_state == 0 then -- LIFE_ALIVE
                    local scene_node = entity:read(MEM_ADDRESS, get_schema(cache, "C_BaseEntity", "m_pGameSceneNode"))
                    if scene_node and scene_node:is_valid() then
                        local origin = scene_node:read(MEM_VECTOR, get_schema(cache, "CGameSceneNode", "m_vecAbsOrigin"))
                        local view_offset = entity:read(MEM_VECTOR, get_schema(cache, "C_BaseModelEntity", "m_vecViewOffset"))

                        if origin and view_offset then
                            local eye_pos = vec3(origin.x + view_offset.x, origin.y + view_offset.y, origin.z + view_offset.z)
                            if vec_length_sqr(vec3(eye_pos.x - detonation_pos.x, eye_pos.y - detonation_pos.y, eye_pos.z - detonation_pos.z)) <= radius_sqr then
                                if raytrace.is_visible_world(detonation_pos, eye_pos) then
                                    if is_flash then
                                        local angles = entity:read(MEM_VECTOR, get_schema(cache, "C_CSPlayerPawn", "v_angle"))
                                        if angles then
                                            local forward, _, _ = angle_to_directions(angles.x, angles.y)
                                            local to_nade = vec_normalized(vec3(detonation_pos.x - eye_pos.x, detonation_pos.y - eye_pos.y, detonation_pos.z - eye_pos.z))
                                            if vec_dot(forward, to_nade) > -0.5 then -- ~120 degree FOV check
                                                affected_count = affected_count + 1
                                            end
                                        end
                                    else
                                        affected_count = affected_count + 1
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
    end
    return affected_count
end

-- ============================================================
-- detonation logic
-- ============================================================
local function should_detonate( weapon_name, vel, tick )
    if weapon_name == NADE_SMOKE or weapon_name == NADE_DECOY then
        local speed_2d = math.sqrt( vel.x * vel.x + vel.y * vel.y )
        local threshold = weapon_name == NADE_DECOY and 0.2 or 0.1
        local check_ticks = math.floor( 0.2 / TICK_INTERVAL )
        if check_ticks < 1 then check_ticks = 1 end
        return speed_2d < threshold and ( tick % check_ticks ) == 0

    elseif weapon_name == NADE_MOLOTOV or weapon_name == NADE_INC then
        return tick * TICK_INTERVAL > 2.0

    elseif weapon_name == NADE_FLASH or weapon_name == NADE_HE then
        return ( tick - 8 ) * TICK_INTERVAL > 1.5
    end
    return false
end

-- ============================================================
-- raytrace collision: find hit point and approximate normal
-- ============================================================
local function trace_ray( start_pos, end_pos )
    if raytrace.is_visible_world( start_pos, end_pos ) then
        return { hit = false, end_pos = end_pos, fraction = 1.0, normal = nil }
    end

    local lo, hi = 0.0, 1.0
    for _ = 1, BINARY_SEARCH_ITERS do
        local mid = ( lo + hi ) * 0.5
        local mid_pos = vec_lerp( start_pos, end_pos, mid )
        if raytrace.is_visible_world( start_pos, mid_pos ) then
            lo = mid
        else
            hi = mid
        end
    end

    local fraction = lo
    local hit_pos = vec_lerp( start_pos, end_pos, fraction )

    local probe_dist = 2.0
    local normal = vec3( 0, 0, 0 )
    local probes = {
        { vec3( probe_dist, 0, 0 ), vec3( 1, 0, 0 ) },
        { vec3( -probe_dist, 0, 0 ), vec3( -1, 0, 0 ) },
        { vec3( 0, probe_dist, 0 ), vec3( 0, 1, 0 ) },
        { vec3( 0, -probe_dist, 0 ), vec3( 0, -1, 0 ) },
        { vec3( 0, 0, probe_dist ), vec3( 0, 0, 1 ) },
        { vec3( 0, 0, -probe_dist ), vec3( 0, 0, -1 ) },
    }

    for _, probe in ipairs( probes ) do
        local offset, dir = probe[1], probe[2]
        local probe_pos = vec3( hit_pos.x + offset.x, hit_pos.y + offset.y, hit_pos.z + offset.z )
        if raytrace.is_visible_world( probe_pos, hit_pos ) then
            normal = vec3( normal.x + dir.x, normal.y + dir.y, normal.z + dir.z )
        end
    end

    local n_len = vec_length( normal )
    if n_len < 0.001 then
        local move = vec3( end_pos.x - start_pos.x, end_pos.y - start_pos.y, end_pos.z - start_pos.z )
        normal = vec_normalized( vec3( -move.x, -move.y, -move.z ) )
    else
        normal = vec3( normal.x / n_len, normal.y / n_len, normal.z / n_len )
    end

    return { hit = true, end_pos = hit_pos, fraction = fraction, normal = normal }
end

-- ============================================================
-- physics: resolve bounce collision
-- ============================================================
local function resolve_collision( normal, vel )
    local total_elasticity = math.max( 0.0, math.min( ELASTICITY, 0.9 ) )
    local backoff = vec_dot( vel, normal ) * 2.0

    local new_vel = vec3(
        ( vel.x - normal.x * backoff ) * total_elasticity,
        ( vel.y - normal.y * backoff ) * total_elasticity,
        ( vel.z - normal.z * backoff ) * total_elasticity
    )

    if normal.z > 0.7 then
        local speed_sqr = vec_length_sqr( new_vel )
        if speed_sqr > 96000.0 then
            local nv_norm = vec_normalized( new_vel )
            local l = vec_dot( nv_norm, normal )
            if l > 0.5 then
                local scale = 1.5 - l
                new_vel = vec3( new_vel.x * scale, new_vel.y * scale, new_vel.z * scale )
            end
        end
        if speed_sqr < 400.0 then
            return vec3( 0, 0, 0 )
        end
    end

    return new_vel
end

-- ============================================================
-- physics: step one simulation tick with raytrace
-- ============================================================
local function step_simulation( pos, vel, gravity, use_raytrace )
    local new_vel_z = vel.z - gravity * TICK_INTERVAL

    local move = vec3(
        vel.x * TICK_INTERVAL,
        vel.y * TICK_INTERVAL,
        ( vel.z + new_vel_z ) * 0.5 * TICK_INTERVAL
    )

    local end_pos = vec3( pos.x + move.x, pos.y + move.y, pos.z + move.z )
    local new_vel = vec3( vel.x, vel.y, new_vel_z )

    local hit = false
    local hit_normal = nil

    if use_raytrace then
        local result = trace_ray( pos, end_pos )
        if result.hit then
            hit = true
            hit_normal = result.normal
            end_pos = result.end_pos
            new_vel = resolve_collision( hit_normal, new_vel )

            local remaining = 1.0 - result.fraction
            if remaining > 0.01 then
                local post_move = vec3(
                    new_vel.x * remaining * TICK_INTERVAL,
                    new_vel.y * remaining * TICK_INTERVAL,
                    new_vel.z * remaining * TICK_INTERVAL
                )
                local post_end = vec3(
                    end_pos.x + post_move.x,
                    end_pos.y + post_move.y,
                    end_pos.z + post_move.z
                )
                local post_trace = trace_ray( end_pos, post_end )
                end_pos = post_trace.end_pos
            end
        end
    end

    return end_pos, new_vel, hit, hit_normal
end

-- ============================================================
-- physics: full trajectory simulation
-- ============================================================
local function simulate_trajectory( origin, velocity, weapon_name, sv_gravity, use_raytrace )
    local gravity = ( sv_gravity or 800.0 ) * GRAVITY_SCALE
    local molotov_max_slope_z = math.cos( math.rad( 55.0 ) )

    local result = {
        points   = {},
        bounces  = {},
        end_pos  = nil,
        end_tick = -1,
        duration = 0,
        valid    = false,
    }

    local pos = vec_copy( origin )
    local vel = vec_copy( velocity )
    local bounce_count = 0
    local tick_timer = 0

    for tick = 0, MAX_TICKS - 1 do
        if tick_timer == 0 then
            table.insert( result.points, vec_copy( pos ) )
        end

        local new_pos, new_vel, hit, hit_normal = step_simulation( pos, vel, gravity, use_raytrace )
        pos = new_pos
        vel = new_vel

        if hit then
            bounce_count = bounce_count + 1
            table.insert( result.bounces, vec_copy( pos ) )

            local is_molotov = weapon_name == NADE_MOLOTOV or weapon_name == NADE_INC
            if is_molotov and hit_normal and hit_normal.z >= molotov_max_slope_z then
                result.end_tick = tick
                result.end_pos = vec_copy( pos )
                result.duration = tick * TICK_INTERVAL
                break
            end
        end

        local vel_stopped = math.abs( vel.x ) < 20.0
            and math.abs( vel.y ) < 20.0
            and vec_length_sqr( vel ) < 400.0

        if should_detonate( weapon_name, vel, tick ) or bounce_count > 20 or vel_stopped then
            result.end_tick = tick
            result.end_pos = vec_copy( pos )
            result.duration = tick * TICK_INTERVAL
            break
        end

        if hit or tick_timer + 1 >= TICKS_PER_POINT then
            tick_timer = 0
        else
            tick_timer = tick_timer + 1
        end
    end

    if #result.points > 0 and result.end_tick >= 0 and result.end_pos then
        local last = result.points[#result.points]
        local dx = last.x - result.end_pos.x
        local dy = last.y - result.end_pos.y
        local dz = last.z - result.end_pos.z
        if ( dx * dx + dy * dy + dz * dz ) > 1.0 then
            table.insert( result.points, vec_copy( result.end_pos ) )
        end
    end

    result.valid = result.end_tick >= 0
    return result
end

-- ============================================================
-- rendering
-- ============================================================
local function render_trajectory( self, traj, alpha, weapon_name, affected_count )
    if not traj.valid or #traj.points < 2 then return end

    local base_color = get_type_color( self, weapon_name )
    local total = #traj.points

    for i = 1, total - 1 do
        local s0 = modules.w2s( traj.points[i] )
        local s1 = modules.w2s( traj.points[i + 1] )

        if s0 and s1 then
            local t = ( i - 1 ) / ( total - 1 )
            local seg_alpha = self.line_gradient and alpha * ( 1.0 - t * 0.6 ) or alpha
            local a = math.floor( math.max( 0, math.min( 255, seg_alpha * base_color.a ) ) )

            modules.kernel:line( s0.x, s0.y, s1.x, s1.y, self.line_thickness, color:new( base_color.r, base_color.g, base_color.b, a ) )
        end
    end

    if self.show_bounces then
        local bc = hex_color( self.bounce_color )
        local sz = self.bounce_size
        local out_sz = sz + 1

        for _, bounce in ipairs( traj.bounces ) do
            local s = modules.w2s( bounce )
            if s then
                local a = math.floor( math.max( 0, math.min( 255, alpha * bc.a ) ) )
                modules.kernel:boxf( s.x - out_sz, s.y - out_sz, out_sz * 2, out_sz * 2, color:new( 0, 0, 0, a ) )
                modules.kernel:boxf( s.x - sz, s.y - sz, sz * 2, sz * 2, color:new( bc.r, bc.g, bc.b, a ) )
            end
        end
    end

    if traj.end_pos then
        local dc = hex_color( self.detonate_color )
        local sz = self.detonate_size
        local out_sz = sz + 1
        local s = modules.w2s( traj.end_pos )
        if s then
            local a = math.floor( math.max( 0, math.min( 255, alpha * dc.a ) ) )
            modules.kernel:boxf( s.x - out_sz, s.y - out_sz, out_sz * 2, out_sz * 2, color:new( 0, 0, 0, a ) )
            modules.kernel:boxf( s.x - sz, s.y - sz, sz * 2, sz * 2, color:new( dc.r, dc.g, dc.b, a ) )
            modules.kernel:text( string.format( "%.1fs", traj.duration ), 12, s.x + sz + 4, s.y - 6, color:new( dc.r, dc.g, dc.b, a ) )

            if affected_count and affected_count > 0 then
                local count_text = string.format("%d enemy(s)", affected_count)
                local text_color = color:new(255, 100, 100, a)
                modules.kernel:text(count_text, 12, s.x + sz + 4, s.y + 6, text_color)
            end
        end
    end
end

-- ============================================================
-- weapon identification
-- ============================================================
local function identify_weapon( cache, weapon_entity )
    if not weapon_entity then return nil end

    local subclass_offset = get_schema( cache, "C_BaseEntity", "m_nSubclassID" )
    if not subclass_offset then return nil end

    local vdata = weapon_entity:read( MEM_ADDRESS, subclass_offset + 0x8 )
    if not vdata or not vdata:is_valid() then return nil end

    local name_ptr = vdata:read( MEM_ADDRESS, get_schema( cache, "CCSWeaponBaseVData", "m_szName" ) )
    if not name_ptr or not name_ptr:is_valid() then return nil end

    local name = name_ptr:read( MEM_STRING, 0, 64 )
    if not name or #name == 0 then return nil end
    return name
end

local GRENADE_NAMES = {
    [NADE_HE] = true, [NADE_FLASH] = true, [NADE_SMOKE] = true,
    [NADE_MOLOTOV] = true, [NADE_INC] = true, [NADE_DECOY] = true,
}

local function is_grenade( name )
    return GRENADE_NAMES[name] == true
end

-- ============================================================
-- throw setup
-- ============================================================
local function setup_throw( cache, localplayer, pawn, weapon )
    local strength = 1.0
    local pin_pulled = weapon:read( MEM_BOOL, get_schema( cache, "C_BaseCSGrenade", "m_bPinPulled" ) )

    if pin_pulled then
        local raw = weapon:read( MEM_FLOAT, get_schema( cache, "C_BaseCSGrenade", "m_flThrowStrength" ) )
        if raw then
            strength = math.max( 0.0, math.min( 1.0, raw ) )
            if math.abs( strength - 0.5 ) <= 0.1 then
                strength = 0.5
            end
        end
    end

    local angles = modules.entity_list:get_viewangles()
    if not angles then return nil, nil end

    local pitch, yaw = angles.x, angles.y
    if pitch > 90.0 then pitch = pitch - 360.0
    elseif pitch < -90.0 then pitch = pitch + 360.0 end

    pitch = pitch - ( 90.0 - math.abs( pitch ) ) * 10.0 / 90.0

    local player_vel = pawn:read( MEM_VECTOR, get_schema( cache, "C_BaseEntity", "m_vecAbsVelocity" ) )
    if not player_vel then
        player_vel = vec3( 0, 0, 0 )
    elseif vec_length_sqr( player_vel ) < 1.0 then 
        player_vel = vec3( 0, 0, 0 )
    end

    local game_scene_node = pawn:read( MEM_ADDRESS, get_schema( cache, "C_BaseEntity", "m_pGameSceneNode" ) )
    if not game_scene_node or not game_scene_node:is_valid() then return nil, nil end

    local pawn_origin = game_scene_node:read( MEM_VECTOR, get_schema( cache, "CGameSceneNode", "m_vecAbsOrigin" ) )
    if not pawn_origin then return nil, nil end

    local view_offset = pawn:read( MEM_VECTOR, get_schema( cache, "C_BaseModelEntity", "m_vecViewOffset" ) )
    if not view_offset then return nil, nil end

    local eye_pos = vec3( pawn_origin.x + view_offset.x, pawn_origin.y + view_offset.y, pawn_origin.z + view_offset.z )

    local origin = vec3( eye_pos.x, eye_pos.y, eye_pos.z + strength * 12.0 - 12.0 )
    local forward = angle_to_directions( pitch, yaw )

    local trace_end = vec3(
        origin.x + forward.x * 22.0,
        origin.y + forward.y * 22.0,
        origin.z + forward.z * 22.0
    )

    if raytrace.is_loaded() and not raytrace.is_visible_world( origin, trace_end ) then
        local tr = trace_ray( origin, trace_end )
        origin = vec3(
            tr.end_pos.x - forward.x * 6.0,
            tr.end_pos.y - forward.y * 6.0,
            tr.end_pos.z - forward.z * 6.0
        )
    else
        origin = vec3(
            origin.x + forward.x * 16.0,
            origin.y + forward.y * 16.0,
            origin.z + forward.z * 16.0
        )
    end

    local throw_vel_table = {
        [NADE_HE]      = 750.0,
        [NADE_FLASH]   = 750.0,
        [NADE_SMOKE]   = 750.0,
        [NADE_MOLOTOV] = 750.0,
        [NADE_INC]     = 750.0,
        [NADE_DECOY]   = 750.0,
    }
    local throw_vel = throw_vel_table[cache.weapon_name] or 750.0
    local clamped = math.max( 15.0, math.min( 750.0, throw_vel * 0.9 ) )
    local speed = ( strength * 0.7 + 0.3 ) * clamped

    local velocity = vec3(
        forward.x * speed + player_vel.x * 1.25,
        forward.y * speed + player_vel.y * 1.25,
        forward.z * speed + player_vel.z * 1.25
    )

    return origin, velocity
end

-- ============================================================
-- in-flight grenade tracking
-- ============================================================
local function update_in_flight( cache, pawn, use_raytrace )
    if not grenade_trajectory.track_in_flight then return end

    local now = fantasy.time()
    local entities = modules.entity_list:get_entities()
    if not entities then return end

    local local_pawn_handle = nil
    if grenade_trajectory.local_only and pawn then
        local controller = modules.entity_list:get_localplayer()
        if controller then
            local_pawn_handle = controller:read( MEM_INT, get_schema( cache, "CCSPlayerController", "m_hPlayerPawn" ) )
        end
    end

    local alive_entities = {}

    for _, entity in pairs( entities ) do
        if not entity then goto continue end

        local class_info = { entity:get_class() }
        local class_name = class_info[2]
        if not class_name then goto continue end

        local weapon_name = PROJECTILE_CLASSES[class_name]
        if weapon_name == nil and not PROJECTILE_CLASSES[class_name] then
            goto continue
        end

        if not weapon_name then goto continue end

        if grenade_trajectory.local_only and local_pawn_handle then
            local thrower = entity:read( MEM_INT, get_schema( cache, "C_BaseGrenade", "m_hThrower" ) )
            if thrower and thrower ~= local_pawn_handle then
                goto continue
            end
        end

        local entity_addr = entity["address"] or tostring( entity )
        alive_entities[entity_addr] = true

        local existing = nil
        for i, g in ipairs( cache.in_flight ) do
            if g.entity_addr == entity_addr then
                existing = g
                break
            end
        end

        if existing then
            existing.last_seen = now

            if not existing.corrected then
                local init_pos = entity:read( MEM_VECTOR, get_schema( cache, "C_BaseCSGrenadeProjectile", "m_vInitialPosition" ) )
                local init_vel = entity:read( MEM_VECTOR, get_schema( cache, "C_BaseCSGrenadeProjectile", "m_vInitialVelocity" ) )

                if init_vel and vec_length_sqr( init_vel ) >= 1.0 then
                    existing.traj = simulate_trajectory( init_pos, init_vel, existing.weapon_name, cache.sv_gravity, use_raytrace )
                    existing.corrected = true
                end
            end
        else
            local init_pos = entity:read( MEM_VECTOR, get_schema( cache, "C_BaseCSGrenadeProjectile", "m_vInitialPosition" ) )
            local init_vel = entity:read( MEM_VECTOR, get_schema( cache, "C_BaseCSGrenadeProjectile", "m_vInitialVelocity" ) )

            if init_vel and vec_length_sqr( init_vel ) >= 1.0 then
                local traj = simulate_trajectory( init_pos, init_vel, weapon_name, cache.sv_gravity, use_raytrace )
                table.insert( cache.in_flight, {
                    entity_addr    = entity_addr,
                    weapon_name    = weapon_name,
                    throw_time     = now,
                    last_seen      = now,
                    last_calc_time = 0,
                    affected_count = 0,
                    corrected      = true,
                    detonated      = false,
                    detonate_time  = 0,
                    traj           = traj,
                })
            end
        end

        ::continue::
    end

    -- Process missing/detonated grenades and throttle raytrace calculations
    for _, g in ipairs( cache.in_flight ) do
        if not g.detonated and not alive_entities[g.entity_addr] then
            local missing_for = ( now - g.last_seen ) / 1000.0
            if missing_for >= MISSING_GRACE then
                g.detonated = true
                g.detonate_time = now
            end
        end
        
        -- Throttled calculation logic
        if not g.detonated and g.traj and g.traj.end_pos then
            if (now - g.last_calc_time) > SCAN_THROTTLE_MS then
                g.affected_count = count_affected_enemies(cache, g.traj.end_pos, g.weapon_name, use_raytrace)
                g.last_calc_time = now
            end
        end
    end

    local fade_ms = grenade_trajectory.fade_duration * 1000
    local new_list = {}
    for _, g in ipairs( cache.in_flight ) do
        if g.detonated then
            if ( now - g.detonate_time ) <= fade_ms or alive_entities[g.entity_addr] then
                table.insert( new_list, g )
            end
        else
            table.insert( new_list, g )
        end
    end
    cache.in_flight = new_list
end

-- Cleaned up rendering function (No heavy logic executed here)
local function render_in_flight( self, cache )
    local now = fantasy.time()
    local fade_ms = self.fade_duration * 1000

    for _, g in ipairs( cache.in_flight ) do
        if not g.traj or not g.traj.valid then goto continue end

        local alpha = 1.0

        if not g.detonated then
            local elapsed = ( now - g.throw_time ) / 1000.0
            if g.traj.duration > 0 and elapsed >= g.traj.duration then
                g.detonated = true
                g.detonate_time = now
            end
        end

        if g.detonated then
            local fade_elapsed = now - g.detonate_time
            alpha = math.max( 0.0, 1.0 - fade_elapsed / fade_ms )
        end

        if alpha > 0.0 then
            render_trajectory( self, g.traj, alpha, g.weapon_name, g.affected_count )
        end

        ::continue::
    end
end

-- ============================================================
-- callbacks
-- ============================================================
function grenade_trajectory.on_loaded()
    fantasy.log( "Grenade Trajectory: Loaded. Waiting for CS2 calibration..." )
end

function grenade_trajectory.on_solution_calibrated( data )
    grenade_trajectory.cache.gameid = data.gameid
    if data.gameid ~= GAME_CS2 then
        fantasy.log( "Grenade Trajectory: CS2 only." )
        return false
    end

    if raytrace.init() then
        grenade_trajectory.cache.rt_ready = true
        fantasy.log( "Grenade Trajectory: Raytrace initialized." )
    else
        fantasy.log( "Grenade Trajectory: Raytrace not available, using simplified physics." )
    end

    fantasy.log( "Grenade Trajectory: Ready." )
end

function grenade_trajectory.on_worker( is_calibrated, game_id )
    if not is_calibrated then return end
    if not grenade_trajectory.enabled then return end
    if grenade_trajectory.cache.gameid ~= GAME_CS2 then return end

    local cache = grenade_trajectory.cache

    local use_raytrace = false
    if cache.rt_ready then
        raytrace.update()
        use_raytrace = raytrace.is_loaded()
    end

    local lp_raw = modules.entity_list:get_localplayer()
    if not lp_raw then return end

    local localplayer = players.to_player( lp_raw )
    if not localplayer or not localplayer:is_alive() then return end

    local pawn = localplayer:get_pawn()
    if not pawn then return end

    -- -------------------------------------------------------
    -- in-flight grenade tracking
    -- -------------------------------------------------------
    update_in_flight( cache, pawn, use_raytrace )
    render_in_flight( grenade_trajectory, cache )

    -- -------------------------------------------------------
    -- active grenade prediction
    -- -------------------------------------------------------
    local weapon_services = pawn:read( MEM_ADDRESS, get_schema( cache, "C_BasePlayerPawn", "m_pWeaponServices" ) )
    if not weapon_services or not weapon_services:is_valid() then return end

    local weapon_handle = weapon_services:read( MEM_INT, get_schema( cache, "CPlayer_WeaponServices", "m_hActiveWeapon" ) )
    local weapon = nil
    local holding = false

    if weapon_handle and weapon_handle ~= 0 and weapon_handle ~= -1 then
        weapon = modules.entity_list:from_handle( weapon_handle )
        if weapon then
            local wname = identify_weapon( cache, weapon )
            if wname and is_grenade( wname ) then
                cache.weapon_name = wname

                local pin = weapon:read( MEM_BOOL, get_schema( cache, "C_BaseCSGrenade", "m_bPinPulled" ) )
                if pin then holding = true end

                local throw_t = weapon:read( MEM_FLOAT, get_schema( cache, "C_BaseCSGrenade", "m_fThrowTime" ) )
                if throw_t and throw_t > 0.0 then holding = false end
            end
        end
    end

    local now = fantasy.time()
    if cache.was_holding and not holding then
        cache.last_throw_time = now
    end
    cache.was_holding = holding

    if holding and weapon then
        local since_throw = now - ( cache.last_throw_time or 0 )
        if since_throw >= THROW_COOLDOWN or cache.last_throw_time == 0 then
            local origin, velocity = setup_throw( cache, localplayer, pawn, weapon )
            if origin and velocity then
                local traj = simulate_trajectory( origin, velocity, cache.weapon_name, cache.sv_gravity, use_raytrace )
                
                if traj.valid then
                    local affected_count = 0
                    if traj.end_pos then
                        -- Throttled calculation logic for currently held grenade
                        if (now - cache.last_held_calc_time) > SCAN_THROTTLE_MS then
                            cache.held_affected_count = count_affected_enemies(cache, traj.end_pos, cache.weapon_name, use_raytrace)
                            cache.last_held_calc_time = now
                        end
                        affected_count = cache.held_affected_count or 0
                    end
                    render_trajectory( grenade_trajectory, traj, 1.0, cache.weapon_name, affected_count )
                end
            end
        end
    end
end

return grenade_trajectory
