GameManager = {

	-- TILE CODES --
	-- 0 : nothing
	-- 1 : Static box
	-- 2 : player

	stage1 = {
		{1, 0, 0, 0, 0, 4, 4, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, -- 20x20
		{1, 0, 0, 0, 0, 4, 4, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
		{1, 0, 0, 0, 0, 4, 4, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
		{1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
		{1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
		{1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
		{1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
		{1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
		{1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
		{1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
		{1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
		{1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
		{1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
		{1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
		{1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 3, 3, 1, 1, 1, 0, 0, 1},
		{1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1},
		{1, 0, 2, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1},
		{1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1},
		{1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1},
		{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1},
	},

	OnStart = function(self)
		local walls_root = Actor.Find("Walls")
		local visited = {}
		for y=1,20 do
			visited[y] = {}
		end

		-- Spawn stage.  Merge adjacent matching tiles into larger rectangles so
		-- the sample scene does not create 150 tiny physics bodies.
		for y=1,20 do 
			for x = 1,20 do
				local tile_code = self.stage1[y][x]
				local tile_pos = Vector2(x, y)

				if tile_code == 2 then
					local new_player = Actor.Instantiate("Player")
					local new_player_rb = new_player:GetComponent("Rigidbody")
					new_player_rb.x = tile_pos.x
					new_player_rb.y = tile_pos.y
					local new_player_transform = new_player:GetComponent("Transform")
					if new_player_transform ~= nil then
						new_player_transform.x = tile_pos.x
						new_player_transform.y = tile_pos.y
					end
				
				elseif (tile_code == 1 or tile_code == 3 or tile_code == 4) and not visited[y][x] then
					local width = self:FindRectWidth(tile_code, x, y, visited)
					local height = self:FindRectHeight(tile_code, x, y, width, visited)
					self:MarkRectVisited(visited, x, y, width, height)
					self:SpawnTileRect(tile_code, x, y, width, height, walls_root)
				end
			end
		end
	end,

	FindRectWidth = function(self, tile_code, start_x, y, visited)
		local width = 0
		while start_x + width <= 20 and
			self.stage1[y][start_x + width] == tile_code and
			not visited[y][start_x + width] do
			width = width + 1
		end
		return width
	end,

	FindRectHeight = function(self, tile_code, start_x, y, width, visited)
		local height = 1
		local can_extend = true
		while y + height <= 20 and can_extend do
			for x = start_x, start_x + width - 1 do
				if self.stage1[y + height][x] ~= tile_code or visited[y + height][x] then
					can_extend = false
					break
				end
			end
			if can_extend then
				height = height + 1
			end
		end
		return height
	end,

	MarkRectVisited = function(self, visited, start_x, start_y, width, height)
		for y = start_y, start_y + height - 1 do
			for x = start_x, start_x + width - 1 do
				visited[y][x] = true
			end
		end
	end,

	SpawnTileRect = function(self, tile_code, start_x, start_y, width, height, walls_root)
		local template = "KinematicBox"
		if tile_code == 3 then
			template = "BouncyBox"
		elseif tile_code == 4 then
			template = "VictoryBox"
		end

		local new_box = Actor.Instantiate(template)
		local center_x = start_x + (width - 1) * 0.5
		local center_y = start_y + (height - 1) * 0.5
		local new_box_rb = new_box:GetComponent("Rigidbody")
		new_box_rb.x = center_x
		new_box_rb.y = center_y
		new_box_rb.width = width
		new_box_rb.height = height

		local new_box_transform = new_box:GetComponent("Transform")
		if new_box_transform ~= nil then
			new_box_transform.x = center_x
			new_box_transform.y = center_y
		end

		local new_box_sprite = new_box:GetComponent("SpriteRenderer")
		if new_box_sprite ~= nil then
			new_box_sprite.scale_x = width
			new_box_sprite.scale_y = height
		end

		if tile_code == 1 and walls_root ~= nil then
			new_box:SetParent(walls_root)
		end
	end,

	OnUpdate = function(self)
		
	end
}
