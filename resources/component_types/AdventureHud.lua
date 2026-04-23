local function DrawShadowText(text, x, y, size, r, g, b, a)
    Text.Draw(text, x + 2, y + 2, "NotoSans-Regular", size, 18, 22, 28, a)
    Text.Draw(text, x, y, "NotoSans-Regular", size, r, g, b, a)
end

local function DrawWorldHealth(component_name, y_offset)
    local actors = Actor.FindAll(component_name == "EnemyAI" and "enemy" or "altar")
    for index = 1, #actors do
        local actor = actors[index]
        local transform = actor:GetComponent("Transform")
        local component = actor:GetComponent(component_name)
        if transform ~= nil and component ~= nil then
            local health_units = component:GetHealthUnits()
            if health_units > 0 then
                AdventureShared.DrawHeartRowWorld(
                    transform.x, transform.y + y_offset,
                    health_units, component:GetMaxHealthUnits(),
                    0.68, 300)
            end
        end
    end
end

AdventureHud = {
    OnUpdate = function(self)
        local director_actor = Actor.Find("director")
        local director = nil
        if director_actor ~= nil then
            director = director_actor:GetComponent("DefenseDirector")
        end

        local player_actor = Actor.Find("player")
        local player = nil
        local player_transform = nil
        if player_actor ~= nil then
            player = player_actor:GetComponent("AdventurePlayer")
            player_transform = player_actor:GetComponent("Transform")
        end

        local scene_label = Scene.GetCurrent()
        local objective_text = "Defend the altar."
        local enemies_remaining = 0
        local status_message = ""
        if director ~= nil and director.stage ~= nil then
            scene_label = director.stage.scene_label
            objective_text = director:GetObjectiveText()
            enemies_remaining = director:GetEnemiesRemaining()
            status_message = director.status_message or ""
        end

        DrawShadowText(scene_label, 16, 12, 26, 244, 244, 240, 255)
        if player ~= nil then
            AdventureShared.DrawHeartRowUI(16, 42,
                                           player:GetHealthUnits(),
                                           player:GetMaxHealthUnits())
        end
        DrawShadowText("ENEMIES  " .. tostring(enemies_remaining),
                       16, 74, 18, 220, 236, 218, 255)
        DrawShadowText(objective_text, 16, 100, 16, 216, 226, 238, 255)

        local weapon_line = "Weapon: Sword"
        if player ~= nil and player:HasBow() then
            weapon_line = "Weapon: Bow"
        end
        if player ~= nil and player:HasShield() then
            weapon_line = weapon_line .. "   Shield: Ready"
        end
        DrawShadowText(weapon_line, 16, 126, 16, 246, 214, 178, 255)

        local controls_line = "Move WASD / Arrows   Attack Left Click / Space / J"
        if player ~= nil and player:HasShield() then
            controls_line = controls_line .. "   Shield Hold Right Click"
        end
        DrawShadowText(controls_line, 16, 334, 15, 220, 220, 220, 235)

        if status_message ~= "" then
            DrawShadowText(status_message, 124, 302, 18, 255, 236, 178, 255)
        end

        if player ~= nil and player:IsDead() then
            DrawShadowText("You fell. The defense rewinds...",
                           170, 160, 24, 255, 188, 188, 255)
        end

        if player ~= nil and player:IsShieldActive() and player_transform ~= nil then
            Image.DrawEx("shield",
                         player_transform.x, player_transform.y - 0.02,
                         0.0, 1.7, 1.7, 0.5, 0.5,
                         255, 255, 255, 235, 260)
        end

        DrawWorldHealth("EnemyAI", -0.72)
        DrawWorldHealth("DefenseTarget", -0.66)
    end
}
