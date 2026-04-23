local function ChooseDirectionVisual(self, facing_x, facing_y)
    if self.enemy_kind == "slime" then
        return AdventureShared.SlimeDirectionRow(facing_x, facing_y)
    end
    return AdventureShared.StandardDirectionRow(facing_x, facing_y)
end

local function ReadStyle(self)
    if self.enemy_kind == "slime" then
        return {
            idle_sprite = "Enemies/Slime/Idle",
            walk_sprite = "Enemies/Slime/Walk",
            attack_sprite = "Enemies/Slime/Attack",
            damage_sprite = "Enemies/Slime/Damage",
            death_sprite = "Enemies/Slime/Death",
            idle_frames = 4,
            walk_frames = 4,
            attack_frames = 4,
            damage_frames = 4,
            death_frames = 4
        }
    end

    if self.enemy_kind == "sword_saint" then
        return {
            idle_sprite = "Enemies/GoblinSwordSaint/Idle",
            walk_sprite = "Enemies/GoblinSwordSaint/Walk",
            attack_sprite = "Enemies/GoblinSwordSaint/Attack",
            damage_sprite = "Enemies/GoblinSwordSaint/Walk",
            death_sprite = "Enemies/GoblinSwordSaint/Death",
            idle_frames = 2,
            walk_frames = 6,
            attack_frames = 9,
            damage_frames = 6,
            death_frames = 6
        }
    end

    return {
        idle_sprite = "Enemies/Spear Goblin/Idle",
        walk_sprite = "Enemies/Spear Goblin/Run",
        attack_sprite = "Enemies/Spear Goblin/Spear",
        damage_sprite = "Enemies/Spear Goblin/Damage",
        death_sprite = "Enemies/Spear Goblin/Dead",
        idle_frames = 4,
        walk_frames = 8,
        attack_frames = 6,
        damage_frames = 4,
        death_frames = 4
    }
end

local function IsActorTargetable(actor)
    if actor == nil then
        return false
    end

    local player = actor:GetComponent("AdventurePlayer")
    if player ~= nil then
        return not player:IsDead()
    end

    local altar = actor:GetComponent("DefenseTarget")
    if altar ~= nil then
        return not altar:IsDestroyed()
    end

    return false
end

EnemyAI = {
    enemy_kind = "goblin",
    sprite_scale = 2.0,
    target_preference = "player",
    move_speed = 0.016,
    max_health_units = 6,
    attack_damage = 2,
    attack_range = 0.34,
    attack_duration_frames = 24,
    attack_hit_frame = 11,
    attack_cooldown_frames = 34,
    damage_stun_frames = 10,
    corpse_frames = 32,
    hit_radius = 0.2,
    boss_wave_min_frames = 90,
    boss_wave_max_frames = 150,
    boss_wave_damage = 2,
    boss_wave_speed = 0.072,

    OnStart = function(self)
        self.transform = self.actor:GetComponent("Transform")
        self.sprite_renderer = self.actor:GetComponent("SpriteRenderer")
        self.health_units = self.max_health_units
        self.attack_timer = 0
        self.attack_cooldown = 0
        self.damage_timer = 0
        self.corpse_timer = 0
        self.has_landed_attack = false
        self.facing_x = 0.0
        self.facing_y = 1.0
        self.target_name = nil
        self.retarget_timer = 0
        self.boss_wave_timer = math.random(self.boss_wave_min_frames,
                                           self.boss_wave_max_frames)
        self.style = ReadStyle(self)
    end,

    IsAlive = function(self)
        return (self.health_units or self.max_health_units or 0) > 0
    end,

    GetHealthUnits = function(self)
        return self.health_units or self.max_health_units or 0
    end,

    GetMaxHealthUnits = function(self)
        return self.max_health_units
    end,

    ResolveTargetActor = function(self)
        local altar = Actor.Find("altar")
        local player = Actor.Find("player")

        if self.enemy_kind == "slime" or self.target_preference == "altar" then
            if IsActorTargetable(altar) then
                return altar
            end
            return player
        end

        if self.enemy_kind == "sword_saint" or self.target_preference == "player" then
            if IsActorTargetable(player) then
                return player
            end
            return altar
        end

        if self.retarget_timer <= 0 or self.target_name == nil then
            self.retarget_timer = 90
            if IsActorTargetable(player) and IsActorTargetable(altar) then
                self.target_name = (math.random(1, 100) <= 50) and "player" or "altar"
            elseif IsActorTargetable(player) then
                self.target_name = "player"
            else
                self.target_name = "altar"
            end
        else
            self.retarget_timer = self.retarget_timer - 1
        end

        if self.target_name == "altar" and IsActorTargetable(altar) then
            return altar
        end
        if IsActorTargetable(player) then
            return player
        end
        return altar
    end,

    TakeDamage = function(self, amount, hit_x, hit_y, damage_kind)
        if self.corpse_timer > 0 then
            return
        end

        local final_amount = amount or 1
        if self.enemy_kind == "sword_saint" and damage_kind == "sword_wave" then
            final_amount = final_amount + 2
        end

        self.health_units = math.max(0, self.health_units - final_amount)
        self.damage_timer = self.damage_stun_frames
        self.attack_timer = 0
        self.attack_cooldown = self.attack_cooldown_frames

        if self.transform ~= nil then
            self.transform.x = self.transform.x + (hit_x or 0.0) * 0.12
            self.transform.y = self.transform.y + (hit_y or 0.0) * 0.12
        end

        if self.health_units <= 0 then
            self.corpse_timer = self.corpse_frames
        end
    end,

    DealAttackDamage = function(self, target_actor, move_x, move_y)
        if target_actor == nil then
            return
        end

        local player = target_actor:GetComponent("AdventurePlayer")
        if player ~= nil then
            player:TakeDamage(self.attack_damage, move_x, move_y)
            return
        end

        local altar = target_actor:GetComponent("DefenseTarget")
        if altar ~= nil then
            altar:TakeDamage(self.attack_damage)
        end
    end,

    HitTargetsInMeleeRange = function(self, move_x, move_y)
        local player_actor = Actor.Find("player")
        if player_actor ~= nil then
            local player = player_actor:GetComponent("AdventurePlayer")
            local player_transform = player_actor:GetComponent("Transform")
            if player ~= nil and player_transform ~= nil and not player:IsDead() then
                local player_distance = AdventureShared.Distance(
                    self.transform.x, self.transform.y,
                    player_transform.x, player_transform.y)
                if player_distance <= self.attack_range + player.hit_radius then
                    player:TakeDamage(self.attack_damage, move_x, move_y)
                end
            end
        end

        local altar_actor = Actor.Find("altar")
        if altar_actor ~= nil then
            local altar = altar_actor:GetComponent("DefenseTarget")
            local altar_transform = altar_actor:GetComponent("Transform")
            if altar ~= nil and altar_transform ~= nil and not altar:IsDestroyed() then
                local altar_distance = AdventureShared.Distance(
                    self.transform.x, self.transform.y,
                    altar_transform.x, altar_transform.y)
                if altar_distance <= self.attack_range + 0.32 then
                    altar:TakeDamage(self.attack_damage)
                end
            end
        end
    end,

    SpawnSwordWave = function(self, direction_x, direction_y)
        AdventureShared.PlaySwordWave()
        local projectile_actor = Actor.Instantiate("AdventureProjectile")
        if projectile_actor == nil then
            return
        end

        local projectile_transform = projectile_actor:GetComponent("Transform")
        local projectile = projectile_actor:GetComponent("AdventureProjectile")
        local sprite_renderer = projectile_actor:GetComponent("SpriteRenderer")
        if projectile_transform == nil or projectile == nil or self.transform == nil then
            return
        end

        projectile_transform.x = self.transform.x + direction_x * 0.34
        projectile_transform.y = self.transform.y + direction_y * 0.34
        projectile.projectile_kind = "sword_wave"
        projectile.speed = self.boss_wave_speed
        projectile.damage = self.boss_wave_damage
        projectile.owner_kind = "enemy"
        projectile.target_category = "player"
        projectile.can_reflect = true
        projectile.velocity_x = direction_x
        projectile.velocity_y = direction_y
        projectile.lifetime_frames = 120
        projectile.radius = 0.16
        if sprite_renderer ~= nil then
            sprite_renderer.auto_sorting_order = false
            sprite_renderer.sorting_order = 260
        end
        projectile:RefreshVisuals()
    end,

    OnUpdate = function(self)
        if self.transform == nil or self.sprite_renderer == nil then
            return
        end

        if self.attack_cooldown > 0 then
            self.attack_cooldown = self.attack_cooldown - 1
        end

        local direction_row, horizontal_scale =
            ChooseDirectionVisual(self, self.facing_x, self.facing_y)

        if self.corpse_timer > 0 then
            self.corpse_timer = self.corpse_timer - 1
            self.sprite_renderer.sprite = self.style.death_sprite
            self.sprite_renderer.scale_x = horizontal_scale * self.sprite_scale
            self.sprite_renderer.scale_y = self.sprite_scale
            self.sprite_renderer:SetSpriteCell(direction_row,
                math.min(self.style.death_frames,
                    1 + math.floor((self.corpse_frames - self.corpse_timer) / 7)))
            self.sprite_renderer.r = 255
            self.sprite_renderer.g = 255
            self.sprite_renderer.b = 255
            self.sprite_renderer.a = 230
            if self.corpse_timer <= 0 then
                Actor.Destroy(self.actor)
            end
            return
        end

        local target_actor = self:ResolveTargetActor()
        local target_transform = nil
        if target_actor ~= nil then
            target_transform = target_actor:GetComponent("Transform")
        end

        local move_x = 0.0
        local move_y = 0.0
        local distance = 999.0
        if target_transform ~= nil then
            move_x, move_y, distance = AdventureShared.Normalize(
                target_transform.x - self.transform.x,
                target_transform.y - self.transform.y)
            if move_x ~= 0.0 or move_y ~= 0.0 then
                self.facing_x = move_x
                self.facing_y = move_y
                direction_row, horizontal_scale =
                    ChooseDirectionVisual(self, self.facing_x, self.facing_y)
            end
        end

        if self.damage_timer > 0 then
            self.damage_timer = self.damage_timer - 1
            self.sprite_renderer.sprite = self.style.damage_sprite
            self.sprite_renderer.scale_x = horizontal_scale * self.sprite_scale
            self.sprite_renderer.scale_y = self.sprite_scale
            self.sprite_renderer:SetSpriteCell(direction_row,
                AdventureShared.AnimationFrame(self.style.damage_frames, 5))
            self.sprite_renderer.r = 255
            self.sprite_renderer.g = 185
            self.sprite_renderer.b = 185
            self.sprite_renderer.a = 255
            return
        end

        if self.enemy_kind == "sword_saint" and self.attack_timer <= 0 and
           self.damage_timer <= 0 then
            self.boss_wave_timer = self.boss_wave_timer - 1
            if self.boss_wave_timer <= 0 and distance <= 3.2 then
                self:SpawnSwordWave(move_x, move_y)
                self.boss_wave_timer = math.random(self.boss_wave_min_frames,
                                                   self.boss_wave_max_frames)
            end
        end

        if self.attack_timer > 0 then
            local attack_elapsed = self.attack_duration_frames - self.attack_timer
            if not self.has_landed_attack and attack_elapsed >= self.attack_hit_frame then
                self:HitTargetsInMeleeRange(move_x, move_y)
                self.has_landed_attack = true
            end

            self.attack_timer = self.attack_timer - 1
            self.sprite_renderer.sprite = self.style.attack_sprite
            self.sprite_renderer.scale_x = horizontal_scale * self.sprite_scale
            self.sprite_renderer.scale_y = self.sprite_scale
            self.sprite_renderer:SetSpriteCell(direction_row,
                math.min(self.style.attack_frames,
                    1 + math.floor(attack_elapsed * self.style.attack_frames /
                                   self.attack_duration_frames)))
            self.sprite_renderer.r = 255
            self.sprite_renderer.g = 255
            self.sprite_renderer.b = 255
            self.sprite_renderer.a = 255
            return
        end

        if distance <= self.attack_range and self.attack_cooldown <= 0 then
            self.attack_timer = self.attack_duration_frames
            self.has_landed_attack = false
            self.sprite_renderer.sprite = self.style.attack_sprite
            self.sprite_renderer.scale_x = horizontal_scale * self.sprite_scale
            self.sprite_renderer.scale_y = self.sprite_scale
            self.sprite_renderer:SetSpriteCell(direction_row, 1)
            return
        end

        if distance < 3.6 and target_actor ~= nil then
            self.transform.x = self.transform.x + move_x * self.move_speed
            self.transform.y = self.transform.y + move_y * self.move_speed
            self.sprite_renderer.sprite = self.style.walk_sprite
            self.sprite_renderer.scale_x = horizontal_scale * self.sprite_scale
            self.sprite_renderer.scale_y = self.sprite_scale
            self.sprite_renderer:SetSpriteCell(direction_row,
                AdventureShared.AnimationFrame(self.style.walk_frames, 6))
        else
            self.sprite_renderer.sprite = self.style.idle_sprite
            self.sprite_renderer.scale_x = horizontal_scale * self.sprite_scale
            self.sprite_renderer.scale_y = self.sprite_scale
            self.sprite_renderer:SetSpriteCell(direction_row,
                AdventureShared.AnimationFrame(self.style.idle_frames, 10))
        end

        self.sprite_renderer.r = 255
        self.sprite_renderer.g = 255
        self.sprite_renderer.b = 255
        self.sprite_renderer.a = 255
    end
}
