namespace VspEngine.Rendering
{
	/// <summary>
	/// The settings a frame is rendered with, as a game reaches them. They describe
	/// the FRAME rather than a draw: what it clears to and which sky is painted
	/// behind everything - which is why they live on the engine's frame driver and
	/// not on any pipeline.
	///
	/// The engine paints the sky of every frame itself (see <see cref="Skybox"/>);
	/// a pipeline records its own passes into the frame it is given and never has to
	/// know the sky exists.
	/// </summary>
	public static class RenderSettings
	{
		private static Material? skyboxMaterial;

		/// <summary>
		/// The material every frame's sky is drawn with, or null for the engine's own
		/// default sky (see <see cref="SkyboxRenderer.DefaultMaterial"/>).
		///
		/// This is the whole of the skybox's configuration: the material names the
		/// shader that draws the sky and carries the values it is drawn with, so
		/// replacing the material replaces the sky - a differently TINTED sky is a
		/// different material of the engine's shader, and a completely different sky
		/// is a material of a shader that declares the same properties (see
		/// <see cref="SkyboxRenderer.PropertyNames"/>).
		/// </summary>
		public static Material? Skybox
		{
			get => skyboxMaterial;
			set => skyboxMaterial = value;
		}

		/// <summary>
		/// Whether the engine paints a sky at all. True by default; a game that wants
		/// to see its own clear colour (an editor viewport, a loading screen) turns it
		/// off for a frame.
		/// </summary>
		public static bool SkyboxEnabled { get; set; } = true;

		/// <summary>
		/// The colour the frame is cleared to before anything is drawn. The sky -
		/// when there is one - covers it, so this is what shows through where the
		/// sky does not reach.
		/// </summary>
		public static Color BackgroundColor { get; set; } = new Color(0.06f, 0.06f, 0.10f, 1.0f);

		/// <summary>The material the next frame will actually be drawn with: the game's, or the engine's default.</summary>
		public static Material? ResolvedSkybox =>
			SkyboxEnabled ? (skyboxMaterial ?? SkyboxRenderer.GetOrCreateDefaultMaterial()) : null;

		/// <summary>
		/// Creates a material of the engine's sky shader: the starting point of a sky
		/// of a game's own. Tint it, then put it in <see cref="Skybox"/>.
		///
		/// A game that wants a completely different sky writes its own shader instead
		/// and hands the engine a material of that: the engine reads the same
		/// properties by name (see <see cref="SkyboxRenderer.PropertyNames"/>) and
		/// fills the same push-constant block, so any shader that declares them draws
		/// the engine's sky.
		/// </summary>
		public static Material? CreateSkyboxMaterial() => SkyboxRenderer.CreateMaterialFromDefaultShader();
	}
}
