AdventureProjectile = {
    projectile_kind = "arrow",
    speed = 0.06,
    damage = 2,
    radius = 0.16,
    lifetime_frames = 140,
    owner_kind = "player",
    target_category = "enemy",
    can_reflect = false,

    OnStart = function(self)
        self.transform = self.actor:GetComponent("Transform")
        self.sprite_renderer = self.actor:GetComponent("SpriteRenderer")
        self.velocity_x = self.velocity_x or 0.0
        self.velocity_y = self.velocity_y or 0.0
        self.remaining_frames = self.lifetime_frames
        self.reflected = false
        self:RefreshVisuals()
    end,

    RefreshVisuals = function(self)
        if self.transform == nil or self.sprite_renderer == nil then
            return
        end

        if self.projectile_kind == "sword_wave" then
            self.sprite_renderer.sprite = "Enemies/GoblinSwordSaint/YellowFIRE"
            self.sprite_renderer.scale_x = 1.0
            self.sprite_renderer.scale_y = 1.0
            self.sprite_renderer:SetSpriteCell(3, 4)
            local rotation = math.deg(math.atan(self.velocity_y, self.velocity_x))
            self.transform.rotation = rotation
            if self.reflected then
                self.sprite_renderer.r = 150
                self.sprite_renderer.g = 235
                self.sprite_renderer.b = 255
            else
                self.sprite_renderer.r = 255
                self.sprite_renderer.g = 220
                self.sprite_renderer.b = 140
            end
            self.sprite_renderer.a = 255
            return
        end

        AdventureShared.ConfigureArrowSprite(self.sprite_renderer,
                                             self.velocity_x, self.velocity_y)
        self.sprite_renderer.r = 255
        self.sprite_renderer.g = 255
        self.sprite_renderer.b = 255
        self.sprite_renderer.a = 255
        self.transform.rotation = 0.0
    end,

    Reflect = function(self)
        AdventureShared.PlayShieldBlock()
        self.velocity_x = -self.velocity_x
        self.velocity_y = -self.velocity_y
        self.owner_kind = "player"
        self.target_category = "enemy"
        self.reflected = true
        self.can_reflect = false
        self.damage = self.damage + 2
        self.remaining_frames = math.max(self.remaining_frames, 70)
        self:RefreshVisuals()
    end,

    HitEnemy = function(self)
        local enemies = Actor.FindAll("enemy")
        for index = 1, #enemies do
            local enemy = enemies[index]
            local enemy_ai = enemy:GetComponent("EnemyAI")
            local enemy_transform = enemy:GetComponent("Transform")
            if enemy_ai ~= nil and enemy_transform ~= nil and enemy_ai:IsAlive() then
                local distance = AdventureShared.Distance(
                    self.transform.x, self.transform.y,
                    enemy_transform.x, enemy_transform.y)
                if distance <= self.radius + enemy_ai.hit_radius then
                    enemy_ai:TakeDamage(self.damage, self.velocity_x,
                                        self.velocity_y, self.projectile_kind)
                    Actor.Destroy(self.actor)
                    return true
                end
            end
        end
        return false
    end,

    HitPlayer = function(self)
        local player_actor = Actor.Find("player")
        if player_actor == nil then
            return false
        end

        local player = player_actor:GetComponent("AdventurePlayer")
        local player_transform = player_actor:GetComponent("Transform")
        if player == nil or player_transform == nil or player:IsDead() then
            return false
        end

        local distance = AdventureShared.Distance(
            self.transform.x, self.transform.y,
            player_transform.x, player_transform.y)

        if self.can_reflect and player:IsShieldActive() and
           distance <= player:GetShieldRadius() then
            self:Reflect()
            return false
        end

        if distance <= self.radius + player.hit_radius then
            player:TakeDamage(self.damage, self.velocity_x, self.velocity_y)
            Actor.Destroy(self.actor)
            return true
        end
        return false
    end,

    HitAltar = function(self)
        local altar_actor = Actor.Find("altar")
        if altar_actor == nil then
            return false
        end

        local altar = altar_actor:GetComponent("DefenseTarget")
        local altar_transform = altar_actor:GetComponent("Transform")
        if altar == nil or altar_transform == nil or altar:IsDestroyed() then
            return false
        end

        local distance = AdventureShared.Distance(
            self.transform.x, self.transform.y,
            altar_transform.x, altar_transform.y)
        if distance <= self.radius + 0.32 then
            altar:TakeDamage(self.damage)
            Actor.Destroy(self.actor)
            return true
        end
        return false
    end,

    OnUpdate = function(self)
        if self.transform == nil then
            return
        end

        self.remaining_frames = self.remaining_frames - 1
        if self.remaining_frames <= 0 then
            Actor.Destroy(self.actor)
            return
        end

        self.transform.x = self.transform.x + self.velocity_x * self.speed
        self.transform.y = self.transform.y + self.velocity_y * self.speed

        if self.projectile_kind == "sword_wave" then
            self.transform.rotation =
                math.deg(math.atan(self.velocity_y, self.velocity_x))
        end

        if self.transform.x < -3.6 or self.transform.x > 3.6 or
           self.transform.y < -2.4 or self.transform.y > 2.4 then
            Actor.Destroy(self.actor)
            return
        end

        if self.target_category == "enemy" then
            self:HitEnemy()
            return
        end

        if self.target_category == "player" then
            self:HitPlayer()
            return
        end

        if self.target_category == "altar" then
            self:HitAltar()
            return
        end

        if self.target_category == "player_or_altar" then
            if self:HitPlayer() then
                return
            end
            self:HitAltar()
        end
    end
}
