local function CardinalizeDirection(dx, dy, fallback_x, fallback_y)
    if math.abs(dx) <= 0.0001 and math.abs(dy) <= 0.0001 then
        return fallback_x or 0.0, fallback_y or 1.0
    end

    if math.abs(dx) > math.abs(dy) then
        return (dx < 0.0) and -1.0 or 1.0, 0.0
    end
    return 0.0, (dy < 0.0) and -1.0 or 1.0
end

local function GetMouseWorldPosition()
    local mouse_position = Input.GetMousePosition()
    local zoom = math.max(0.01, Camera.GetZoom())
    local window_width, window_height = AdventureShared.GetWindowSize()

    local world_x = Camera.GetPositionX() +
        (mouse_position.x - window_width * 0.5) / (100.0 * zoom)
    local world_y = Camera.GetPositionY() +
        (mouse_position.y - window_height * 0.5) / (100.0 * zoom)
    return world_x, world_y
end

AdventurePlayer = {
    move_speed = 0.03,
    sprite_scale = 2.0,
    shield_move_factor = 0.58,
    max_health_units = 10,
    sword_damage = 2,
    sword_range = 0.58,
    sword_attack_duration_frames = 18,
    sword_hit_frame = 7,
    bow_damage = 2,
    bow_attack_duration_frames = 16,
    bow_release_frame = 6,
    bow_projectile_speed = 0.082,
    invulnerability_frames = 38,
    death_reload_frames = 90,
    hit_radius = 0.2,
    shield_radius = 0.34,
    min_x = -2.7,
    max_x = 2.7,
    min_y = -1.7,
    max_y = 1.7,

    OnStart = function(self)
        local scene_name = Scene.GetCurrent()
        local stage_min_x, stage_max_x, stage_min_y, stage_max_y =
            AdventureShared.GetStageBounds(scene_name)
        local edge_padding = math.max(0.12, self.hit_radius)
        self.min_x = stage_min_x + edge_padding
        self.max_x = stage_max_x - edge_padding
        self.min_y = stage_min_y + edge_padding
        self.max_y = stage_max_y - edge_padding

        self.transform = self.actor:GetComponent("Transform")
        self.sprite_renderer = self.actor:GetComponent("SpriteRenderer")
        if self.transform ~= nil then
            self.transform.x = AdventureShared.Clamp(self.transform.x, self.min_x, self.max_x)
            self.transform.y = AdventureShared.Clamp(self.transform.y, self.min_y, self.max_y)
        end
        self.health_units = self.max_health_units
        self.facing_x = 0.0
        self.facing_y = 1.0
        self.attack_timer = 0
        self.attack_kind = ""
        self.invulnerability_timer = 0
        self.death_timer = 0
        self.attacked_targets = {}
        self.pending_shot_x = 0.0
        self.pending_shot_y = -1.0

        local state = AdventureShared.GetState()
        if state.equipped_weapon == nil or state.equipped_weapon == "" then
            state.equipped_weapon = "sword"
        end
        if not state.bow_unlocked and state.equipped_weapon == "bow" then
            state.equipped_weapon = "sword"
        end
    end,

    GetHealthUnits = function(self)
        return self.health_units or self.max_health_units or 0
    end,

    GetMaxHealthUnits = function(self)
        return self.max_health_units
    end,

    RestoreFullHealth = function(self)
        self.health_units = self.max_health_units
        self.death_timer = 0
        self.invulnerability_timer = 0
    end,

    IsDead = function(self)
        return self.death_timer > 0
    end,

    HasBow = function(self)
        return AdventureShared.GetState().bow_unlocked
    end,

    HasShield = function(self)
        return AdventureShared.GetState().shield_unlocked
    end,

    IsBowEquipped = function(self)
        local state = AdventureShared.GetState()
        return state.bow_unlocked and state.equipped_weapon == "bow"
    end,

    EquipWeapon = function(self, weapon_kind)
        local state = AdventureShared.GetState()
        if weapon_kind == "bow" then
            if state.bow_unlocked then
                state.equipped_weapon = "bow"
            end
            return
        end
        state.equipped_weapon = "sword"
    end,

    IsShieldActive = function(self)
        return self:HasShield() and not self:IsDead() and
            Input.GetMouseButton(3) and self.attack_timer <= 0
    end,

    GetShieldRadius = function(self)
        return self.shield_radius
    end,

    TakeDamage = function(self, amount, source_x, source_y)
        if self:IsDead() or self.invulnerability_timer > 0 then
            return
        end

        AdventureShared.PlayPlayerDamaged()
        self.health_units = math.max(0, self.health_units - (amount or 1))
        self.invulnerability_timer = self.invulnerability_frames

        if self.transform ~= nil then
            self.transform.x = AdventureShared.Clamp(
                self.transform.x + (source_x or 0.0) * 0.1, self.min_x, self.max_x)
            self.transform.y = AdventureShared.Clamp(
                self.transform.y + (source_y or 0.0) * 0.1, self.min_y, self.max_y)
        end

        if self.health_units <= 0 then
            self.death_timer = self.death_reload_frames
        end
    end,

    HitEnemiesWithSword = function(self)
        local enemies = Actor.FindAll("enemy")
        for index = 1, #enemies do
            local enemy_actor = enemies[index]
            local enemy_uid = enemy_actor:GetUID()
            if self.attacked_targets[enemy_uid] == nil then
                local enemy = enemy_actor:GetComponent("EnemyAI")
                local enemy_transform = enemy_actor:GetComponent("Transform")
                if enemy ~= nil and enemy_transform ~= nil and enemy:IsAlive() then
                    local dx = enemy_transform.x - self.transform.x
                    local dy = enemy_transform.y - self.transform.y
                    local distance_sq = dx * dx + dy * dy
                    if distance_sq <= self.sword_range * self.sword_range then
                        enemy:TakeDamage(self.sword_damage, self.facing_x,
                                         self.facing_y, "sword")
                        self.attacked_targets[enemy_uid] = true
                    end
                end
            end
        end
    end,

    FireArrow = function(self)
        local projectile_actor = Actor.Instantiate("AdventureProjectile")
        if projectile_actor == nil then
            return
        end

        local projectile_transform = projectile_actor:GetComponent("Transform")
        local projectile = projectile_actor:GetComponent("AdventureProjectile")
        local sprite_renderer = projectile_actor:GetComponent("SpriteRenderer")
        if projectile_transform == nil or projectile == nil then
            return
        end

        local shot_x = self.pending_shot_x or self.facing_x
        local shot_y = self.pending_shot_y or self.facing_y
        if math.abs(shot_x) <= 0.0001 and math.abs(shot_y) <= 0.0001 then
            shot_x = self.facing_x
            shot_y = self.facing_y
        end
        projectile_transform.x = self.transform.x + shot_x * 0.24
        projectile_transform.y = self.transform.y + shot_y * 0.24
        projectile.projectile_kind = "arrow"
        projectile.speed = self.bow_projectile_speed
        projectile.damage = self.bow_damage
        projectile.owner_kind = "player"
        projectile.target_category = "enemy"
        projectile.velocity_x = shot_x
        projectile.velocity_y = shot_y
        projectile.lifetime_frames = 90
        projectile.radius = 0.14
        if sprite_renderer ~= nil then
            sprite_renderer.auto_sorting_order = false
            sprite_renderer.sorting_order = 240
        end
        projectile:RefreshVisuals()
    end,

    OnUpdate = function(self)
        if self.transform == nil or self.sprite_renderer == nil then
            return
        end

        if self.invulnerability_timer > 0 then
            self.invulnerability_timer = self.invulnerability_timer - 1
        end

        if self.death_timer > 0 then
            self.death_timer = self.death_timer - 1
            self.sprite_renderer.sprite = "Player/Dead"
            self.sprite_renderer.scale_x = self.sprite_scale
            self.sprite_renderer.scale_y = self.sprite_scale
            self.sprite_renderer:SetSpriteCell(1, math.min(4,
                1 + math.floor((self.death_reload_frames - self.death_timer) / 12)))
            self.sprite_renderer.r = 255
            self.sprite_renderer.g = 255
            self.sprite_renderer.b = 255
            self.sprite_renderer.a = 255
            if self.death_timer <= 0 then
                Scene.Load(Scene.GetCurrent())
            end
            return
        end

        local move_x = 0.0
        local move_y = 0.0
        if Input.GetKey("a") or Input.GetKey("left") then
            move_x = move_x - 1.0
        end
        if Input.GetKey("d") or Input.GetKey("right") then
            move_x = move_x + 1.0
        end
        if Input.GetKey("w") or Input.GetKey("up") then
            move_y = move_y - 1.0
        end
        if Input.GetKey("s") or Input.GetKey("down") then
            move_y = move_y + 1.0
        end
        move_x, move_y = AdventureShared.Normalize(move_x, move_y)

        if Input.GetKeyDown("1") then
            self:EquipWeapon("sword")
        elseif Input.GetKeyDown("2") then
            self:EquipWeapon("bow")
        end

        local mouse_attack_click = Input.GetMouseButtonDown(1)
        local mouse_click =
            mouse_attack_click or
            Input.GetMouseButtonDown(2) or
            Input.GetMouseButtonDown(3)
        local mouse_world_x, mouse_world_y = GetMouseWorldPosition()
        local mouse_aim_x, mouse_aim_y = AdventureShared.Normalize(
            mouse_world_x - self.transform.x,
            mouse_world_y - self.transform.y)
        local hover_facing_x, hover_facing_y = CardinalizeDirection(
            mouse_world_x - self.transform.x,
            mouse_world_y - self.transform.y,
            self.facing_x, self.facing_y)
        local move_facing_x, move_facing_y = CardinalizeDirection(
            move_x, move_y, self.facing_x, self.facing_y)

        local shield_active = self:IsShieldActive()
        local move_speed = self.move_speed
        if shield_active then
            move_speed = move_speed * self.shield_move_factor
        end

        if mouse_click then
            self.facing_x = hover_facing_x
            self.facing_y = hover_facing_y
        elseif self.attack_timer <= 0 and (move_x ~= 0.0 or move_y ~= 0.0) then
            self.facing_x = move_facing_x
            self.facing_y = move_facing_y
        elseif self.attack_timer <= 0 then
            self.facing_x = hover_facing_x
            self.facing_y = hover_facing_y
        end

        if self.attack_timer <= 0 and (move_x ~= 0.0 or move_y ~= 0.0) then
            self.transform.x = AdventureShared.Clamp(
                self.transform.x + move_x * move_speed, self.min_x, self.max_x)
            self.transform.y = AdventureShared.Clamp(
                self.transform.y + move_y * move_speed, self.min_y, self.max_y)
        end

        local wants_attack =
            Input.GetKeyDown("space") or Input.GetKeyDown("j") or
            mouse_attack_click
        if self.attack_timer <= 0 and wants_attack and not shield_active then
            self.attacked_targets = {}
            if self:IsBowEquipped() then
                if math.abs(mouse_aim_x) > 0.0001 or math.abs(mouse_aim_y) > 0.0001 then
                    self.pending_shot_x = mouse_aim_x
                    self.pending_shot_y = mouse_aim_y
                else
                    self.pending_shot_x = self.facing_x
                    self.pending_shot_y = self.facing_y
                end
                AdventureShared.PlayArrowShot()
                self.attack_kind = "bow"
                self.attack_timer = self.bow_attack_duration_frames
            else
                AdventureShared.PlaySwordSwing()
                self.attack_kind = "sword"
                self.attack_timer = self.sword_attack_duration_frames
            end
        end

        local direction_row, horizontal_scale =
            AdventureShared.StandardDirectionRow(self.facing_x, self.facing_y)

        if self.attack_timer > 0 then
            if self.attack_kind == "bow" then
                local attack_elapsed = self.bow_attack_duration_frames - self.attack_timer
                if attack_elapsed == self.bow_release_frame then
                    self:FireArrow()
                end
                self.sprite_renderer.sprite = "Player/Bow and Arrow"
                self.sprite_renderer.scale_x = horizontal_scale * self.sprite_scale
                self.sprite_renderer.scale_y = self.sprite_scale
                self.sprite_renderer:SetSpriteCell(direction_row,
                    math.min(7,
                        1 + math.floor(attack_elapsed * 7 /
                                       self.bow_attack_duration_frames)))
            else
                local attack_elapsed = self.sword_attack_duration_frames - self.attack_timer
                if attack_elapsed == self.sword_hit_frame then
                    self:HitEnemiesWithSword()
                end
                self.sprite_renderer.sprite = "Player/Sword"
                self.sprite_renderer.scale_x = horizontal_scale * self.sprite_scale
                self.sprite_renderer.scale_y = self.sprite_scale
                self.sprite_renderer:SetSpriteCell(direction_row,
                    math.min(10,
                        1 + math.floor(attack_elapsed * 10 /
                                       self.sword_attack_duration_frames)))
            end
            self.attack_timer = self.attack_timer - 1
        elseif move_x ~= 0.0 or move_y ~= 0.0 then
            self.sprite_renderer.sprite = "Player/Run"
            self.sprite_renderer.scale_x = horizontal_scale * self.sprite_scale
            self.sprite_renderer.scale_y = self.sprite_scale
            self.sprite_renderer:SetSpriteCell(direction_row,
                AdventureShared.AnimationFrame(8, 5))
        else
            self.sprite_renderer.sprite = "Player/Idle"
            self.sprite_renderer.scale_x = horizontal_scale * self.sprite_scale
            self.sprite_renderer.scale_y = self.sprite_scale
            self.sprite_renderer:SetSpriteCell(direction_row,
                AdventureShared.AnimationFrame(4, 10))
        end

        if shield_active then
            self.sprite_renderer.r = 210
            self.sprite_renderer.g = 235
            self.sprite_renderer.b = 255
        elseif self.invulnerability_timer > 0 and
               (self.invulnerability_timer % 6) < 3 then
            self.sprite_renderer.r = 255
            self.sprite_renderer.g = 180
            self.sprite_renderer.b = 180
        else
            self.sprite_renderer.r = 255
            self.sprite_renderer.g = 255
            self.sprite_renderer.b = 255
        end
        self.sprite_renderer.a = 255
    end
}
