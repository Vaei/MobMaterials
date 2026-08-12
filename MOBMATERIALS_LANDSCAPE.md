# Landscape Reference

Setup and troubleshooting are in [`README.md`](README.md). The surface master is in [`MOBMATERIALS_SURFACE.md`](MOBMATERIALS_SURFACE.md).

| System | |
|---|---|
| [Texture packs](#texture-packs) | two textures per layer |
| [Working on one layer](#working-on-one-layer) | turn everything else off, and put it back |
| [Tile size](#tile-size) | UVScale is per quad, not per metre |
| [Paint layers](#paint-layers) | how many, and what the base layer is for |
| [Blending](#blending) | height interlock rather than cross-fade |
| [Tiling break](#tiling-break) | four tiers, from free to stochastic |
| [Layer masks](#layer-masks) | gating a layer by slope and altitude |
| [Slope rock](#slope-rock) | cliffs without painting them |
| [Moss](#moss) | cavity, shade and slope decide where it grows |
| [Wetness](#wetness) | damp, then standing water |
| [Accumulation](#accumulation) | snow, dust or ash settling by facing |
| [Trample](#trample) | what has been walked through |
| [Global grade](#global-grade) | one place to shift the whole terrain |
| [Debug views](#debug-views) | seeing the blend instead of guessing at it |
| [Runtime virtual texture](#runtime-virtual-texture) | caching the blend, and blending meshes into it |
| [Project integration](#project-integration) | RVT, footstep surfaces, grass |

---

## Texture packs

Two textures per layer, not three, because a landscape can carry a dozen layers and each one costs memory whether it is painted or not.

| Suffix | Content | Compression |
|---|---|---|
| `_BC` | albedo | Default, sRGB |
| `_NRM` | tangent normal | Normalmap |
| `_HRC` | **H**eight, **R**oughness, **C**avity | Masks, sRGB off |

Height drives the blend, so the interlock costs no extra texture.

There is no ambient occlusion channel, on purpose. The renderer supplies its own occlusion and a baked AO map on top of it darkens twice, so the third channel carries cavity instead: it multiplies base colour and specular through the global grade and never reaches the AO pin, which stays at 1.

Height and cavity are not the same signal. Height is macro - which layer wins where the two interlock. Cavity is micro - the crevice between two stones, which shades but never decides a blend.

Art rarely arrives packed this way. **Mob → Remap Texture Channels...** repacks it: pick how the incoming maps are laid out - packed ORM, MRAO, RMA, or a texture each - and it fills the three output slots, which stay editable for a layout it does not know. The result is written linear as Masks and overwrites an existing asset of the same name in place, so a repack keeps every material already pointing at it. Note that no preset ever wires ambient occlusion into anything, for the reason above; point it at a slot yourself if that is genuinely what you want.

## Working on one layer

The master is built to have everything on at once, which is the wrong thing to be looking at while a single layer's art is being authored. Slope rock, moss, wetness, the tiling break and the other layers all move together, and a mistake in any of them looks like a mistake in the texture.

**Mob → Simplify Material To Layer...** turns it down to one. It sets the chosen layer's weight to full and every other layer's to nothing, zeroes the slope and altitude masks, and optionally turns off the tiling break, zeroes slope rock, moss and wetness, and neutralises the per-layer grade. **Show Everywhere** additionally points every other layer's textures and tiling at the chosen one's, so unpainted ground shows it too - which is what you want when only one layer's art exists and the base layer is still a placeholder.

Everything it touches is recorded first, keyed by the instance's path, and **Restore** in the same window puts back exactly what was there rather than the master's defaults. A layer that had already been tuned does not lose that tuning to a debugging aid. The record is per developer and is not checked in.

Turning the tiling break off and on again is a static switch, so both directions recompile that instance's permutations.

## Tile size

`<Layer>_UVScale` is **tiles per landscape quad**, not per metre. A quad is one unit of the landscape actor's own scale, so what a tile measures on the ground depends on that scale, and a landscape that has been resampled or resized does not have the 100 unit quads the defaults assume. Every layer then tiles at the wrong size at once, which reads as a blurred or moire mess rather than as a wrong number - the texture is minified past its top mips and no detail survives at any distance.

**Mob → Fit UV Scale To Landscape...** does the arithmetic. It takes the landscape selected in the level (or the only one in it) and the material instance selected in the Content Browser (or whatever the landscape is rendering with), asks how many metres a tile should span, and writes every layer. Layers wanting a tighter repeat than the rest go in **Per Layer Tile Size**.

The sum, if you would rather do it by hand: `UVScale = QuadCm / (TileMetres * 100)`. A landscape scaled 15.87 with a 4 m tile wants `0.0397`.

## Paint layers

Edit **Layers** on the recipe. Each entry is a name, a physical surface and a physical material; the last two are only consumed by the project integration phases, never by the graph.

The **first layer is the base**: it holds whatever weight the painted layers leave behind. Without that, untouched ground sits at a painted weight of zero and the first brush stroke blends against nothing, landing as a hard edge.

Layers fold together through a chain of `MF_MobHeightBlendPair` rather than a `LandscapeLayerBlend` node, because a blend node exposes no weight to mask and the layers here have to be gated by slope and altitude.

## Texture arrays

**Texture Array Layers** on the recipe samples every layer out of three arrays - one per channel, one slice per layer - instead of three textures each.

Texture count stops growing with layer count. The master carries a slice index per layer rather than three texture parameters, so the parameter list stays readable at eight layers and the material stops being the reason not to add a ninth.

| Five layers plus moss | Texture parameters | Samplers | Pixel taps | PS instructions |
|---|---|---|---|---|
| Loose textures | 19 | 5 | 37 | 1779 |
| Texture arrays | **4** | 5 | 37 | 1794 |

The shader is not the point: samplers and taps are identical, and the slice appends cost about fifteen instructions. What changes is that eighteen texture objects become three, which is what makes a large layer set practical at all - and array slices share the wrap sampler exactly like loose textures do, so the sampler budget is no more in play than before.

Set **Layer Texture Root** and run **Mob → Pack Layers for \<Recipe\>**. Each layer is matched by name against a channel suffix - `_BC`, `_BaseColor`, `_basecolor` for colour, `_NRM`, `_Normal` for normals, `_HRC`, `_HeigRougAO` for the mask pack - searched recursively, so per-layer subfolders are fine. The log lists the slice order and names any layer it could not resolve.

Slices are the recipe's layer order, **with moss last**. Moss is an overlay the master adds itself rather than a layer anyone asked for, so if no moss texture is found it borrows the first layer's and says so, instead of failing the pack.

> [!IMPORTANT]
> Every layer's textures must share one resolution and one format per channel. That is what a texture array is, not a limitation here - and it is why the packer refuses rather than substituting. Swapping one layer's art means a repack, not a parameter change.

> [!CAUTION]
> Pack before generating, and repack after changing the layer list. The master samples whatever is at each slice index and cannot tell that a slice is not the art the layer wanted, so a stale array reads as the wrong texture on the wrong layer rather than as an error.

## Blending

Shared by every transition, so the whole terrain reads the same:

| | |
|---|---|
| `BlendHeightContrast` | how wide a band the interlock happens over |
| `BlendHeightAmount` | 0 is a plain cross-fade, 1 is a pure height interlock |

> [!NOTE]
> Contrast only widens the band the interlock happens over; it never softens the interlock itself, so a low value always reads as a hard crunchy edge however soft the brush was. Fading back toward the paint weight is what actually gives a gradient, which is what `BlendHeightAmount` does.

## Tiling break

Per layer, chosen by two static bools, so an unused tier compiles out entirely.

| Tier | Taps | |
|---|---|---|
| **None** | 1 | straight sample |
| **Cheap** | 1 | modulated by the macro noise the master samples once for all layers |
| **Dual** | 2 | a second scale overlaid, breaking the macro repeat without halving contrast |
| **Hex** | 3 | stochastic hex-tile sampling. No repeat survives it |

Dual combines as an overlay rather than a lerp: a lerp of two scales just halves the contrast and reads as blur, where overlay keeps the detail and only disturbs the low frequency that made the repeat visible.

Hex blends three taps, which averages away the texture's variance and leaves it washed out, so `VarianceRestore` pushes the result back out from the texture's mean. `MeanColor` is a per-layer parameter because it is a property of the source art, not something the shader can know.

Every tier takes explicit gradients. The layer samplers are Shared:Wrap, so an implicit sample inside a Custom node would take its derivatives from the wrong place once UVs are rotated or offset per cell, and mip selection would fight the coordinates across a cell seam.

## Layer masks

Per layer, through `MF_MobLayerWeight`:

| | |
|---|---|
| `SlopeMin`, `SlopeMax`, `SlopeAmount` | fade a layer out as the ground steepens |
| `AltitudeMin`, `AltitudeMax`, `AltitudeFeather`, `AltitudeAmount` | band a layer over world Z, for a waterline or a snowline |

Both compare against cosines and smoothsteps rather than calling `acos`, so there is no trigonometry in the inner loop.

## Slope rock

`MF_MobSlopeRock` overlays the Rock layer wherever the ground is steeper than `SlopeStart`, reaching full by `SlopeEnd`, so cliffs do not have to be painted. `HeightContrast` and `HeightAmount` control the interlock at the transition, `NoiseAmount` breaks the boundary with the shared macro noise.

## Moss

`MF_MobTerrainMoss` grows moss where moss actually grows, from four independently weighted signals:

| | |
|---|---|
| **Cavity** | crevices between stones, from the blended layer height. Derivative-free, so unlike a screen-space curvature it does not swim under camera motion |
| **Slope** | flat tops or steep faces, whichever `SlopeFavour` asks for |
| **Shade** | faces turned away from the sun. `SunDirection` is a parameter rather than a light, so it stays a per-level constant |
| **Noise** | the shared macro noise, so the coverage is not uniform |

`Amount` is the global dial, and a painted `Moss` layer adds on top. Moss and wetness are overlays: they sample like a layer but their weight is a painted alpha rather than a weight-blended one, so they never compete for the normalised weight.

## Wetness

`MF_MobTerrainWetness` darkens and smooths the surface, and flattens it out completely where water stands. `Darkening`, `RoughnessTarget`, `NormalFlatten` for the damp pass; `PuddleDepth` and `PuddleRoughness` for standing water, which sits in the deepest part of the cavity and so needs a harder threshold than the darkening around it.

The mask feeds the physical material output, so wet dirt reads as mud underfoot.

## Accumulation

`MF_MobTerrainAccumulation` lays snow, dust or ash over the finished terrain, weighted by which way the ground faces and biased into the crevices. It costs no samples: it reuses the blended cavity and the surface normal.

| | |
|---|---|
| `Accumulation_Amount` | how much has fallen. This is the one to drive |
| `Accumulation_Colour` | white and slightly blue is snow; a warm grey is dust |
| `Accumulation_Facing` | how up-facing the ground must be to hold any |
| `Accumulation_CavityBias` | how much it favours crevices, where a thin covering starts and the last of it survives |
| `Accumulation_NoiseAmount` | breaks the line between covered and bare, so it does not read as a threshold |
| `Accumulation_CoverRoughness` | snow is rough. High |
| `Accumulation_TrampleErase` | how completely a footprint clears the covering |

There is no static switch: it is arithmetic on values the material already has, so `Amount` at 0 costs what a switch would have saved. The recipe's **Landscape Accumulation** decides whether it is in the master at all.

A **paint layer with a slope mask** is still the better answer where snow has to drift in one particular place. This is for a fall that covers everything at once and can be driven from a single number. [`MOBMATERIALS_WEATHER.md`](MOBMATERIALS_WEATHER.md) has the numbers that separate snow from dust and ash.

## Trample

`bTrample`. A world-space record of what has been walked through, read back out of the render target `AMobTrampleVolume` draws into: the ground goes darker, its roughness moves, the surface tilts into the print, and the accumulation comes off it.

Three taps of one render target - the point, and one texel along each axis, which is where the slope comes from. A screen-space derivative would have been one tap, but a screen-space derivative of a world-projected mask changes with the camera and the trench would slide across the ground as you turned.

| | |
|---|---|
| `TrampleMask` | the render target. Must be the same asset the volume draws into |
| `Trample_Depth` | scales what the target holds into how far the surface tilts |
| `Trample_Darkening` | base colour multiplier where the ground is fully broken |
| `Trample_RoughnessTarget` | wet mud is smoother, broken snow is rougher. Both live here |
| `Trample_NormalStrength` | how far the trench tilts the surface |
| `bTrampleWPO` | sinks the ground into a print for real, rather than only shading like it. One vertex tap, off by default |
| `Trample_WPODepth` | how far a full strength print sinks, in world units |
| `Trample_WPOFadeStart`, `Trample_WPOFadeLength` | where the trench flattens out again with distance |

The whole system - placing a volume, what writes it, what it costs and what it cannot do - is in [`MOBMATERIALS_WEATHER.md`](MOBMATERIALS_WEATHER.md#trample).

## Global grade

One Custom node at the end of the chain: `GlobalHue`, `GlobalSaturation`, `GlobalValue`, `GlobalTint`, `GlobalNormalFlatten`, `GlobalRoughnessMin`, plus a distance desaturation and a distance colour for aerial perspective.

## Debug views

`bDebug`, then `DebugMode`, which is a named list on the instance rather than an index:

| | |
|---|---|
| Layer Weights | the first three paint layers as red, green and blue |
| Cavity | blended cavity, which is also what height blending reads |
| Normal | blended normal, in tangent space |
| Wetness | where the terrain counts as wet |
| Height | blended height. Flat grey here means nothing to blend along |
| Vertex Colour | as painted |
| Trample | where the ground has been walked through. Black everywhere no volume covers |
| Accumulation | where snow, dust or ash has settled |

`DebugExposure` scales the view before it is drawn. Height and weights both spend most of their time near the top of their range, and turning this down is what brings the variation that matters back into a range the eye can read.

Weights are the first three layers rather than all of them, because a landscape blend is a chain of pairs and there is no single place holding more than that. Three is enough to read: it is the transitions between neighbouring layers that go wrong, not layer nine on its own.

Terrain has no emissive to borrow, so the view goes to base colour with the normal flattened - an unlit read is the point, and a flat normal is the closest this gets to one.

## Runtime virtual texture

A dozen paint layers blended per pixel is a lot of work to redo every frame for ground that has not changed. With **Build Project Outputs** on, the master gains a **runtime virtual texture output**: the whole blend is rendered once into a cached page and the base pass samples that instead, so the cost stops scaling with layer count.

Two are authored - one carrying base colour, normal and roughness, one carrying world height.

`bUseRVT` on the instance chooses which path the base pass takes. Off routes it through the layer blend directly, which is what you want where `r.VirtualTextures` is off, since the sample would otherwise return nothing.

`wire_landscape_rvt()` points the open level's landscape at both and fits a volume to each.

### Blending meshes into the terrain

The height RVT is what lets a mesh disappear into the ground it is standing in. `MF_MobRVTBlend` reads both textures and fades a mesh's base colour, normal and roughness into the terrain's own over the last few centimetres before its base, so a rock meets the ground instead of cutting a silhouette into it.

| | |
|---|---|
| `Amount` | overall strength |
| `BlendDistance` | world units above the terrain over which the mesh takes over |
| `BlendOffset` | shifts the transition up or down, for a mesh whose pivot is not at its base |
| `BlendSharpness` | how abruptly the mesh wins |

The terrain side comes from the virtual texture rather than a second set of layer parameters, so it always matches what the landscape actually renders - repaint the ground and the meshes standing in it follow.

Two virtual texture samples on the mesh, so put it behind a static switch on the mesh material and only turn it on for things that sit in the ground. It is authored alongside the landscape master whenever project outputs are on, since it cannot exist before the RVTs do.

## Project integration

A runtime virtual texture, footstep surfaces and grass types are **game** assets: they are sized to a landscape, named after a project's physical surfaces, and reference its meshes. Referencing them from the plugin's own material would leave it pointing at packages that do not exist anywhere else, so they are off by default.

Tick **Build Project Outputs** on the recipe and set the paths beneath it:

| | |
|---|---|
| **Runtime Virtual Texture Path** | where the two runtime virtual textures are created |
| **Physical Material Path**, **Layer Info Path** | footstep physical materials, and the landscape's layer info assets |
| **Grass Type Path** | grass type assets. Which meshes each one scatters is still a table in `Python/author_landscape.py` |

Then the master gains its RVT output and sample, its physical material output and its grass output, `bUseRVT` appears on the instances, and `MF_<AssetName>RVTBlend` is authored alongside.

> [!NOTE]
> `configure_layer_infos()` sets blend method and physical material on layer info assets, but it cannot create them: `ULandscapeLayerInfoObject::LayerName` is `VisibleAnywhere`, so Python refuses to set it, and the function that does is not exposed. Create them once from Landscape mode with the **+** next to each target layer, then run it.

`wire_landscape_rvt()` points the open level's landscape at both RVTs and fits a volume to each, building the transform the same way `RuntimeVirtualTexture::SetBounds` does, since `BoundsAlignActor` and the Set Bounds button are editor-only C++.
