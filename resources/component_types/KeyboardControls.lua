KeyboardControls = {
	speed = 5,
	jump_power = 350,

	OnStart = function(self)
		self.rb = self.actor:GetComponent("Rigidbody")
	end,

	OnUpdate = function(self)
		-- Horizontal
		local horizontal_input = 0
		if Input.GetKey("right") or Input.GetKey("d") then
			horizontal_input = self.speed
		end

		if Input.GetKey("left") or Input.GetKey("a") then
			horizontal_input = -self.speed
		end

		-- Vertical
		local vertical_input = 0

		-- Check on ground
		local on_ground = false
		ground_object = Physics.Raycast(self.rb:GetPosition(), Vector2(0, 1), 1)
		if Input.GetKeyDown("up") or Input.GetKeyDown("space") then
			if ground_object ~= nil then
				vertical_input = -self.jump_power
			end
		end

		self.rb:AddForce(Vector2(horizontal_input, vertical_input))
	end
}

