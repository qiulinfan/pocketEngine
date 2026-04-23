AdventureShared = AdventureShared or {}

local kMusicChannel = 0
local kSwordChannel = 1
local kArrowChannel = 2
local kDamageChannel = 3
local kPickupChannel = 4
local kWaveChannel = 5
local kShieldChannel = 6

local kArrowSprite = "Arrow"
local kHeartFullSprite = "adventure_ui/heart_full"
local kHeartHalfSprite = "adventure_ui/heart_half"
local kHeartEmptySprite = "adventure_ui/heart_empty"
local kBarBackgroundSprite = "adventure_ui/health_bg"
local kResolvedClipCache = {}

local function EnsureSeeded()
    if AdventureShared._seeded then
        return
    end

    local seed = 424242
    if os ~= nil and os.time ~= nil then
        seed = os.time() % 2147483647
    end
    math.randomseed(seed)
    AdventureShared._seeded = true
end

function AdventureShared.GetState()
    EnsureSeeded()

    if AdventureShared.state == nil then
        AdventureShared.state = {
            bow_unlocked = false,
            shield_unlocked = false,
            last_scene = "",
            pending_reset = false,
            current_music = "",
            equipped_weapon = "sword"
        }
    end
    return AdventureShared.state
end

function AdventureShared.ResetRun()
    local state = AdventureShared.GetState()
    state.bow_unlocked = false
    state.shield_unlocked = false
    state.last_scene = ""
    state.pending_reset = false
    state.current_music = ""
    state.equipped_weapon = "sword"
    Audio.Halt(kMusicChannel)
end

function AdventureShared.MarkReturningToFirstScene()
    local state = AdventureShared.GetState()
    state.pending_reset = true
end

function AdventureShared.ConsumePendingReset(scene_name)
    local state = AdventureShared.GetState()
    if scene_name == "forest_glade" and state.pending_reset then
        AdventureShared.ResetRun()
    end
    state.last_scene = scene_name
end

function AdventureShared.Clamp(value, minimum, maximum)
    if value < minimum then
        return minimum
    end
    if value > maximum then
        return maximum
    end
    return value
end

function AdventureShared.GetWindowSize()
    local window_width = 640
    local window_height = 360

    if Application ~= nil and Application.GetWindowWidth ~= nil then
        window_width = math.max(1, Application.GetWindowWidth())
    end
    if Application ~= nil and Application.GetWindowHeight ~= nil then
        window_height = math.max(1, Application.GetWindowHeight())
    end

    return window_width, window_height
end

function AdventureShared.GetViewportHalfExtents(zoom)
    local safe_zoom = math.max(0.01, zoom or 1.0)
    local window_width, window_height = AdventureShared.GetWindowSize()
    return window_width / (200.0 * safe_zoom), window_height / (200.0 * safe_zoom)
end

function AdventureShared.GetStageBounds(scene_name)
    if scene_name == "stone_sanctum" then
        return -3.1, 3.1, -2.15, 2.15
    end
    return -3.2, 3.2, -2.2, 2.2
end

function AdventureShared.ResolveAudioClip(candidates)
    if candidates == nil then
        return ""
    end

    local cache_key = table.concat(candidates, "|")
    local cached_clip = kResolvedClipCache[cache_key]
    if cached_clip ~= nil then
        return cached_clip
    end

    for index = 1, #candidates do
        local candidate = candidates[index]
        if candidate ~= nil and candidate ~= "" and Audio.HasClip(candidate) then
            kResolvedClipCache[cache_key] = candidate
            return candidate
        end
    end
    kResolvedClipCache[cache_key] = ""
    return ""
end

function AdventureShared.PreloadAudioClip(candidates, as_music)
    local clip_name = AdventureShared.ResolveAudioClip(candidates)
    if clip_name == "" then
        return ""
    end
    if Audio.Preload ~= nil then
        Audio.Preload(clip_name, as_music == true)
    end
    return clip_name
end

function AdventureShared.PreloadCommonAudio(scene_name)
    AdventureShared.PreloadAudioClip({"playSwingSword_clean", "playSwingSword"}, false)
    AdventureShared.PreloadAudioClip({"playerDamaged"}, false)
    AdventureShared.PreloadAudioClip({"itemPickUp"}, false)
    AdventureShared.PreloadAudioClip({"arrow-swish"}, false)

    if scene_name == "stone_sanctum" then
        AdventureShared.PreloadAudioClip({"goblinsaint_swordwave"}, false)
        AdventureShared.PreloadAudioClip({"shieldblock"}, false)
    end
end

function AdventureShared.PlayMusic(candidates)
    local clip_name = AdventureShared.ResolveAudioClip(candidates)
    if clip_name == "" then
        Audio.Halt(kMusicChannel)
        AdventureShared.GetState().current_music = ""
        return ""
    end

    local state = AdventureShared.GetState()
    if Audio.IsPlaybackEnabled ~= nil and not Audio.IsPlaybackEnabled() then
        state.current_music = ""
        return clip_name
    end
    if state.current_music == clip_name and
       (Audio.IsPlaying == nil or Audio.IsPlaying(kMusicChannel)) then
        return clip_name
    end

    Audio.SetVolume(kMusicChannel, 78)
    Audio.Play(kMusicChannel, clip_name, true)
    state.current_music = clip_name
    return clip_name
end

function AdventureShared.StopMusic()
    Audio.Halt(kMusicChannel)
    AdventureShared.GetState().current_music = ""
end

function AdventureShared.PlaySwordSwing()
    local clip_name = AdventureShared.ResolveAudioClip({"playSwingSword_clean", "playSwingSword"})
    if clip_name ~= "" then
        Audio.SetVolume(kSwordChannel, 84)
        Audio.Play(kSwordChannel, clip_name, false)
    end
end

function AdventureShared.PlayArrowShot()
    local clip_name = AdventureShared.ResolveAudioClip({"arrow-swish"})
    if clip_name ~= "" then
        Audio.SetVolume(kArrowChannel, 88)
        Audio.Play(kArrowChannel, clip_name, false)
    end
end

function AdventureShared.PlayPlayerDamaged()
    if Audio.HasClip("playerDamaged") then
        Audio.SetVolume(kDamageChannel, 96)
        Audio.Play(kDamageChannel, "playerDamaged", false)
    end
end

function AdventureShared.PlayPickup()
    if Audio.HasClip("itemPickUp") then
        Audio.SetVolume(kPickupChannel, 88)
        Audio.Play(kPickupChannel, "itemPickUp", false)
    end
end

function AdventureShared.PlaySwordWave()
    if Audio.HasClip("goblinsaint_swordwave") then
        Audio.SetVolume(kWaveChannel, 90)
        Audio.Play(kWaveChannel, "goblinsaint_swordwave", false)
    end
end

function AdventureShared.PlayShieldBlock()
    local clip_name = AdventureShared.ResolveAudioClip({"shieldblock"})
    if clip_name ~= "" then
        Audio.SetVolume(kShieldChannel, 92)
        Audio.Play(kShieldChannel, clip_name, false)
    end
end

function AdventureShared.Lerp(current, target, factor)
    return current + (target - current) * factor
end

function AdventureShared.Normalize(x, y)
    local length = math.sqrt(x * x + y * y)
    if length <= 0.0001 then
        return 0.0, 0.0, 0.0
    end
    return x / length, y / length, length
end

function AdventureShared.Distance(ax, ay, bx, by)
    local dx = bx - ax
    local dy = by - ay
    return math.sqrt(dx * dx + dy * dy)
end

function AdventureShared.AnimationFrame(frame_count, frame_stride)
    return 1 + (math.floor(Application.GetFrame() / frame_stride) % frame_count)
end

function AdventureShared.StandardDirectionRow(facing_x, facing_y)
    if math.abs(facing_x) > math.abs(facing_y) then
        return 3, (facing_x < 0.0) and -1.0 or 1.0
    end
    if facing_y < 0.0 then
        return 2, 1.0
    end
    return 1, 1.0
end

function AdventureShared.SlimeDirectionRow(facing_x, facing_y)
    if math.abs(facing_x) > math.abs(facing_y) then
        return 1, (facing_x < 0.0) and -1.0 or 1.0
    end
    if facing_y < 0.0 then
        return 3, 1.0
    end
    return 2, 1.0
end

function AdventureShared.ArrowDirectionRow(facing_x, facing_y)
    return AdventureShared.StandardDirectionRow(facing_x, facing_y)
end

function AdventureShared.SpriteRef(image_name, row, column)
    return "sprite://" .. image_name .. ".png?row=" ..
        tostring(row) .. "&column=" .. tostring(column)
end

function AdventureShared.BarBackgroundSprite()
    return kBarBackgroundSprite
end

function AdventureShared.HeartSprite(kind)
    if kind == "full" then
        return kHeartFullSprite
    end
    if kind == "half" then
        return kHeartHalfSprite
    end
    return kHeartEmptySprite
end

function AdventureShared.GetHeartState(health_units, heart_index)
    local units_remaining = health_units - (heart_index - 1) * 2
    if units_remaining >= 2 then
        return "full"
    end
    if units_remaining == 1 then
        return "half"
    end
    return "empty"
end

function AdventureShared.DrawHeartRowUI(x, y, health_units, max_health_units)
    local heart_count = math.max(1, math.ceil(max_health_units / 2))
    local background_count = math.max(1, math.ceil((heart_count * 12 + 18) / 44))
    for index = 1, background_count do
        Image.DrawUIEx(AdventureShared.BarBackgroundSprite(),
                       x + (index - 1) * 44, y,
                       255, 255, 255, 255, 10)
    end
    for index = 1, heart_count do
        local heart_state = AdventureShared.GetHeartState(health_units, index)
        Image.DrawUIEx(AdventureShared.HeartSprite(heart_state),
                       x + 8 + (index - 1) * 12, y + 4,
                       255, 255, 255, 255, 11)
    end
end

function AdventureShared.DrawHeartRowWorld(x, y, health_units, max_health_units,
                                           scale, sorting_order)
    local heart_count = math.max(1, math.ceil(max_health_units / 2))
    local width_offset = (heart_count - 1) * 0.06
    Image.DrawEx(AdventureShared.BarBackgroundSprite(), x - 0.12 - width_offset * 0.5,
                 y, 0.0, scale * (1.0 + heart_count * 0.18), scale,
                 0.0, 0.0, 255, 255, 255, 230, sorting_order)
    for index = 1, heart_count do
        local heart_state = AdventureShared.GetHeartState(health_units, index)
        Image.DrawEx(AdventureShared.HeartSprite(heart_state),
                     x - width_offset * 0.5 + (index - 1) * 0.12, y + 0.02,
                     0.0, scale, scale, 0.5, 0.5,
                     255, 255, 255, 255, sorting_order + 1)
    end
end

function AdventureShared.ConfigureArrowSprite(sprite_renderer, facing_x, facing_y)
    if sprite_renderer == nil then
        return
    end
    local row, scale_x = AdventureShared.ArrowDirectionRow(facing_x, facing_y)
    sprite_renderer.sprite = kArrowSprite
    sprite_renderer.scale_x = scale_x
    sprite_renderer.scale_y = 1.0
    sprite_renderer:SetSpriteCell(row, 1 + (math.floor(Application.GetFrame() / 5) % 4))
end

function AdventureShared.CurrentWeaponName()
    local state = AdventureShared.GetState()
    if state.bow_unlocked then
        return "bow"
    end
    return "sword"
end
