VictoryBox = {

	OnTriggerEnter = function(self, collision)
		if collision.other:GetName() == "player" then
			Event.Publish("event_victory")
		end
	end
}

