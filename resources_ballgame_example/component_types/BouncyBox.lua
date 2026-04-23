BouncyBox = {

	OnCollisionEnter = function(self, collision)
		if collision.other:GetName() == "player" then
			local rb = collision.other:GetComponent("Rigidbody")
			local current_vel = rb:GetVelocity()
			local new_vel = Vector2(current_vel.x, -15)
			rb:SetVelocity(new_vel)
		end
	end
}

