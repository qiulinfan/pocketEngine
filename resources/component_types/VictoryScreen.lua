local function DrawShadowText(text, x, y, size, r, g, b, a)
    Text.Draw(text, x + 2, y + 2, "NotoSans-Regular", size, 18, 22, 28, a)
    Text.Draw(text, x, y, "NotoSans-Regular", size, r, g, b, a)
end

VictoryScreen = {
    return_scene = "forest_glade",
    music_candidates = {"victory"},

    OnStart = function(self)
        Camera.SetPosition(0.0, 0.0)
        Camera.SetZoom(1.2)
        AdventureShared.PlayMusic(self.music_candidates)
    end,

    OnUpdate = function(self)
        Camera.SetPosition(0.0, 0.0)
        Camera.SetZoom(1.2)

        DrawShadowText("The altar still stands.", 142, 76, 34, 238, 246, 238, 255)
        DrawShadowText("Slimes, raiders, and the sword saint all fell before your watch.",
                       34, 136, 21, 220, 228, 236, 255)
        DrawShadowText("Press Space or Enter to begin another defense.",
                       104, 270, 22, 184, 244, 240, 255)

        if Input.GetKeyDown("space") or Input.GetKeyDown("return") or
           Input.GetKeyDown("enter") then
            AdventureShared.MarkReturningToFirstScene()
            Scene.Load(self.return_scene)
        end
    end
}
