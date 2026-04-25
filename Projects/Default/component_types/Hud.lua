Hud = {
	seconds_elapsed = 0,
	finish = false,

	OnStart = function(self)
		Event.Subscribe("event_victory", self, self.OnEventVictory)
	end,

	OnUpdate = function(self)

		if Application.GetFrame() > 0 and Application.GetFrame() % 60 == 0 and self.finish == false then
			self.seconds_elapsed = self.seconds_elapsed + 1
		end

		local text = "Time : " .. self.seconds_elapsed
		local x = 10
		local y = 10

		if self.finish then
			text = "FINISH! Final Time : " .. self.seconds_elapsed .. " seconds!"
		end

		Text.Draw(text, x, y, "NotoSans-Regular", 24, 0, 0, 0, 255)
	end,

	OnEventVictory = function(self)
		self.finish = true
	end
}

