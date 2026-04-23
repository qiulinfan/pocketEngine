AudioSceneBootstrap = {
	stop_all_channels = true,
	stop_music = true,
	log_label = "AudioSceneBootstrap",

	music_clip = "",
	music_loop = true,
	music_volume = 96,

	layer_a_clip = "",
	layer_a_channel = 1,
	layer_a_loop = true,
	layer_a_volume = 64,

	layer_b_clip = "",
	layer_b_channel = 2,
	layer_b_loop = true,
	layer_b_volume = 48,

	PlayConfiguredClip = function(self, clip_name, channel, does_loop, volume)
		if clip_name == nil or clip_name == "" then
			return
		end

		Audio.SetVolume(channel, volume)
		Audio.Play(channel, clip_name, does_loop)
	end,

	PlayConfiguredMusic = function(self)
		if self.music_clip == nil or self.music_clip == "" then
			return
		end

		Music.SetVolume(self.music_volume)
		Music.Play(self.music_clip, self.music_loop)
	end,

	OnStart = function(self)
		if self.stop_all_channels then
			Audio.Halt(-1)
		end

		if self.stop_music then
			Music.Halt()
		end

		self:PlayConfiguredMusic()
		self:PlayConfiguredClip(self.layer_a_clip, self.layer_a_channel, self.layer_a_loop, self.layer_a_volume)
		self:PlayConfiguredClip(self.layer_b_clip, self.layer_b_channel, self.layer_b_loop, self.layer_b_volume)

		Debug.Log(self.log_label .. ": bootstrapped scene audio for [" .. Scene.GetCurrent() .. "]")
	end
}
