# Everything, Fast

Getting from a working master material to the full feature set.

This picks up where the [README](./README.md) leaves off: the plugin is enabled, you have a recipe, and you have generated a master and put an instance on a mesh.

| | |
|---|---|
| [Read this first](#read-this-first) | three facts that explain every decision here |
| [The fast path](#the-fast-path) | the whole thing, in order, ~20 minutes |
| [1. Decide the recipe](#1-decide-the-recipe) | what goes in the master, once |
| [Art you have to supply](#art-you-have-to-supply) | four placeholders that read as broken features |
| [2. One good surface](#2-one-good-surface) | layers, blending, projection |
| [3. Make it hold up](#3-make-it-hold-up) | tiling break, detail, parallax |
| [4. Make it vary](#4-make-it-vary) | vertex paint, colour variation, primitive data |
| [5. Weather](#5-weather) | wetness, ripples, snow |
| [6. Foliage](#6-foliage) | a second master |
| [7. Terrain](#7-terrain) | layers, slope rock, moss, arrays |
| [8. Tie it together](#8-tie-it-together) | RVT, footsteps, grass |
| [Working with debug views](#working-with-debug-views) | the part that saves the most time |
| [Keeping the cost honest](#keeping-the-cost-honest) | before you ship |
| [Wrong turns](#wrong-turns) | what people hit |

---

## Read this first

**A feature you do not turn on does not exist.** Gating is done with static bools on material function inputs, and an input no live branch reads is never compiled - so a disabled layer takes its texture samples with it.

**Recipe options and instance switches are different things.** The recipe decides what is *in* the master; the switch decides whether an *instance* uses it.

**Presets exist to keep the permutation count down.** Every distinct combination of static switches is another shader map, multiplied by every mesh type that wears it. Parent to the preset closest to what you want rather than turning switches on from the base master, and reuse presets across assets.

> [!TIP]
> Regenerating is idempotent - assets are emptied and rebuilt in place, so instances keep their references and their values. Changing your mind about a recipe option is always safe. It is just slow.

## The fast path

If you only read one section, read this one. Each step is expanded below.

1. **Recipe** - turn on every option you might want. Detail, tiling break, primitive data, accumulation, ripples, debug views. Leave parallax off unless you know you want it. Generate.
2. **One surface** - child of `MI_<Name>_Prop`, set `Layer0_BC` / `_NRM` / `_CRM`. Look at it.
3. **Second layer** - `bLayer1` on, set its three textures, `BlendHeightAmount` to 0.5. Paint vertex red.
4. **Tiling break** - `Layer0_TileBreak` on. Walk backwards. The grid should dissolve.
5. **Detail** - `bDetail` on, a fine normal, `DetailScale` around 8.
6. **Variation** - `bColorVariation` on for a set of props, or primitive data for per-instance tint.
7. **Weather** - `bWetness` on, drag `Wetness` on the collection 0 to 1. Then `bRipples`, then `bAccumulation`.
8. **Foliage** - second recipe, same output path, `bFoliage` on, different asset name. Generate.
9. **Terrain** - landscape recipe, layers named, generate, assign, paint.
10. **Verify and Report** from the Mob menu. Read the numbers.

## 1. Decide the recipe

The one step worth slowing down for, because changing it later means regenerating.

| Recipe option | Turn it on if | Cost when an instance says no |
|---|---|---|
| **Detail Maps** | anything is seen closer than a couple of metres | nothing |
| **Distance Tiling Break** | anything tiles across a large surface | nothing |
| **Primitive Data** | you want per-instance tint without new material instances | nothing |
| **Debug Views** | always | nothing |
| **Accumulation** | there is ever snow, dust or ash | nothing |
| **Rain Ripples** | it ever rains | nothing |
| **Parallax** | one hero surface needs fake depth | nothing, but see below |
| **Foliage** | this recipe is *for* foliage | it is a different master |

Everything in that list is free until an instance asks, so the honest default is to turn them all on except parallax and foliage.

Parallax is the exception worth thinking about: the cheap offset mode is fine, but occlusion mode is the only thing in either master that spends a sampler slot, because a raymarch cannot be expressed as graph taps and its Custom node brings its own sampler. Turn the option on if you want it available; just do not put occlusion on more than a hero surface.

Foliage is not an option so much as a fork - shading model, two-sidedness and blend mode are material properties rather than parameters, so a foliage master cannot be a switch on a standard one. Point a second recipe at the same output path with a different asset name.

## Art you have to supply

Every texture parameter defaults to a 4x4 neutral from the plugin, so nothing is ever unassigned and no base material drags art into memory. Four of those neutrals are placeholders rather than defaults you would ship - the feature compiles and costs what it costs, but a flat texture makes it look like nothing is happening.

| Parameter | Default | What it wants | Symptom if left |
|---|---|---|---|
| `MacroNoiseTexture` | `T_BaseLinear` | a large, soft greyscale noise, tiled very low | macro variation does nothing - the noise is a constant |
| `RippleNormal` | `T_BaseNormal` | any tiling normal; a real ripple texture beats generic noise | ripples cost their two samples and the puddle stays glassy |
| `DetailNormal` | `T_BaseNormal` | a fine, high-frequency normal | detail costs its sample and adds no detail |
| `EmissiveMask` | `T_BaseBlack` | white where it should glow | emissive is on and nothing glows - black masks everything |

> [!IMPORTANT]
> These are the ones to check first when a feature "does not work". The switch is on, the cost is real, and the input is flat - which reads exactly like a broken feature.

The plugin ships no noise texture of its own on purpose: a noise map is art, and one that suits a stylised project ruins a realistic one. Any tiling greyscale noise works, and the engine already carries two worth starting from:

| | |
|---|---|
| `/Engine/EngineMaterials/T_Default_MacroVariation` | low frequency. What `MacroNoiseTexture` wants |
| `/Engine/EngineMaterials/Good64x64TilingNoiseHighFreq` | high frequency. Usable as a stand-in for `DetailNormal` and `RippleNormal` until there is real art, though both would rather have a normal map than a greyscale |

## 2. One good surface

Right-click `MI_<Name>_Prop` → **Create Material Instance**. That preset has everything off, three texture samples, and is the cheapest thing the master can be.

Set `Layer0_BC`, `Layer0_NRM`, `Layer0_CRM`. Nothing else. Look at it on a mesh before adding anything.

**CRM is Cavity, Roughness, Metallic** - not AO. The game already has ambient occlusion; a texture AO on top double-darkens. Cavity does the job AO was doing and also serves as the height source for blending, so no separate height texture is needed.

Then the second layer:

1. `bLayer1` on.
2. `Layer1_BC` / `_NRM` / `_CRM`.
3. `BlendHeightAmount` to 0.5, `BlendContrast` around 0.5.

`BlendHeightAmount` at 0 is a plain cross-fade and at 1 is a pure height interlock, where the layers meet along their own detail - mortar filling before brick, gravel settling between cobbles. Somewhere in the middle usually reads best. `BlendContrast` is how wide a band that happens over; low is a hard crunchy edge.

For projection, `LayerN_Triplanar` per layer. Off is one mesh-UV tap. On is world-aligned, three taps per texture, needs no UVs and survives non-uniform scale - the right answer for rocks, cliffs and anything kitbashed. Set `TriplanarScale` in world units per UV, `TriplanarSharpness` for how narrowly the three cross-fade.

> [!TIP]
> Triplanar and tiling break do not stack. The break is mesh-UV only on purpose: triplanar already samples three ways, and breaking it too would be nine taps to solve a repeat the projection has largely hidden.

## 3. Make it hold up

Three features that do nothing in a screenshot and everything in motion.

**Tiling break** - `LayerN_TileBreak`. A second tiling of the same texture at an incommensurate scale and rotation, faded in with distance. Near keeps the detail it was authored with; far dissolves the grid.

Set `TileBreakScale` to something irrational-ish - 0.37, not 0.5. At 0.5 the two repeats line up and you have made a bigger grid. `TileBreakStart` and `TileBreakFalloff` say where it comes in.

The two tilings combine as an overlay rather than a lerp, because a lerp of two scales halves the contrast and reads as blur - trading one artefact for another.

**Detail** - `bDetail`, then point `DetailNormal` at a fine tiling normal, `DetailScale` around 8, `DetailAmount` to taste. It defaults to a flat normal, so without that it costs a sample and shows nothing. One extra sample for the whole material, faded out with distance. This is most of what separates a base layer from a finished one up close.

**Parallax** - `LayerN_Parallax` for the cheap offset mode. `ParallaxAmount` small: 0.02 to 0.06. Past that the surface swims. Sells brick, cobbles and plank gaps at anything but a grazing angle. `LayerN_ParallaxOcclusion` raymarches instead, which is genuinely expensive and brings its own sampler.

Neither mode changes the silhouette. At a grazing angle the edge is still flat - that is the honest limit of the trick, and why offset mode is usually enough.

## 4. Make it vary

Three ways to stop a hundred copies of a prop reading as a hundred copies of a prop, in ascending order of cost.

**Custom primitive data** is the cheapest per-instance variation there is: no new material instance, no new shader permutation, and it can be driven from Blueprint at runtime. Indices are a fixed contract - 0,1,2 tint, 3 roughness offset, 4 wetness offset - so whatever sets them can rely on them.

**Colour variation** - `bColorVariation`. Hashes object position into an HSV shift, so each instance of the same mesh lands somewhere slightly different. `VariationHue` / `VariationSaturation` / `VariationValue` are how far it can drift. Keep hue tiny; value does most of the work.

**Vertex paint** is per-mesh rather than per-instance, and it is worth knowing the convention before you pick up the brush:

| Channel | White | Black |
|---|---|---|
| R | layer 0 | layer 1 |
| G | layer 0 | layer 2 |
| B | dry | wet |
| A | no darkening | darkened |

**Black adds, white is neutral.** You do not have to fill a mesh with black before painting - a mesh with no vertex colour buffer gets white from the vertex factory, and white is the unpainted base layer, dry and unshaded. The cost is that the brush is subtractive: you paint black to add a layer. The benefit is that any mesh reaching this material unpainted looks correct instead of arriving as the top layer, soaking wet.

## 5. Weather

The short version: `bWetness` on, then **Mob → Open MPC_MobWeather** and drag `Wetness` 0 to 1.

Wetness is a state the world is in rather than a per-material look, which is why it lives in a parameter collection. Point several recipes at one collection and a whole project follows it. Per instance, `Wetness_LocalAmount` scales it, and **0 opts out entirely** - use it for interiors and anything sheltered.

Start with only `RoughnessTarget` and `Darkening`. Wet is mostly a roughness change. If it does not read at `Wetness` 1 with those two alone, the problem is the lighting.

Then `bRipples` for rain - set `RippleNormal` too, it defaults to flat - and `bAccumulation` for snow, dust or ash, which needs no art at all.

> [!NOTE]
> Weather has a tutorial of its own with the numbers for each: [`MOBMASTER_WEATHER.md`](./MOBMASTER_WEATHER.md).

## 6. Foliage

A second recipe pointed at the same output path, a different asset name, **Foliage** on. Generate. You get `M_MyFoliage` beside `M_MySurface`, sharing one copy of the material functions.

It comes out masked, two-sided, two-sided-foliage shading, with a subsurface colour for light coming through a leaf.

For wind, `bWind`, then two scales of the same motion: `WindStrength` / `WindSpeed` is the trunk sway carrying the whole plant, and `WindFlutterStrength` / `WindFlutterSpeed` is the leaf flutter across the sway direction so the leaves do not all travel along one line. Movement is weighted by height above the object origin, so the base stays planted with no skeleton and no painted weight. Phase comes from world position, so two plants side by side never move together.

Vertex colour red scales wind where painted; unpainted is white, which is already the tips-move-most case.

> [!NOTE]
> Wind is world position offset, so it runs per vertex. Foliage meshes want to be low enough poly that this is free.

## 7. Terrain

A landscape recipe's **Layers** list is the whole design: the first is the base, and it holds whatever weight the painted layers leave behind, so untouched ground has something to blend against.

1. Name the layers. Generate.
2. Assign `MI_<Name>` to the landscape.
3. Landscape mode, **+** beside each target layer to create its layer info asset.
4. Point each layer's `_BC` / `_NRM` / `_HRC` at your textures.
5. Paint.

Layer count is built into the graph, so it cannot be changed on an instance. Edit the recipe and generate again.

**HRC is Height, Roughness, Cavity** - the landscape's pack, same reasoning as CRM. Height leads because the blend is height-aware.

Then the terrain-specific features:

| | |
|---|---|
| `<Layer>_HexTiling` | stochastic hex tiling - three rotated samples per texture, weighted per cell, with variance restored so the blend does not wash the colour out. The strongest repeat killer here, and the most expensive |
| `<Layer>_DualScale` | the cheap tier. One extra tiling overlaid. On by default |
| `SlopeRock_*` | rock taking over past a slope angle, height-interlocked rather than cross-faded, broken up by noise |
| `Moss_*` | moss placed by cavity, slope, sun-facing shade and noise rather than painted |
| `<Layer>_SlopeMin/Max/Amount` | stop a layer from painting on cliffs |
| `<Layer>_AltitudeMin/Max/Amount` | stop a layer below or above a height |

### Texture arrays

Past about four layers, consider **Texture Array Layers** on the recipe. Each layer's three textures become three slices, so the master samples three texture objects no matter how many layers there are - eight layers is three texture objects instead of twenty-four, and one slice index per layer instead of three texture parameters.

1. Tick **Texture Array Layers** and set **Layer Texture Root** to the folder holding the art.
2. **Mob → Pack Layers for \<Recipe\>**. It matches each layer by name against a channel suffix - `_BC`, `_BaseColor`, `_basecolor` for colour, `_NRM`, `_Normal` for normals, `_HRC`, `_HeigRougAO` for the mask pack - searching recursively, so per-layer subfolders are fine.
3. Read the log. It lists the slice order and names any layer it could not find a texture for.
4. Generate.

The cost is that a slice cannot differ from its neighbours: every layer's textures must share one resolution and one format per channel. That is what a texture array is, not a limitation of this plugin. Swapping one layer's art means a repack rather than a parameter change.

> [!IMPORTANT]
> Pack before you generate. The master samples whatever is at each slice index and has no way to notice that a slice is not the art the layer wanted - a mismatch looks like the wrong texture on the wrong layer, not like an error. Moss takes the last slice, after the painted layers.

## 8. Tie it together

**Build Project Outputs** on a landscape recipe adds the three things that connect terrain to the rest of the game. They reference assets a game owns rather than the plugin, which is why they are off by default - set the four paths beneath the tickbox before generating.

| | |
|---|---|
| **Runtime virtual texture** | caches the blend so the terrain shades once per texel instead of per pixel, and gives meshes something to read |
| **Physical material output** | footsteps follow the paint, and the wet mask feeds it too, so wet dirt reads as mud without anything wiring it up |
| **Grass output** | grass placed by paint weight |

Generating also authors `MF_<Name>RVTBlend`, which fades a mesh into the terrain by reading the height RVT - the thing that stops a rock or a wall meeting the ground on a hard line. Put it on the surface material of anything that sits in the terrain.

## Working with debug views

The single biggest time saver here, and it costs nothing until asked.

`bDebug` on the instance, then `DebugMode`:

| | |
|---|---|
| 1 | layer weights - red is layer 0, green layer 1, blue layer 2 |
| 2 | cavity |
| 3 | blended normal |
| 4 | wetness mask |
| 5 | blended height |
| 6 | vertex colour, as painted |

The result goes to emissive with base colour blacked out, so you see the value itself rather than the value times whatever the light was doing.

**Layer weights is the one to reach for.** A wrong weight is invisible in the final image precisely when it matters, because it looks like a texture choice rather than a mistake - and no other view can reconstruct it.

Mode 4 is the one to check when weather is not behaving: ripples are gated by the puddle mask, so if there is no puddle there is nothing to ripple.

The landscape master has the same views for weights, cavity, wetness and height.

## Keeping the cost honest

Two things on the Mob menu, both of which answer questions that otherwise surface at cook time.

**Verify Contract** builds a scratch instance per feature and asserts what the documentation claims - taps and samplers per feature, that the ambient occlusion pin is left alone, that the custom primitive data indices have not moved. Run it after changing a recipe.

**Report Cost** counts distinct shader permutations across every master's instances, and estimates texture held resident per master. Permutation count is the one to watch: each is another shader map, multiplied by every vertex factory that wears it. If it is climbing, assets are flipping their own switches instead of parenting to presets.

> [!WARNING]
> Do not trust the Material Editor's "Texture samplers" stat. It over-reports - it counts the sample function overload declarations and the engine's own lookups alongside yours. Dump the shader (`r.DumpShaderDebugInfo 1`) and count `Texture2DSample(Material_` if you need the real number.

## Wrong turns

| | |
|---|---|
| **Turned a switch on and nothing happened** | the recipe option is off, so the feature is not in the master. Turn it on, regenerate |
| **Everything is soaking wet and the top layer** | vertex colour convention. Black adds, white is neutral - an unpainted mesh should read as the base layer. If it does not, something has filled the mesh with black |
| **A feature is on and nothing changed** | its texture is still the flat placeholder. See [Art you have to supply](#art-you-have-to-supply) |
| **Ripples do nothing** | they are gated by the puddle mask. Check debug view 4 first: no puddle, no ripple. Raise `PuddleDepth` or check the surface is facing up |
| **Wetness reads as a darker texture** | `RoughnessTarget` is not low enough. Wet is mostly a roughness change |
| **Tiling break made it blurry** | `TileBreakScale` too close to a simple fraction, or `TileBreakAmount` too high. Try 0.37 |
| **Parallax swims** | `ParallaxAmount` too high. 0.02 to 0.06 |
| **The editor froze while generating** | it is working. A landscape master rebuilds several hundred nodes and recompiles on the game thread, which can take minutes |
| **Wrong texture on a terrain layer after packing arrays** | slice order is the recipe's layer order with moss last. Repack and read the log's slice list |
| **Permutation count is climbing** | assets are setting their own switches. Parent them to presets instead |

---

| Reference | |
|---|---|
| [`MOBMASTER_SURFACE.md`](./MOBMASTER_SURFACE.md) | every surface parameter |
| [`MOBMASTER_LANDSCAPE.md`](./MOBMASTER_LANDSCAPE.md) | every landscape parameter |
| [`MOBMASTER_WEATHER.md`](./MOBMASTER_WEATHER.md) | rain, snow, dust |
