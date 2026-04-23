CameraController = {

	frames_until_hop = 3,

	OnStart = function(self)
		math.randomseed(498)
	end,

	OnUpdate = function(self)
		
		-- every couple frames, jump randomly between positions 0 to 50 
		self.frames_until_hop = self.frames_until_hop - 1
		if self.frames_until_hop <= 0 then
			
			local new_x = math.random(0, 25) * 3
			local new_y = 0
			Camera.SetPosition(new_x, new_y)

			self.frames_until_hop = 3
		end
	end
}