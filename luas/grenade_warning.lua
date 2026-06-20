 --[[
    @title
        Grenade warner

    @author
        rico

    @description
        Grenade ESP and fire zone visualizer for CS2.

        Renders all grenade projectiles (HE, Flash, Smoke, Molotov,
        Decoy) with type icons, name labels, and per-type coloring.
        Smoke, decoy, and inferno have countdown timer bars.

        Tracks active C_Inferno entities and draws a filled convex
        hull polygon around the fire zone using a scanline rasterizer.

        Inferno Duration Detection: Scans in-flight C_MolotovProjectile
        entities for m_bIsIncGrenade and maps it to the thrower.
        When C_Inferno spawns, it correctly applies 5.5s or 7.0s.
--]]

local modules       = require( "modules" )
local extended_draw = require( "extended_draw" )



-- ============================================================
-- configuration
-- ============================================================
local grenade_warner = {
    enabled = true,

    -- global rendering
    font_size         = 12,
    outline           = true,
    outline_color     = "000000FF",

    -- projectile toggles
    show_icon         = true,
    show_name         = true,
    show_timer_bar    = true,
    icon_size         = 20,             -- weapon PNG icon size in pixels

    -- per-type colors
    color_he          = "FF4444FF",     -- bright red
    color_flash       = "FFD633FF",     -- vivid yellow
    color_smoke       = "33CC77FF",     -- vivid green
    color_molotov     = "FF8C1AFF",     -- vivid orange
    color_decoy       = "6699CCFF",     -- sky blue
    color_default     = "B0B8C8FF",     -- light gray

    -- timer bar
    timer_high_color  = "8C96EBFF",     -- blue-purple (full)
    timer_low_color   = "DC6464FF",     -- red (empty)
    bar_background    = "0F1016AA",     -- dark bg
    bar_width         = 30,
    bar_height        = 3,
    timer_font_size   = 10,

    -- fire polygon (inferno)
    show_fire_polygon    = true,
    fire_outline_color   = "D25000B4",  -- 210, 80, 0, 180
    fire_outline_thick   = 2,
    fire_fill_color      = "FF96003C",  -- 255, 150, 0, 60
    fire_padding         = 45.0,

    -- fire center timer text
    fire_text_color      = "FFFFFFFF",
    fire_text_shadow     = "000000C8",

    -- detonation filtering
    hide_detonated       = true,        -- hide HE/Flash after they explode


    -- grenade durations (seconds)
    duration_smoke            = 18.0,
    duration_decoy            = 15.0,
    duration_inferno_molotov  = 7.0,
    duration_inferno_inc      = 5.5,
}

-- ============================================================
-- internal runtime state (not exposed to configuration)
-- ============================================================
local cache = {
    gameid          = nil,
    schemas         = {},
    active_fires    = {},
    active_smokes   = {},
    active_decoys   = {},
    thrower_to_inc  = {},               -- maps thrower handle -> boolean (is_incendiary)
    weapon_icons    = {},               -- image_name -> image_id
}

-- ============================================================
-- constants
-- ============================================================
local TICK_INTERVAL = 1.0 / 64.0

-- projectile class names
local CLASS_HE      = "C_HEGrenadeProjectile"
local CLASS_FLASH   = "C_FlashbangProjectile"
local CLASS_SMOKE   = "C_SmokeGrenadeProjectile"
local CLASS_MOLOTOV = "C_MolotovProjectile"
local CLASS_DECOY   = "C_DecoyProjectile"
local CLASS_INFERNO = "C_Inferno"

local PROJECTILE_TYPE = {
    [CLASS_HE]      = "he",
    [CLASS_FLASH]   = "flash",
    [CLASS_SMOKE]   = "smoke",
    [CLASS_MOLOTOV] = "molotov",
    [CLASS_DECOY]   = "decoy",
    [CLASS_INFERNO] = "inferno",
}

-- display names
local DISPLAY_NAMES = {
    he       = "HE",
    flash    = "FLASH",
    smoke    = "SMOKE",
    molotov  = "MOLOTOV",
    decoy    = "DECOY",
    inferno  = "FIRE",
}

-- weapon PNG image names (must match image cache keys)
local IMAGE_NAMES = {
    he       = "weapon_hegrenade",
    flash    = "weapon_flashbang",
    smoke    = "weapon_smokegrenade",
    molotov  = "weapon_molotov",
    decoy    = "weapon_decoy",
    inferno  = "weapon_molotov",
}

-- ============================================================
-- helpers
-- ============================================================
local function get_schema( c, class_name, field_name )
    local key = class_name .. "." .. field_name
    if not c.schemas[key] then
        c.schemas[key] = modules.source2:get_schema( class_name, field_name )
    end
    return c.schemas[key]
end

-- Returns a stable, consistent key for an entity that survives
-- across frames without relying on potentially missing "address" field.
local function entity_key( entity )
    local addr = rawget( entity, "address" ) or rawget( entity, "ptr" ) or rawget( entity, "handle" )
    if addr then return tostring( addr ) end
    return tostring( entity )
end

local function get_type_color( grenade_type )
    if grenade_type == "he"      then return grenade_warner.color_he      end
    if grenade_type == "flash"   then return grenade_warner.color_flash   end
    if grenade_type == "smoke"   then return grenade_warner.color_smoke   end
    if grenade_type == "molotov" then return grenade_warner.color_molotov end
    if grenade_type == "decoy"   then return grenade_warner.color_decoy   end
    if grenade_type == "inferno" then return grenade_warner.color_molotov end
    return grenade_warner.color_default
end

local function lerp_color( a, b, t )
    t = math.max( 0, math.min( 1, t ) )
    local ar, ag, ab, aa = a.r or 0, a.g or 0, a.b or 0, a.a or 255
    local br, bg, bb, ba = b.r or 0, b.g or 0, b.b or 0, b.a or 255
    return color:new(
        math.floor( ar + ( br - ar ) * t ),
        math.floor( ag + ( bg - ag ) * t ),
        math.floor( ab + ( bb - ab ) * t ),
        math.floor( aa + ( ba - aa ) * t )
    )
end

local function vec3_center( points )
    local cx, cy, cz = 0, 0, 0
    for _, p in ipairs( points ) do
        cx = cx + p.x
        cy = cy + p.y
        cz = cz + p.z
    end
    local n = #points
    return { x = cx / n, y = cy / n, z = cz / n }
end





-- ============================================================
-- convex hull (Andrew's monotone chain) — O(n log n)
-- ============================================================
local function cross_2d( o, a, b )
    return ( a.x - o.x ) * ( b.y - o.y ) - ( a.y - o.y ) * ( b.x - o.x )
end

local function get_convex_hull( screen_points )
    if #screen_points < 3 then return screen_points end

    table.sort( screen_points, function( a, b )
        if a.x ~= b.x then return a.x < b.x end
        return a.y < b.y
    end)

    local lower = {}
    for _, p in ipairs( screen_points ) do
        while #lower >= 2 and cross_2d( lower[#lower - 1], lower[#lower], p ) <= 0 do
            table.remove( lower )
        end
        lower[#lower + 1] = p
    end

    local upper = {}
    for i = #screen_points, 1, -1 do
        local p = screen_points[i]
        while #upper >= 2 and cross_2d( upper[#upper - 1], upper[#upper], p ) <= 0 do
            table.remove( upper )
        end
        upper[#upper + 1] = p
    end

    -- remove last point of each half (duplicate of the other's first)
    table.remove( lower )
    table.remove( upper )

    local hull = {}
    for _, p in ipairs( lower ) do hull[#hull + 1] = p end
    for _, p in ipairs( upper ) do hull[#hull + 1] = p end

    return hull
end

-- ============================================================
-- triangle fan fill for convex hull
-- ============================================================
local function fill_convex_hull( hull, clr )
    if #hull < 3 then return end

    -- fan from hull[1] across all other edges
    local pivot = hull[1]
    for i = 2, #hull - 1 do
        modules.kernel:triangle_filled(
            pivot.x, pivot.y,
            hull[i].x, hull[i].y,
            hull[i + 1].x, hull[i + 1].y,
            clr
        )
    end
end

-- ============================================================
-- draw helpers
-- ============================================================
local function draw_icon_and_name( scr, grenade_type )
    local clr = get_type_color( grenade_type )
    local fs  = grenade_warner.font_size
    local y_offset = 0

    if grenade_warner.show_icon then
        local img_name = IMAGE_NAMES[grenade_type]
        local icon_id  = img_name and cache.weapon_icons[img_name]

        if icon_id then
            local icon_sz = grenade_warner.icon_size
            local icon_h  = math.max( 1, math.floor( icon_sz * 0.5 ) )  -- must be integer >= 1
            local icon_x  = math.floor( scr.x - icon_sz * 0.5 )
            local icon_y  = math.floor( scr.y - icon_h * 0.5 + y_offset )
            modules.kernel:image( icon_x, icon_y, icon_sz, icon_h, icon_id )
            y_offset = y_offset + icon_h + 2
        end
    end

    if grenade_warner.show_name then
        local name    = DISPLAY_NAMES[grenade_type] or "?"
        local name_fs = fs - 2
        local tw      = #name * name_fs * 0.52
        local pad_x   = 4
        local pad_y   = 2

        -- background pill (type color)
        local bg_clr = color:new( clr )
        bg_clr.a = math.min( bg_clr.a, 180 )
        local pill_x = math.floor( scr.x - tw * 0.5 - pad_x )
        local pill_y = math.floor( scr.y + y_offset - pad_y )
        local pill_w = math.floor( tw + pad_x * 2 )
        local pill_h = math.floor( name_fs + pad_y * 2 )
        modules.kernel:boxf( pill_x, pill_y, pill_w, pill_h, bg_clr )

        -- white text on top
        extended_draw.text( name, name_fs, scr.x - tw * 0.5, scr.y + y_offset, {
            text_color = "FFFFFFFF", outline = false
        })

        y_offset = y_offset + pill_h + 2
    end

    return y_offset
end

local function draw_timer_bar( scr, y_offset, remaining, frac, hide_text )
    if not grenade_warner.show_timer_bar then return y_offset end

    local bw = grenade_warner.bar_width
    local bh = grenade_warner.bar_height
    local bx = math.floor( scr.x - bw * 0.5 )
    local by = math.floor( scr.y + y_offset + 6 )

    -- background
    modules.kernel:boxf( bx - 1, by - 1, bw + 2, bh + 2, color:new( grenade_warner.bar_background ) )

    -- color-lerped fill
    local bar_clr = lerp_color(
        color:new( grenade_warner.timer_low_color ),
        color:new( grenade_warner.timer_high_color ),
        frac
    )

    local fill_w = math.floor( math.max( 1, bw * frac ) )  -- integer, at least 1px wide
    modules.kernel:boxf( bx, by, fill_w, bh, bar_clr )

    -- timer text (optional — inferno uses its own big center text)
    if not hide_text then
        local t_text = string.format( "%.1fs", remaining )
        extended_draw.text( t_text, grenade_warner.timer_font_size, bx + bw + 4, by - 2, {
            text_color = "C3C8D7CC", outline = grenade_warner.outline, outline_color = grenade_warner.outline_color
        })
    end

    return y_offset + bh + 4
end

-- ============================================================
-- fire polygon rendering (inferno convex hull + fill)
-- ============================================================
local function draw_fire_polygon( fire_points )
    if not grenade_warner.show_fire_polygon then return end
    if #fire_points < 2 then return end

    local radius = grenade_warner.fire_padding
    local samples = 8
    local screen_points = {}

    -- circle-sample each flame position for a smooth, reliable hull
    for _, fp in ipairs( fire_points ) do
        for i = 0, samples - 1 do
            local angle = ( i / samples ) * math.pi * 2
            local wx = fp.x + math.cos( angle ) * radius
            local wy = fp.y + math.sin( angle ) * radius

            local screen_pos = modules.w2s( vector:new( wx, wy, fp.z ) )
            if screen_pos then
                screen_points[#screen_points + 1] = { x = screen_pos.x, y = screen_pos.y }
            end
        end
    end

    if #screen_points < 3 then return end

    local hull = get_convex_hull( screen_points )
    if #hull < 3 then return end

    -- filled polygon
    fill_convex_hull( hull, color:new( grenade_warner.fire_fill_color ) )

    -- outline polygon
    local outline_clr = color:new( grenade_warner.fire_outline_color )
    for i = 1, #hull do
        local p1 = hull[i]
        local p2 = hull[( i % #hull ) + 1]
        modules.kernel:line( p1.x, p1.y, p2.x, p2.y, grenade_warner.fire_outline_thick, outline_clr )
    end
end

-- ============================================================
-- main render loop
-- ============================================================
local function render_all()
    local globals = modules.source2:get_globals()
    if not globals then return end

    local tick_count = globals.tick_count or 0
    local now = fantasy.time()
    local entities = modules.entity_list:get_entities()
    if not entities then return end

    local seen_fires  = {}
    local seen_smokes = {}
    local seen_decoys = {}

    for _, entity in pairs( entities ) do
        do -- scope block for goto compatibility
        if not entity then goto continue end

        local class_info = { entity:get_class() }
        local class_name = class_info[2]
        if not class_name then goto continue end

        local grenade_type = PROJECTILE_TYPE[class_name]
        if not grenade_type then goto continue end

        -- ==================================================
        -- IN-FLIGHT MOLOTOV: track m_bIsIncGrenade for timer
        -- ==================================================
        if grenade_type == "molotov" then
            local thrower_handle = entity:read( MEM_INT, get_schema( cache, "C_BaseGrenade", "m_hThrower" ) )
            if thrower_handle and thrower_handle > 0 then
                local is_inc = entity:read( MEM_BOOL, get_schema( cache, "C_MolotovProjectile", "m_bIsIncGrenade" ) )
                cache.thrower_to_inc[thrower_handle] = is_inc
            end

            -- render the in-flight projectile
            local gsn = entity:read( MEM_ADDRESS, get_schema( cache, "C_BaseEntity", "m_pGameSceneNode" ) )
            if gsn and gsn:is_valid() then
                local origin = gsn:read( MEM_VECTOR, get_schema( cache, "CGameSceneNode", "m_vecAbsOrigin" ) )
                if origin then
                    local scr = modules.w2s( origin )
                    if scr then
                        draw_icon_and_name( scr, "molotov" )
                    end
                end
            end

            goto continue
        end

        -- ==================================================
        -- INFERNO (fire on the ground)
        -- ==================================================
        if grenade_type == "inferno" then
            local entity_addr = entity_key( entity )
            seen_fires[entity_addr] = true

            local is_burning = entity:read( MEM_BOOL, get_schema( cache, "C_Inferno", "m_bFireIsBurning" ) )
            if not is_burning then goto continue end

            -- water-tight timer: determine duration on first sight
            if not cache.active_fires[entity_addr] then
                local is_incendiary = false

                local owner_handle = entity:read( MEM_INT, get_schema( cache, "C_BaseEntity", "m_hOwnerEntity" ) )
                if owner_handle and owner_handle > 0 then
                    is_incendiary = cache.thrower_to_inc[owner_handle] == true
                end

                local duration = is_incendiary and grenade_warner.duration_inferno_inc or grenade_warner.duration_inferno_molotov

                cache.active_fires[entity_addr] = {
                    start_time   = now,
                    max_duration = duration,
                }
            end

            local fire_data = cache.active_fires[entity_addr]
            local elapsed   = ( now - fire_data.start_time ) / 1000.0
            local remaining = math.max( 0.0, fire_data.max_duration - elapsed )

            if remaining <= 0.0 then goto continue end

            -- read fire positions
            local fire_count = entity:read( MEM_INT, get_schema( cache, "C_Inferno", "m_fireCount" ) )
            if not fire_count or fire_count <= 0 then goto continue end

            local fire_positions_base = get_schema( cache, "C_Inferno", "m_firePositions" )
            -- guard: if schema lookup failed skip rather than crash on nil arithmetic
            if not fire_positions_base then goto continue end

            local fire_points = {}
            local max_fire = math.min( fire_count, 64 )

            for i = 0, max_fire - 1 do
                local fp = entity:read( MEM_VECTOR, fire_positions_base + ( i * 12 ) )
                if fp then
                    fire_points[#fire_points + 1] = fp
                end
            end

            if #fire_points == 0 then goto continue end

            -- fire polygon (fill + outline)
            draw_fire_polygon( fire_points )

            -- icon, name, timer at center
            local center = vec3_center( fire_points )
            local scr = modules.w2s( center )
            if scr then
                local y_offset = draw_icon_and_name( scr, "inferno" )

                -- timer bar (same style as smoke/decoy)
                local frac = math.max( 0, math.min( 1, remaining / fire_data.max_duration ) )
                y_offset = draw_timer_bar( scr, y_offset, remaining, frac )
            end

            goto continue
        end

        -- ==================================================
        -- HE / FLASH — skip if detonated
        -- ==================================================
        if grenade_type == "he" or grenade_type == "flash" then
            if grenade_warner.hide_detonated then
                local det_tick = entity:read( MEM_INT, get_schema( cache, "C_BaseCSGrenadeProjectile", "m_nExplodeEffectTickBegin" ) )
                if det_tick and det_tick > 0 then
                    goto continue
                end
            end

            local gsn = entity:read( MEM_ADDRESS, get_schema( cache, "C_BaseEntity", "m_pGameSceneNode" ) )
            if not gsn or not gsn:is_valid() then goto continue end

            local origin = gsn:read( MEM_VECTOR, get_schema( cache, "CGameSceneNode", "m_vecAbsOrigin" ) )
            if not origin then goto continue end

            local scr = modules.w2s( origin )
            if scr then
                draw_icon_and_name( scr, grenade_type )
            end

            goto continue
        end

        -- ==================================================
        -- SMOKE — with 18s timer
        -- ==================================================
        if grenade_type == "smoke" then
            local entity_addr = entity_key( entity )
            seen_smokes[entity_addr] = true

            local gsn = entity:read( MEM_ADDRESS, get_schema( cache, "C_BaseEntity", "m_pGameSceneNode" ) )
            if not gsn or not gsn:is_valid() then goto continue end

            local origin = gsn:read( MEM_VECTOR, get_schema( cache, "CGameSceneNode", "m_vecAbsOrigin" ) )
            if not origin then goto continue end

            local scr = modules.w2s( origin )
            if not scr then goto continue end

            local y_offset = draw_icon_and_name( scr, "smoke" )

            local smoke_active = entity:read( MEM_BOOL, get_schema( cache, "C_SmokeGrenadeProjectile", "m_bDidSmokeEffect" ) )
            if smoke_active then
                local smoke_tick = entity:read( MEM_INT, get_schema( cache, "C_SmokeGrenadeProjectile", "m_nSmokeEffectTickBegin" ) )
                if smoke_tick and smoke_tick > 0 then
                    local elapsed   = ( tick_count - smoke_tick ) * TICK_INTERVAL
                    local remaining = math.max( 0, grenade_warner.duration_smoke - elapsed )
                    local frac      = math.max( 0, math.min( 1, remaining / grenade_warner.duration_smoke ) )
                    y_offset = draw_timer_bar( scr, y_offset, remaining, frac )
                end
            end

            goto continue
        end

        -- ==================================================
        -- DECOY — with 15s timer
        -- ==================================================
        if grenade_type == "decoy" then
            local entity_addr = entity_key( entity )
            seen_decoys[entity_addr] = true
            local gsn = entity:read( MEM_ADDRESS, get_schema( cache, "C_BaseEntity", "m_pGameSceneNode" ) )
            if not gsn or not gsn:is_valid() then goto continue end
            local origin = gsn:read( MEM_VECTOR, get_schema( cache, "CGameSceneNode", "m_vecAbsOrigin" ) )
            if not origin then goto continue end
            local scr = modules.w2s( origin )
            if not scr then goto continue end
            local y_offset = draw_icon_and_name( scr, "decoy" )
            local decoy_tick = entity:read( MEM_INT, get_schema( cache, "C_DecoyProjectile", "m_nDecoyShotTick" ) )
            if decoy_tick and decoy_tick > 0 then
                -- Cache the tick the FIRST time we see it shoot
                if not cache.active_decoys[entity_addr] then
                    cache.active_decoys[entity_addr] = decoy_tick
                end
                -- Use our cached starting tick, NOT the live updating one
                local start_tick = cache.active_decoys[entity_addr]
                local elapsed    = ( tick_count - start_tick ) * TICK_INTERVAL
                local remaining  = math.max( 0, grenade_warner.duration_decoy - elapsed )
                
                if remaining > 0 then
                    local frac = math.max( 0, math.min( 1, remaining / grenade_warner.duration_decoy ) )
                    y_offset = draw_timer_bar( scr, y_offset, remaining, frac )
                end
            end
            goto continue
        end

        end -- do
        ::continue::
        local _ = 0 -- LuaJIT parser workaround for label at end of block
    end

    -- cleanup stale data for fires, smokes, decoys, and predictions
    local new_fires = {}
    for addr, data in pairs( cache.active_fires ) do
        if seen_fires[addr] then
            new_fires[addr] = data
        end
    end
    cache.active_fires = new_fires
end

-- ============================================================
-- callbacks
-- ============================================================
function grenade_warner.on_solution_calibrated( data )
    cache.gameid = data.gameid

    if data.gameid ~= GAME_CS2 then
        fantasy.print( "Grenade Warner: CS2 only, disabling." )
        grenade_warner.enabled = false
        return
    end

    fantasy.log( "Grenade Warner: Ready. GameID: " .. tostring( data.gameid ) )
end

-- cache weapon PNG icons from the image library
function grenade_warner.on_overlay_created()
    local images     = modules.images
    local image_list = images:list()
    local count      = 0

    for k, v in pairs( image_list ) do
        local name = type( k ) == "string" and k or ( type( v ) == "string" and v or nil )
        if name then
            local id = images:get( name )
            if id then
                local clean = name:gsub( "%.png$", "" )
                cache.weapon_icons[clean] = id
                count = count + 1
            end
        end
    end
    fantasy.log( "Grenade Warning: cached " .. tostring( count ) .. " weapon icons." )

    -- debug: dump available icon names so we can match grenade types
    for name, _ in pairs( cache.weapon_icons ) do
        fantasy.log( "  [icon] " .. name )
    end
end

function grenade_warner.on_worker( is_calibrated )
    if not is_calibrated then return end
    if not grenade_warner.enabled then return end

    render_all()
end

function grenade_warner.on_configuration_overwrite()
    cache.active_fires   = {}
    cache.thrower_to_inc = {}
    fantasy.log( "Grenade Warner: Configuration updated." )
end

function grenade_warner.on_scripts_loaded()
    cache.active_fires   = {}
    cache.thrower_to_inc = {}
    cache.schemas        = {}
end

return grenade_warner
