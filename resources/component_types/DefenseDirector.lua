local kStageConfigs = {
    forest_glade = {
        scene_label = "Stage 1  Slime Siege",
        objective_text = "Protect the altar. Clear the slime wave and claim the bow.",
        music_candidates = {"main", "Village Under Siege"},
        next_scene = "bridge_watch",
        reward = "bow",
        altar_x = 0.0,
        altar_y = 0.02,
        random_decoration_count = 26,
        waves = {
            {frame = 20, enemies = {
                {"SlimeEnemy", -2.55, -1.08},
                {"SlimeEnemy", 2.48, 0.96}
            }},
            {frame = 180, enemies = {
                {"SlimeEnemy", -2.64, 0.0},
                {"SlimeEnemy", 2.56, -0.72},
                {"SlimeEnemy", 2.46, 1.08}
            }},
            {frame = 360, enemies = {
                {"SlimeEnemy", -2.58, 1.1},
                {"SlimeEnemy", -2.44, -0.94},
                {"SlimeEnemy", 2.56, 0.0}
            }}
        }
    },
    bridge_watch = {
        scene_label = "Stage 2  Goblin Raid",
        objective_text = "Goblins rush both you and the altar. Survive and recover the shield.",
        music_candidates = {"invasion", "Village Under Siege"},
        next_scene = "stone_sanctum",
        reward = "shield",
        altar_x = 0.18,
        altar_y = -0.06,
        random_decoration_count = 30,
        waves = {
            {frame = 20, enemies = {
                {"SpearGoblin", -2.62, -1.08},
                {"SpearGoblin", 2.55, 0.82}
            }},
            {frame = 180, enemies = {
                {"SpearGoblin", -2.58, 0.18},
                {"SpearGoblin", 2.48, -0.96},
                {"SpearGoblin", 2.52, 1.04}
            }},
            {frame = 360, enemies = {
                {"SpearGoblin", -2.6, -1.08},
                {"SpearGoblin", -2.5, 1.04},
                {"SpearGoblin", 2.58, 0.02}
            }}
        }
    },
    stone_sanctum = {
        scene_label = "Stage 3  Sword Saint",
        objective_text = "Defeat the goblin sword saint. Right-click to shield and reflect sword waves.",
        music_candidates = {"GoblinSwordSaint", "boss", "goblin-sword-saint"},
        next_scene = "victory",
        reward = "",
        altar_x = 0.0,
        altar_y = -1.12,
        random_decoration_count = 34,
        waves = {
            {frame = 15, enemies = {
                {"GoblinSwordSaint", 1.78, -0.02}
            }}
        }
    }
}

-- The only allowed Summer Crops cells for scene dressing.
local kRandomDecorationCells = {
    {2, 1}, {2, 2}, {2, 3},
    {4, 1}, {4, 2}, {4, 3}, {4, 4},
    {6, 3}, {6, 4},
    {8, 1}, {8, 3},
    {10, 2}, {10, 3},
    {16, 3},
    {20, 3},
    {22, 3}, {22, 4},
    {24, 11},
    {26, 3}
}
local kDecorationScale = 2.0
local kDecorationRangeX = 3.1
local kDecorationRangeY = 2.2

local function SpawnProp(sprite_name, row, column, x, y, scale_x, scale_y, sorting_order,
                         auto_sorting, tint_r, tint_g, tint_b, tint_a)
    local actor = Actor.Instantiate("AdventureProp")
    if actor == nil then
        return nil
    end

    local transform = actor:GetComponent("Transform")
    local sprite = actor:GetComponent("SpriteRenderer")
    if transform == nil or sprite == nil then
        return actor
    end

    transform.x = x
    transform.y = y
    sprite.sprite = sprite_name
    sprite:SetSpriteCell(row, column)
    sprite.scale_x = scale_x or 1.0
    sprite.scale_y = scale_y or 1.0
    sprite.auto_sorting_order = auto_sorting == true
    sprite.sorting_order = sorting_order or 0
    sprite.r = tint_r or 255
    sprite.g = tint_g or 255
    sprite.b = tint_b or 255
    sprite.a = tint_a or 255
    return actor
end

local function SpawnEnemy(template_name, x, y)
    local actor = Actor.Instantiate(template_name)
    if actor == nil then
        return nil
    end

    local transform = actor:GetComponent("Transform")
    if transform ~= nil then
        transform.x = x
        transform.y = y
    end
    return actor
end

local function RandomRange(minimum, maximum)
    return minimum + math.random() * (maximum - minimum)
end

local function IsTooCloseToPoint(x, y, target_x, target_y, radius)
    return AdventureShared.Distance(x, y, target_x, target_y) < radius
end

DefenseDirector = {
    OnStart = function(self)
        self.scene_name = Scene.GetCurrent()
        AdventureShared.ConsumePendingReset(self.scene_name)
        self.stage = kStageConfigs[self.scene_name]
        self.frame_in_scene = 0
        self.next_wave_index = 1
        self.reward_spawned = false
        self.reward_collected = false
        self.stage_cleared = false
        self.transition_timer = 0
        self.fail_timer = 0
        self.status_message = ""
        self.enemies_remaining = 0

        if self.stage == nil then
            self.status_message = "Unknown stage."
            return
        end

        local state = AdventureShared.GetState()
        if self.scene_name == "bridge_watch" then
            state.bow_unlocked = true
        elseif self.scene_name == "stone_sanctum" then
            state.bow_unlocked = true
            state.shield_unlocked = true
        end

        AdventureShared.PreloadCommonAudio(self.scene_name)
        AdventureShared.PlayMusic(self.stage.music_candidates)
        self:SpawnEnvironment()
    end,

    SpawnEnvironment = function(self)
        local decoration_count = self.stage.random_decoration_count or 0
        local placed = 0
        local attempts = 0
        while placed < decoration_count and attempts < decoration_count * 12 do
            attempts = attempts + 1
            local x = RandomRange(-kDecorationRangeX, kDecorationRangeX)
            local y = RandomRange(-kDecorationRangeY, kDecorationRangeY)

            if not IsTooCloseToPoint(x, y, self.stage.altar_x, self.stage.altar_y, 1.05) and
               not IsTooCloseToPoint(x, y, 0.0, 0.0, 0.7) then
                local cell = kRandomDecorationCells[
                    math.random(1, #kRandomDecorationCells)]
                SpawnProp("Summer Crops", cell[1], cell[2],
                          x, y, kDecorationScale, kDecorationScale, 0, true)
                placed = placed + 1
            end
        end

        local altar = Actor.Instantiate("AdventureAltar")
        if altar ~= nil then
            local transform = altar:GetComponent("Transform")
            if transform ~= nil then
                transform.x = self.stage.altar_x
                transform.y = self.stage.altar_y
            end
        end
    end,

    GetEnemiesRemaining = function(self)
        return self.enemies_remaining
    end,

    IsCleared = function(self)
        return self.stage_cleared
    end,

    GetObjectiveText = function(self)
        if self.fail_timer > 0 then
            return "The altar fell. The watch resets..."
        end
        if self.transition_timer > 0 and self.reward_collected then
            if self.scene_name == "stone_sanctum" then
                return "The sword saint is down. The forest breathes again."
            end
            return "Relic recovered. Marching to the next battlefield..."
        end
        if self.reward_spawned and not self.reward_collected then
            if self.stage.reward == "shield" then
                return "Pick up the shield sigil."
            end
            return "Pick up the bow cache."
        end
        if self.stage_cleared and self.scene_name == "stone_sanctum" then
            return "Hold steady. The shrine is safe."
        end
        return self.stage.objective_text
    end,

    HandlePickupCollected = function(self, pickup_type)
        self.reward_collected = true
        if pickup_type == "shield" then
            self.status_message = "Shield acquired. Right mouse now reflects sword waves."
        else
            self.status_message = "Bow acquired. Your shots will carry into the next stage."
        end
        self.transition_timer = 70
    end,

    SpawnReward = function(self)
        if self.reward_spawned or self.stage.reward == nil or self.stage.reward == "" then
            return
        end

        self.reward_spawned = true
        self.status_message = (self.stage.reward == "shield")
            and "The goblins dropped a shield sigil."
            or "The slimes dropped a bow cache."

        local pickup = Actor.Instantiate("AdventurePickup")
        if pickup == nil then
            return
        end

        local transform = pickup:GetComponent("Transform")
        local component = pickup:GetComponent("AdventurePickup")
        if transform ~= nil then
            transform.x = self.stage.altar_x
            transform.y = self.stage.altar_y - 0.48
        end
        if component ~= nil then
            component.pickup_type = self.stage.reward
        end
    end,

    UpdateWaves = function(self)
        if self.next_wave_index > #self.stage.waves then
            return
        end

        local wave = self.stage.waves[self.next_wave_index]
        if self.frame_in_scene < wave.frame then
            return
        end

        for index = 1, #wave.enemies do
            local enemy = wave.enemies[index]
            SpawnEnemy(enemy[1], enemy[2], enemy[3])
        end
        self.next_wave_index = self.next_wave_index + 1
    end,

    OnUpdate = function(self)
        if self.stage == nil then
            return
        end

        self.frame_in_scene = self.frame_in_scene + 1
        self:UpdateWaves()

        local enemies = Actor.FindAll("enemy")
        self.enemies_remaining = #enemies

        local altar_actor = Actor.Find("altar")
        local altar = nil
        if altar_actor ~= nil then
            altar = altar_actor:GetComponent("DefenseTarget")
        end

        if altar ~= nil and altar:IsDestroyed() then
            if self.fail_timer <= 0 then
                self.fail_timer = 80
                self.status_message = "The altar shattered."
            else
                self.fail_timer = self.fail_timer - 1
                if self.fail_timer <= 0 then
                    Scene.Load(self.scene_name)
                end
            end
            return
        end

        local all_waves_spawned = self.next_wave_index > #self.stage.waves
        self.stage_cleared = all_waves_spawned and self.enemies_remaining == 0

        if self.stage_cleared and not self.reward_spawned and not self.reward_collected then
            if self.scene_name == "stone_sanctum" then
                self.reward_collected = true
                self.status_message = "The sword saint has fallen."
                self.transition_timer = 110
            else
                self:SpawnReward()
            end
        end

        if self.transition_timer > 0 then
            self.transition_timer = self.transition_timer - 1
            if self.transition_timer <= 0 then
                Scene.Load(self.stage.next_scene)
            end
        end
    end
}
