AdventurePickup = {
    pickup_type = "bow",
    bob_height = 0.06,
    bob_speed = 0.12,
    pulse_speed = 0.09,

    OnStart = function(self)
        self.transform = self.actor:GetComponent("Transform")
        self.sprite_renderer = self.actor:GetComponent("SpriteRenderer")
        self.base_y = (self.transform ~= nil) and self.transform.y or 0.0
        self:ApplyVisual()
    end,

    ApplyVisual = function(self)
        if self.sprite_renderer == nil then
            return
        end

        if self.pickup_type == "shield" then
            self.sprite_renderer.sprite = "shield"
            self.base_scale = 1.35
            self.sprite_renderer.scale_x = self.base_scale
            self.sprite_renderer.scale_y = self.base_scale
            self.sprite_renderer.r = 255
            self.sprite_renderer.g = 255
            self.sprite_renderer.b = 255
            self.sprite_renderer.a = 255
            return
        end

        self.sprite_renderer.sprite = "bow"
        self.base_scale = 0.78
        self.sprite_renderer.scale_x = self.base_scale
        self.sprite_renderer.scale_y = self.base_scale
        self.sprite_renderer.r = 255
        self.sprite_renderer.g = 255
        self.sprite_renderer.b = 255
        self.sprite_renderer.a = 255
    end,

    OnCollect = function(self)
        AdventureShared.PlayPickup()
        local state = AdventureShared.GetState()
        if self.pickup_type == "shield" then
            state.shield_unlocked = true
        else
            state.bow_unlocked = true
        end

        local director_actor = Actor.Find("director")
        if director_actor ~= nil then
            local director = director_actor:GetComponent("DefenseDirector")
            if director ~= nil then
                director:HandlePickupCollected(self.pickup_type)
            end
        end

        local player_actor = Actor.Find("player")
        if player_actor ~= nil then
            local player = player_actor:GetComponent("AdventurePlayer")
            if player ~= nil then
                player:RestoreFullHealth()
            end
        end

        Actor.Destroy(self.actor)
    end,

    OnUpdate = function(self)
        if self.transform == nil or self.sprite_renderer == nil then
            return
        end

        local bob = math.sin(Application.GetFrame() * self.bob_speed) * self.bob_height
        self.transform.y = self.base_y + bob

        local pulse = 0.96 + 0.08 * math.sin(Application.GetFrame() * self.pulse_speed)
        self.sprite_renderer.scale_x = self.base_scale * pulse
        self.sprite_renderer.scale_y = self.base_scale * pulse

        local player_actor = Actor.Find("player")
        if player_actor == nil then
            return
        end

        local player_transform = player_actor:GetComponent("Transform")
        if player_transform == nil then
            return
        end

        if AdventureShared.Distance(self.transform.x, self.transform.y,
                                    player_transform.x, player_transform.y) <= 0.34 then
            self:OnCollect()
        end
    end
}
