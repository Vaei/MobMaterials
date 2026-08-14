/* Copyright (c) Jared Taylor. All Rights Reserved */

/* The only file that knows which repo this is. Copy assets/docs.css + assets/docs.js
   into another plugin, replace this, and it has a documentation site. */

window.DOCS = {
	title: 'MobMaterials',
	repo: 'https://github.com/Vaei/MobMaterials',
	icon: 'assets/icon.png',
	imgDir: 'img/',
	footer: 'MobMaterials is MIT licensed. &middot; <a href="shots.html">Art checklist</a>',

	sections: [
		{
			name: 'Start',
			pages: [
				{ file: 'index.html', label: 'Home', blurb: 'what this is' },
				{ file: 'install.html', label: 'Install', blurb: 'plugin, project settings, first master' }
			]
		},
		{
			name: 'Build a world',
			pages: [
				{ file: 'workflow.html', label: 'Building a world', blurb: 'the order that avoids backtracking' },
				{ file: 'techniques.html', label: 'Techniques', blurb: 'triplanar, hex tiling, tile break, parallax - when and when not' },
				{ file: 'stylized.html', label: 'Stylized', blurb: 'palette, flat roughness, drawn cavity' },
				{ file: 'realistic.html', label: 'Realistic', blurb: 'scans, roughness variance, parallax' }
			]
		},
		{
			name: 'Reference',
			pages: [
				{ file: 'tutorial.html', label: 'Everything, fast', blurb: 'the whole feature set in order' },
				{ file: 'surface.html', label: 'Surface', blurb: 'props, buildings, rocks' },
				{ file: 'landscape.html', label: 'Landscape', blurb: 'terrain layers and blending' },
				{ file: 'foliage.html', label: 'Foliage', blurb: 'transmission, wind, rustle' },
				{ file: 'weather.html', label: 'Weather', blurb: 'wet, snow, dust, footprints' },
				{ file: 'performance.html', label: 'Cost', blurb: 'taps, samplers, permutations' },
				{ file: 'troubleshooting.html', label: 'If it is wrong', blurb: 'symptom to cause' }
			]
		},
		{
			name: 'Meta',
			pages: [
				{ file: 'changelog.html', label: 'Changelog', blurb: 'versions' },
				{ file: 'shots.html', label: 'Art checklist', blurb: 'every figure, filled or wanted' }
			]
		}
	],

	/* Figure slots. Declared once here, placed on a page by id alone.
	   A file that is not in img/ renders as a one-line placeholder instead of a gap,
	   so a page is the same length before and after the art exists. */
	shots: {
		'index.hero': { page: 'index.html', cap: 'Terrain, props and foliage under one weather value', file: 'index-hero.png' },
		'index.tour': { page: 'index.html', cap: 'Two minutes: recipe, generate, paint, rain', vid: 'index-tour.mp4', poster: 'index-tour-poster.png' },

		'install.mat-menu': { page: 'install.html', cap: 'The Mat menu on the level editor toolbar', file: 'install-mat-menu.png' },
		'install.generate': { page: 'install.html', cap: 'The generate window with a surface recipe selected', file: 'install-generate.png' },
		'install.recipe': { page: 'install.html', cap: 'A surface recipe asset', file: 'install-recipe.png' },
		'install.test-level': { page: 'install.html', cap: 'The test level, one feature per object', file: 'install-test-level.png' },
		'install.preview': { page: 'install.html', cap: 'Preview platform set to Android ES3.1', file: 'install-preview.png' },

		'workflow.texel': { page: 'workflow.html', cap: 'The same wall at 256 and at 1024 px/m', file: 'workflow-texel-low.png', compare: 'workflow-texel-high.png', compareLabels: ['256 px/m', '1024 px/m'] },
		'workflow.layer-plan': { page: 'workflow.html', cap: 'A landscape recipe with its layers named before generating', file: 'workflow-layer-plan.png' },
		'workflow.interlock': { page: 'workflow.html', cap: 'Gravel meeting grass: cross-fade against height interlock', file: 'workflow-interlock-off.png', compare: 'workflow-interlock-on.png', compareLabels: ['Cross-fade', 'Interlock'] },
		'workflow.repetition': { page: 'workflow.html', cap: 'A hillside at 60 m, tiling break off and on', file: 'workflow-repetition-off.png', compare: 'workflow-repetition-on.png', compareLabels: ['Off', 'On'] },
		'workflow.rvt-blend': { page: 'workflow.html', cap: 'A rock cutting a silhouette, then blended into the terrain', file: 'workflow-rvt-off.png', compare: 'workflow-rvt-on.png', compareLabels: ['No blend', 'RVT blend'] },
		'workflow.variation': { page: 'workflow.html', cap: 'One material instance, twelve crates, all different', file: 'workflow-variation.png' },
		'workflow.debug-weights': { page: 'workflow.html', cap: 'Layer weights debug view over the same hillside', file: 'workflow-debug-weights.png' },
		'workflow.scene': { page: 'workflow.html', cap: 'The finished pass', file: 'workflow-scene.png' },

		'techniques.projection': { page: 'techniques.html', cap: 'A kitbashed rock on mesh UVs, then triplanar', file: 'techniques-projection-uv.png', compare: 'techniques-projection-triplanar.png', compareLabels: ['Mesh UV', 'Triplanar'] },
		'techniques.tiers': { page: 'techniques.html', cap: 'The four tiling tiers on one grass layer: none, cheap, dual, hex', file: 'techniques-tiers.png' },
		'techniques.hex-variance': { page: 'techniques.html', cap: 'Hex tiling washed out by its own averaging, then with VarianceRestore and a correct MeanColor', file: 'techniques-variance-off.png', compare: 'techniques-variance-on.png', compareLabels: ['Washed', 'Restored'] },
		'techniques.tilebreak-distance': { page: 'techniques.html', cap: 'Walking backwards across a tiled floor, break off and on', vid: 'techniques-tilebreak.mp4', poster: 'techniques-tilebreak-poster.png' },
		'techniques.macro': { page: 'techniques.html', cap: 'One hillside with macro variation off and on', file: 'techniques-macro-off.png', compare: 'techniques-macro-on.png', compareLabels: ['Off', 'On'] },
		'techniques.blend': { page: 'techniques.html', cap: 'The same transition at BlendHeightAmount 0, 0.45 and 0.85', file: 'techniques-blend.png' },
		'techniques.detail': { page: 'techniques.html', cap: 'A wall at half a metre, detail normal off and on', file: 'techniques-detail-off.png', compare: 'techniques-detail-on.png', compareLabels: ['Off', 'On'] },
		'techniques.rvt': { page: 'techniques.html', cap: 'Rocks meeting the ground with and without the RVT blend function', file: 'techniques-rvt-off.png', compare: 'techniques-rvt-on.png', compareLabels: ['No blend', 'Blended'] },

		'stylized.palette': { page: 'stylized.html', cap: 'Eight colours, and every layer graded to sit inside them', file: 'stylized-palette.png' },
		'stylized.roughness': { page: 'stylized.html', cap: 'Full roughness range against a band clamped to 0.35-0.6', file: 'stylized-rough-wide.png', compare: 'stylized-rough-band.png', compareLabels: ['0-1', '0.35-0.6'] },
		'stylized.cavity': { page: 'stylized.html', cap: 'A cavity bake, and what it does to base colour and specular', file: 'stylized-cavity.png' },
		'stylized.interlock': { page: 'stylized.html', cap: 'A hard, high-contrast interlock reading as a drawn edge', file: 'stylized-interlock.png' },
		'stylized.macro-hue': { page: 'stylized.html', cap: 'Macro variation shifting hue rather than value', file: 'stylized-macro-hue.png' },
		'stylized.foliage': { page: 'stylized.html', cap: 'Saturated SubsurfaceColor against the sun', file: 'stylized-foliage.png' },
		'stylized.scene': { page: 'stylized.html', cap: 'A stylized scene under this setup', file: 'stylized-scene.png' },

		'realistic.repack': { page: 'realistic.html', cap: 'Remap Texture Channels turning an ORM pack into CRM', file: 'realistic-repack.png' },
		'realistic.texel': { page: 'realistic.html', cap: 'A checker on the mesh before any art goes on it', file: 'realistic-texel.png' },
		'realistic.roughness': { page: 'realistic.html', cap: 'Roughness variance is what sells a scan', file: 'realistic-rough-flat.png', compare: 'realistic-rough-varied.png', compareLabels: ['Flattened', 'Full range'] },
		'realistic.interlock': { page: 'realistic.html', cap: 'A subtle interlock at BlendHeightAmount 0.45', file: 'realistic-interlock.png' },
		'realistic.parallax': { page: 'realistic.html', cap: 'Cobbles with offset parallax, then occlusion', file: 'realistic-parallax-offset.png', compare: 'realistic-parallax-occlusion.png', compareLabels: ['Offset', 'Occlusion'] },
		'realistic.moss': { page: 'realistic.html', cap: 'Moss placed by crevice, slope, shade and noise', file: 'realistic-moss.png' },
		'realistic.puddles': { page: 'realistic.html', cap: 'Water gathering by cavity porosity, not a painted mask', file: 'realistic-puddles.png' },
		'realistic.scene': { page: 'realistic.html', cap: 'A realistic scene under this setup', file: 'realistic-scene.png' },

		'tutorial.first-surface': { page: 'tutorial.html', cap: 'Three textures, nothing else on', file: 'tutorial-first-surface.png' },
		'tutorial.second-layer': { page: 'tutorial.html', cap: 'bLayer1 on, painted through vertex red', file: 'tutorial-second-layer.png' },
		'tutorial.tile-break': { page: 'tutorial.html', cap: 'The same floor at range, tiling break off and on', file: 'tutorial-tilebreak-off.png', compare: 'tutorial-tilebreak-on.png', compareLabels: ['Off', 'On'] },
		'tutorial.detail': { page: 'tutorial.html', cap: 'Close up, with and without the detail normal', file: 'tutorial-detail-off.png', compare: 'tutorial-detail-on.png', compareLabels: ['Off', 'On'] },
		'tutorial.presets': { page: 'tutorial.html', cap: 'The four preset instances a surface recipe writes', file: 'tutorial-presets.png' },
		'tutorial.debug': { page: 'tutorial.html', cap: 'Cycling DebugMode through the enum', vid: 'tutorial-debug.mp4', poster: 'tutorial-debug-poster.png' },

		'surface.packs': { page: 'surface.html', cap: 'BC, NRM and CRM side by side with their channels split', file: 'surface-packs.png' },
		'surface.vertex-paint': { page: 'surface.html', cap: 'Black adds. An unpainted mesh is the base layer', file: 'surface-vertex-paint.png' },
		'surface.triplanar': { page: 'surface.html', cap: 'A kitbashed rock, mesh UV against triplanar', file: 'surface-triplanar-uv.png', compare: 'surface-triplanar-on.png', compareLabels: ['Mesh UV', 'Triplanar'] },
		'surface.blend-height': { page: 'surface.html', cap: 'BlendHeightAmount 0 and 1 on one pair of layers', file: 'surface-blend-0.png', compare: 'surface-blend-1.png', compareLabels: ['0', '1'] },
		'surface.parallax': { page: 'surface.html', cap: 'Brick at a grazing angle, parallax off and on', file: 'surface-parallax-off.png', compare: 'surface-parallax-on.png', compareLabels: ['Off', 'On'] },
		'surface.primitive-data': { page: 'surface.html', cap: 'Custom primitive data on an instanced static mesh', file: 'surface-primitive-data.png' },
		'surface.debug-views': { page: 'surface.html', cap: 'The DebugMode enum on the instance', file: 'surface-debug-views.png' },

		'landscape.paint': { page: 'landscape.html', cap: 'Landscape mode with the target layers created', file: 'landscape-paint.png' },
		'landscape.hex': { page: 'landscape.html', cap: 'One grass layer across a valley, hex tiling off and on', file: 'landscape-hex-off.png', compare: 'landscape-hex-on.png', compareLabels: ['Off', 'On'] },
		'landscape.variance': { page: 'landscape.html', cap: 'Hex tiling washed out, then with VarianceRestore', file: 'landscape-variance.png' },
		'landscape.blend': { page: 'landscape.html', cap: 'Two layers meeting along their own height', file: 'landscape-blend.png' },
		'landscape.slope-rock': { page: 'landscape.html', cap: 'Rock appearing where the ground gets steep', file: 'landscape-slope-rock.png' },
		'landscape.moss': { page: 'landscape.html', cap: 'Moss in crevices and off the sun', file: 'landscape-moss.png' },
		'landscape.uv-scale': { page: 'landscape.html', cap: 'Fit UV Scale To Landscape', file: 'landscape-uv-scale.png' },
		'landscape.arrays': { page: 'landscape.html', cap: 'Pack Layers, and what the arrays look like afterwards', file: 'landscape-arrays.png' },
		'landscape.simplify': { page: 'landscape.html', cap: 'Simplify Material To Layer, with its restore record', file: 'landscape-simplify.png' },
		'landscape.rvt': { page: 'landscape.html', cap: 'Meshes reading the terrain RVT and fading into it', file: 'landscape-rvt.png' },

		'foliage.transmission': { page: 'foliage.html', cap: 'A leaf against the sun, transmission off and on', file: 'foliage-transmission-off.png', compare: 'foliage-transmission-on.png', compareLabels: ['Off', 'On'] },
		'foliage.mask': { page: 'foliage.html', cap: 'OpacityMask reads alpha, not a colour channel', file: 'foliage-mask.png' },
		'foliage.wind': { page: 'foliage.html', cap: 'Two scales of wind: the base plants, the tips move', vid: 'foliage-wind.mp4', poster: 'foliage-wind-poster.png' },
		'foliage.rustle': { page: 'foliage.html', cap: 'Grass pushed aside by something running through it', vid: 'foliage-rustle.mp4', poster: 'foliage-rustle-poster.png' },
		'foliage.layers': { page: 'foliage.html', cap: 'Bark against leaf on one mesh, per-layer TransmissionScale', file: 'foliage-layers.png' },

		'weather.wetness': { page: 'weather.html', cap: 'A courtyard dry, then at Wetness 1', file: 'weather-dry.png', compare: 'weather-wet.png', compareLabels: ['Dry', 'Wet'] },
		'weather.puddles': { page: 'weather.html', cap: 'Water standing in the low points first', file: 'weather-puddles.png' },
		'weather.ripples': { page: 'weather.html', cap: 'Rain ripples on standing water', vid: 'weather-ripples.mp4', poster: 'weather-ripples-poster.png' },
		'weather.snow': { page: 'weather.html', cap: 'Snow 0 to 1 across terrain, props and roofs', file: 'weather-snow-0.png', compare: 'weather-snow-1.png', compareLabels: ['0', '1'] },
		'weather.trample': { page: 'weather.html', cap: 'Footprints clearing snow back to the ground', vid: 'weather-trample.mp4', poster: 'weather-trample-poster.png' },
		'weather.dust': { page: 'weather.html', cap: 'The same system as ash, and as dust', file: 'weather-dust.png' },
		'weather.mpc': { page: 'weather.html', cap: 'MPC_MobWeather, which is the whole interface', file: 'weather-mpc.png' },

		'performance.stats': { page: 'performance.html', cap: 'Material stats with the preview platform on ES3.1', file: 'performance-stats.png' },
		'performance.report': { page: 'performance.html', cap: 'Report Cost in the Output Log', file: 'performance-report.png' },
		'performance.verify': { page: 'performance.html', cap: 'Verify Contract passing', file: 'performance-verify.png' },

		'troubleshooting.rvt-black': { page: 'troubleshooting.html', cap: 'Mirror-black terrain: bUseRVT on with nothing writing the texture', file: 'troubleshooting-rvt-black.png' },
		'troubleshooting.vertex-white': { page: 'troubleshooting.html', cap: 'An unpainted mesh arriving as the top layer, soaking wet', file: 'troubleshooting-vertex-white.png' },
		'troubleshooting.mush': { page: 'troubleshooting.html', cap: 'Layers cross-fading into mush at BlendHeightAmount 0', file: 'troubleshooting-mush.png' }
	}
};
