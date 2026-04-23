AudioMixerLab = {
	scene_title = "Audio Mixer Lab",
	next_scene = "",
	switch_music_clip = "Village Under Siege.ogg",
	font_name = "NotoSans-Regular",
	font_size = 18,
	line_height = 20,

	bgm_a_clip = "GoblinSwordSaint.ogg",
	bgm_b_clip = "Village Under Siege.ogg",
	bgm_c_clip = "main.mp3",
	layer_a_clip = "goblinsaint_swordwave.wav",
	layer_b_clip = "itemPickUp.wav",
	effect_a_clip = "victory.mp3",
	effect_b_clip = "shieldblock.mp3",
	effect_c_clip = "arrow-swish.mp3",

	last_action = "Ready",

	LogAction = function(self, message)
		self.last_action = message
		Debug.Log(self.scene_title .. ": " .. message)
	end,

	PlayOnChannel = function(self, channel, clip_name, volume, does_loop, label)
		Audio.SetVolume(channel, volume)
		Audio.Play(channel, clip_name, does_loop)
		self:LogAction(label .. " -> channel " .. channel .. " [" .. clip_name .. "]")
	end,

	PlayMusic = function(self, clip_name, volume, does_loop, label)
		Music.SetVolume(volume)
		Music.Play(clip_name, does_loop)
		self:LogAction(label .. " -> music stream [" .. clip_name .. "]")
	end,

	DrawLine = function(self, line_index, text, r, g, b)
		local x = 12
		local y = 12 + line_index * self.line_height
		Text.Draw(text, x, y, self.font_name, self.font_size, r, g, b, 255)
	end,

	DrawHud = function(self)
		local current_scene = Scene.GetCurrent()
		self:DrawLine(0, self.scene_title, 20, 20, 20)
		self:DrawLine(1, "Current scene: " .. current_scene, 20, 20, 20)
		self:DrawLine(2, "Last action: " .. self.last_action, 40, 40, 40)
		self:DrawLine(4, "1/2: switch OGG BGM stream   3: test MP3 BGM stream", 0, 0, 0)
		self:DrawLine(5, "4: loop short layer A on ch1   5: loop short layer B on ch2", 0, 0, 0)
		self:DrawLine(6, "6: one-shot SFX   7: halt ch1   8: halt ch2", 0, 0, 0)
		self:DrawLine(7, "9: halt all channels + music", 0, 0, 0)
		self:DrawLine(8, "Tab: load next scene and let next scene replace music", 0, 0, 0)
		self:DrawLine(9, "Q: switch music stream, then queue next scene", 0, 0, 0)
		self:DrawLine(11, "Scene boot now starts music only. Press 4/5 to opt into looped layers.", 60, 60, 60)
		self:DrawLine(12, "BGM uses Music.*; layered loops and SFX stay on Audio channels.", 60, 60, 60)
	end,

	OnStart = function(self)
		self:LogAction("Scene loaded. Scene bootstrap should already have started the default mix.")
	end,

	OnUpdate = function(self)
		if Input.GetKeyDown("1") then
			self:PlayMusic(self.bgm_a_clip, 96, true, "BGM A")
		end

		if Input.GetKeyDown("2") then
			self:PlayMusic(self.bgm_b_clip, 96, true, "BGM B")
		end

		if Input.GetKeyDown("3") then
			self:PlayMusic(self.bgm_c_clip, 96, true, "BGM C")
		end

		if Input.GetKeyDown("4") then
			self:PlayOnChannel(1, self.layer_a_clip, 52, true, "Layer A")
		end

		if Input.GetKeyDown("5") then
			self:PlayOnChannel(2, self.layer_b_clip, 40, true, "Layer B")
		end

		if Input.GetKeyDown("6") then
			self:PlayOnChannel(3, self.effect_c_clip, 128, false, "One-shot SFX")
		end

		if Input.GetKeyDown("7") then
			Audio.Halt(1)
			self:LogAction("Stopped channel 1")
		end

		if Input.GetKeyDown("8") then
			Audio.Halt(2)
			self:LogAction("Stopped channel 2")
		end

		if Input.GetKeyDown("9") then
			Audio.Halt(-1)
			Music.Halt()
			self:LogAction("Stopped all channels and music")
		end

		if Input.GetKeyDown("tab") and self.next_scene ~= nil and self.next_scene ~= "" then
			Scene.Load(self.next_scene)
			self:LogAction("Queued scene load -> " .. self.next_scene)
		end

		if Input.GetKeyDown("q") and self.next_scene ~= nil and self.next_scene ~= "" then
			Music.SetVolume(128)
			Music.Play(self.switch_music_clip, true)
			Scene.Load(self.next_scene)
			self:LogAction("Switched music stream and queued scene load -> " .. self.next_scene)
		end

		self:DrawHud()
	end
}
