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

		-- Spawn stage
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
				
				elseif tile_code == 1 then
					local new_box = Actor.Instantiate("KinematicBox")
					local new_box_rb = new_box:GetComponent("Rigidbody")
					new_box_rb.x = tile_pos.x
					new_box_rb.y = tile_pos.y
					local new_box_transform = new_box:GetComponent("Transform")
					if new_box_transform ~= nil then
						new_box_transform.x = tile_pos.x
						new_box_transform.y = tile_pos.y
					end
					if walls_root ~= nil then
						new_box:SetParent(walls_root)
					end

				elseif tile_code == 3 then
					local new_box = Actor.Instantiate("BouncyBox")
					local new_box_rb = new_box:GetComponent("Rigidbody")
					new_box_rb.x = tile_pos.x
					new_box_rb.y = tile_pos.y
					local new_box_transform = new_box:GetComponent("Transform")
					if new_box_transform ~= nil then
						new_box_transform.x = tile_pos.x
						new_box_transform.y = tile_pos.y
					end
				
				elseif tile_code == 4 then
					local new_box = Actor.Instantiate("VictoryBox")
					local new_box_rb = new_box:GetComponent("Rigidbody")
					new_box_rb.x = tile_pos.x
					new_box_rb.y = tile_pos.y
					local new_box_transform = new_box:GetComponent("Transform")
					if new_box_transform ~= nil then
						new_box_transform.x = tile_pos.x
						new_box_transform.y = tile_pos.y
					end
				end
			end
		end
	end,

	OnUpdate = function(self)
		
	end
}
