DefenseTarget = {
    max_health_units = 12,
    touch_damage_flash_frames = 8,
    destroyed_scene_reload_frames = 100,

    OnStart = function(self)
        self.transform = self.actor:GetComponent("Transform")
        self.sprite_renderer = self.actor:GetComponent("SpriteRenderer")
        self.health_units = self.max_health_units
        self.flash_timer = 0
        self.destroyed_timer = 0
    end,

    GetHealthUnits = function(self)
        return self.health_units or self.max_health_units or 0
    end,

    GetMaxHealthUnits = function(self)
        return self.max_health_units
    end,

    IsDestroyed = function(self)
        return (self.health_units or self.max_health_units or 0) <= 0
    end,

    RestoreFull = function(self)
        self.health_units = self.max_health_units
        self.destroyed_timer = 0
    end,

    TakeDamage = function(self, amount)
        if self.health_units <= 0 then
            return
        end

        self.health_units = math.max(0, self.health_units - (amount or 1))
        self.flash_timer = self.touch_damage_flash_frames
        if self.health_units <= 0 then
            self.destroyed_timer = self.destroyed_scene_reload_frames
            if self.sprite_renderer ~= nil then
                self.sprite_renderer.sprite = "altar/altarBroken"
            end
        end
    end,

    OnUpdate = function(self)
        if self.sprite_renderer == nil then
            return
        end

        if self.health_units > 0 then
            self.sprite_renderer.sprite = "altar/altar"
        end

        if self.flash_timer > 0 then
            self.flash_timer = self.flash_timer - 1
        end

        if self.flash_timer > 0 and (self.flash_timer % 4) < 2 then
            self.sprite_renderer.r = 255
            self.sprite_renderer.g = 190
            self.sprite_renderer.b = 190
        else
            self.sprite_renderer.r = 255
            self.sprite_renderer.g = 255
            self.sprite_renderer.b = 255
        end
        self.sprite_renderer.a = 255
    end
}
