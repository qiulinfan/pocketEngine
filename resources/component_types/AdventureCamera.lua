AdventureCamera = {
    ease_factor = 0.1,
    zoom = 1.35,
    snap_on_start = true,

    OnStart = function(self)
        self.has_snapped = false
        local scene_name = Scene.GetCurrent()
        self.stage_min_x, self.stage_max_x,
        self.stage_min_y, self.stage_max_y =
            AdventureShared.GetStageBounds(scene_name)
    end,

    OnUpdate = function(self)
        Camera.SetZoom(self.zoom)

        local player_actor = Actor.Find("player")
        if player_actor == nil then
            return
        end

        local player_transform = player_actor:GetComponent("Transform")
        if player_transform == nil then
            return
        end

        local target_x = player_transform.x
        local target_y = player_transform.y
        local half_width, half_height =
            AdventureShared.GetViewportHalfExtents(self.zoom)
        local min_camera_x = self.stage_min_x + half_width
        local max_camera_x = self.stage_max_x - half_width
        local min_camera_y = self.stage_min_y + half_height
        local max_camera_y = self.stage_max_y - half_height

        if min_camera_x > max_camera_x then
            local center_x = (self.stage_min_x + self.stage_max_x) * 0.5
            min_camera_x = center_x
            max_camera_x = center_x
        end
        if min_camera_y > max_camera_y then
            local center_y = (self.stage_min_y + self.stage_max_y) * 0.5
            min_camera_y = center_y
            max_camera_y = center_y
        end

        target_x = AdventureShared.Clamp(target_x, min_camera_x, max_camera_x)
        target_y = AdventureShared.Clamp(target_y, min_camera_y, max_camera_y)

        if self.snap_on_start and not self.has_snapped then
            Camera.SetPosition(target_x, target_y)
            self.has_snapped = true
            return
        end

        Camera.SetPosition(
            AdventureShared.Lerp(Camera.GetPositionX(), target_x, self.ease_factor),
            AdventureShared.Lerp(Camera.GetPositionY(), target_y, self.ease_factor))
    end
}
