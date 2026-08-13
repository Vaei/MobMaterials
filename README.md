# Mobile Forward-Rendering Materials <img align="right" width=128, height=128 src="https://github.com/Vaei/MobMaterials/blob/main/Resources/Icon128.png">

> [!IMPORTANT]
> Two AAA-tier master materials
> <br>Landscape master material
> <br>Surface, environment, prop master material
> <br>Designed for lightweight Mobile Forward Rendering pathway

**Suitable for realistic and stylized projects alike**

UE5.8+

---

> [!CAUTION]
> <br>MobMaterials has not officially released. Expect terrible bugs, and updates to occur without versioning or changelog reflecting them. Also any documentation is incomplete, no images or videos are available yet either. **Come back soon!**

<!-- TODO(image): hero shot - terrain with layers interlocking, a rock blending into the ground,
     everything wet -->

<!-- TODO(video): painting layers, then rain coming in across the whole scene -->

## Resources

> [!NOTE]
> While Mobile Forward Rendering can have serious limitations regarding lighting, it is suitable for **many** games, not merely Valorant/TF2 clones
> <br>And these games can look stunningly beautiful and crisp, while retaining ~1000fps in packaged builds

* [Mobile Forward Rendering Guide](https://blog.daftsoftware.com/unreal-perf-maxing/)
  * Includes Convenient Starter Project
* [Valorant Rendering Guide](https://technology.riotgames.com/news/valorant-shaders-and-gameplay-clarity)
  * Valorant uses Mobile Forward Rendering
  * Valorant's rendering is based on Team Fortress 2's techniques
* [Gradient Tool Plugin](https://github.com/Vaei/GradientTool)
  * Valorant and Team Fortress 2 techniques require gradients
  * UE5 has "Color Ramp" node, but any modification requires modifying the base material - Gradient Tool's gradients can be modified in realtime
* [Forward Render Helper Plugin](https://github.com/Vaei/ForwardRender)
  * Replaces Hotpatch Module in Perf Maxing guide above
  * Sets your light vector on the MPC for you

## Features

**Authored from a recipe, not hand-wired.** A recipe is an asset saying what to build and where, so a project carries as many masters as it needs. Each rebuilt with a click. *You never have to touch a base material.*

**Nothing you turn off is paid for.** Every feature is gated by a static bool on a material function input.

<!-- TODO(image): the Mat menu and the generate window, side by side -->

### Terrain that does not repeat, and does not cost what it looks like

- **Stochastic hex tiling with variance restoration.** Three rotated taps on a hex lattice, then the blended result pushed back out from the texture's mean so it does not wash out. No repeat survives it, at any viewing distance
- **The whole layer blend cached into a runtime virtual texture.** Per-pixel cost stops scaling with layer count, so a dozen layers costs what one does. Falls back to blending live where virtual texturing is off
- **Layers interlock rather than cross-fade.** Gravel comes up through grass along its own stones, because the blend is resolved against height instead of lerped

### Terrain that places itself

- **Cliffs appear where the ground gets steep.** No painting a rock layer down every slope
- **Moss grows where moss grows** - in crevices, on shaded faces, off the sun, broken up by noise. Four signals, each weighted, all from data the material already has
- **Rain darkens the world at once**, pooling in the low points first because porosity comes from cavity, and standing water only on surfaces facing up
- **Footsteps and grass come from the paint.** The same weights that blend the ground decide what it sounds like underfoot and what grows out of it

### Meshes that belong to the ground they stand in

- **A rock reads the terrain's own virtual texture and fades into it**, so it meets the ground instead of cutting a silhouette into it. Repaint the ground and everything standing in it follows
- **Triplanar that survives scaling**, with normals reoriented per projection rather than blended flat, so kitbashed geometry needs no UVs
- **Vertex paint with no fill step.** Black adds, white is neutral, so a mesh that was never painted arrives as the base layer instead of covered in the top one, soaking wet
- **Every copy of a prop looks different** from a hash of where it stands - no per-actor setup, no extra instances

### Surfaces that hold up close, and instances that differ

- **A detail normal** over the blended result, faded on the same curve the distance clamp uses, because it is the same high-frequency detail that crawls at range
- **A tiling break bound to distance** - a second incommensurate tiling overlaid, so a wall keeps its detail near and stops reading as a grid far away
- **Parallax, cheap or raymarched.** One-step offset sells brick and cobbles for a multiply and an add; occlusion walks the height field when a hero surface earns it
- **Per-instance tint, roughness and wetness from custom primitive data**, so one material instance serves a thousand actors that all differ, with no new permutations and nothing to author per actor
- **Foliage with wind** - masked, two-sided, and two scales of motion weighted so the base stays planted and the tips move. Light comes through the leaf per texel, from the mask channel a plant has no use for as metallic, so a tip glows where a stem stays opaque
- **Snow, from one number.** Drag `Snow` 0 to 1 and it settles across the world by which way each surface faces, starting in the crevices and covering the flats last. Terrain, props and roofs all follow the same value, and none of it costs a texture sample
- **Footprints through it that clear back to the ground**, darkening, roughening and denting what they cross, from one render target a volume draws into. The terrain can sink into them for real - one vertex tap, opt in
- **And they sound right.** A step on fresh snow plays snow; a step in your own trail plays the earth you uncovered. The audio reaches that from the same three numbers the shader does, so nothing is authored twice
- **Debug views** for layer weights, cavity, wetness and height, because a blend you cannot see is a blend you cannot fix

### Built for the mobile forward path

- **4 samplers however many layers are on.** Every layer sample is Shared:Wrap, so the 16-sampler limit never binds. Only trample and parallax occlusion ever spend a fifth
- **Sub-pixel detail is removed with distance**, because specular that fine crawls without a temporal filter and FXAA cannot save it
- **Cavity, never ambient occlusion.** Micro shadowing multiplies base colour and specular; the AO pin stays free so it cannot double with the renderer's own occlusion
- **A disabled feature is not compiled.** Its texture samples and its maths leave the shader entirely, rather than being multiplied by zero

Full parameter-level detail is in [`MOBMATERIALS_LANDSCAPE.md`](./MOBMATERIALS_LANDSCAPE.md) and [`MOBMATERIALS_SURFACE.md`](./MOBMATERIALS_SURFACE.md). Making a world get wet, snowed on and dusty is a tutorial of its own: [`MOBMATERIALS_WEATHER.md`](./MOBMATERIALS_WEATHER.md).

### Texture packs

| | Layer textures | Packing |
|---|---|---|
| Landscape | `_BC`, `_NRM`, `_HRC` | **H**eight, **R**oughness, **C**avity |
| Surface | `_BaseColor`, `_Normal`, `_CRM` | **C**avity, **R**oughness, **M**etallic |
| Foliage | `_BaseColor`, `_Normal`, `_CRT` | **C**avity, **R**oughness, **T**ransmission |

Neither pack carries ambient occlusion, on purpose. The renderer supplies its own occlusion and a baked AO map on top of it darkens twice. Cavity takes that slot instead: it multiplies base colour and specular, which is what micro shadowing actually does, and never reaches the AO pin.

The landscape gets both, because it has a channel to spare - height drives which layer wins, cavity darkens the crevices. The surface pack has no room for a fourth channel, so cavity does both jobs there.

Blue is the only channel whose meaning depends on the master rather than the texture. A surface master reads it as metallic; a foliage master reads it as transmission, because a plant is never metal and that sample is paid for either way.

**Mob → Remap Texture Channels...** repacks art into any of these from packed ORM, MRAO, RMA, or a map each.

> [!WARNING]
> You may be used to MRA format where R=Metallic, G=Roughness, B=Ambient Occlusion (AO). This format has always been wrong and results in double occlusion.

#### What a surface material asks for

Per layer, and layers 1 and 2 only exist when `bLayer1` / `bLayer2` are on:

| Parameter | Content | Compression |
|---|---|---|
| `Layer0_BC` | albedo | Default, sRGB |
| `Layer0_NRM` | tangent normal | Normalmap |
| `Layer0_CRM` | R cavity, G roughness, B metallic | Masks, sRGB off |

Then, each only sampled when the feature it belongs to is switched on:

| Parameter | Switch | Content |
|---|---|---|
| `MacroNoiseTexture` | `bMacroVariation` | low-frequency world-space noise, so a long wall is not one flat tone |
| `DetailNormal` | `bDetail` | the finer normal laid over the blend, faded with distance |
| `RippleNormal` | `bRipples` | scrolling normal for rain on standing water. Two taps, one texture |
| `TrampleMask` | `bTrample` | not authored. The render target a trample volume draws into |
| `EmissiveMask` | `bEmissive` | where the surface glows |
| `OpacityMask` | base property override | alpha only. Opaque materials compile the sample out |

#### What a foliage material asks for

The same three per layer, with blue read as transmission, plus one more that is always sampled because a foliage master is always masked:

| Parameter | Content | Compression |
|---|---|---|
| `Layer0_BC` | albedo | Default, sRGB |
| `Layer0_NRM` | tangent normal | Normalmap |
| `Layer0_CRT` | R cavity, G roughness, **B transmission** | Masks, sRGB off |
| `OpacityMask` | read from **alpha only** - point it at `Layer0_BC` | Default, sRGB |

The optional maps above are all available here too. Most foliage uses layer 0 alone; layers 1 and 2 are for bark against leaf on one mesh, which is where per-layer `TransmissionScale` earns its place.

There is no thickness map and no subsurface map. Thickness is CRM blue, and the colour of the light coming through is `SubsurfaceColor`, a parameter rather than a texture - one colour for the plant, shaped per texel by that channel.

> [!NOTE]
> `OpacityMask` is its own tap, not a swizzle of the base colour one, so a foliage master samples four textures where a one-layer surface master samples three. Pointing it at the same texture as `Layer0_BC` is the normal thing to do and does not make it free. Samplers do not move - every tap in these masters goes through the shared wrap group.

Every texture parameter defaults to a 4x4 neutral in `Textures/`, so a slot left alone still loads but costs almost nothing.

---

## Project settings

These masters are built for the mobile forward path. Some of this is required for them to work at all; the rest is what makes that path worth being on.

### Required

`Config/DefaultEngine.ini`, under `[/Script/Engine.RendererSettings]`.

| | |
|---|---|
| `r.Substrate=false` | These are legacy material graphs. Substrate is a different material system and they will not compile against it |
| `r.Mobile.ShadingPath=0` | Mobile **forward**. The deferred mobile path has a GBuffer and different rules |
| `r.MobileHDR=True` | Off, there is no tonemapper and no post processing to speak of |
| `+D3D11TargetedShaderFormats=PCD3D_ES31` | So an ES3.1 shader map is actually cooked. Windows targets, alongside SM5 |

Nothing here is checked at runtime. Get it wrong and the symptom is a material that compiles on desktop and behaves differently, or not at all, once the preview platform is ES3.1.

### Strongly recommended

The design assumes these. It still works without them, but decisions in the material stop paying for themselves.

| | |
|---|---|
| `r.ForwardShading=True` | The desktop half of the same lighting model, so a desktop build looks like the mobile one |
| `r.AllowStaticLighting=False` | Fully dynamic. The masters have no lightmap path and the AO pin is deliberately unused |
| `r.Nanite=0`, `r.Shadow.Virtual.Enable=0` | Neither survives the mobile path, and both cost memory being enabled |
| `r.VirtualTextures=True` | Required for the landscape's RVT output and for blending meshes into the terrain. Turn it off and use `bUseRVT` off |

### Worth knowing

| | |
|---|---|
| `r.AntiAliasingMethod=1` (FXAA) | Why the distance clamp exists. With no temporal filter, sub-pixel normal and specular detail crawls, and the material is the only place it can be removed. If you run TAA instead, turn `DistanceNormalFlatten` and `DistanceRoughnessFloor` down |
| `r.EarlyZPass=0` | No depth prepass, so masked geometry costs real overdraw. Stay opaque unless the silhouette needs it |
| `r.DBuffer=False` | No DBuffer decals. Nothing here reads a decal buffer, so this is free to leave off |
| `r.TextureStreaming=False` | Every texture resident at mip 0. This is why every texture parameter defaults to a 4x4 neutral: an unused slot then costs nothing |
| `r.Mobile.Forward.EnableLocalLights=0` | Only the directional light, skylight and reflection captures light these materials |

> [!NOTE]
> The editor previews SM5 or SM6 until something switches it. Set the preview platform to **Android ES3.1** or **PC D3D Mobile** before trusting anything the Material Editor's stats tell you - SM6 will happily compile things that silently vanish at ES3.1.

---

## How to Use

Enable the plugin, then author a master from a **recipe**. A recipe is an asset saying what to build and where, so a project can carry as many masters as it needs - a landscape per biome, a surface master per art style - each regenerated on its own.

**Mob → Generate Materials...** in the level editor toolbar. Pick a recipe or make one (Content Browser → Miscellaneous → Data Asset → Mob Material Recipe), set **Kind**, **Output Path** and **Asset Name**, then **Generate**.

Every recipe in the project also appears directly on the **Mat** menu, so regenerating one later is a single click.

| Also on the Mat menu | |
|---|---|
| **Verify Contract** | asserts what each master claims to cost - taps and samplers per feature, the ambient occlusion pin left free, primitive data indices unmoved. Results in the Output Log |
| **Report Cost** | how many distinct shader maps the instances add up to, and how much texture each master keeps resident |
| **Pack Layers for \<Recipe\>** | packs a landscape recipe's layer textures into the arrays its master samples. Only listed for recipes using them |
| **Simplify Material To Layer...** | turns a landscape material down to one layer so what is on screen is that layer's art and nothing else. Everything it changes is recorded, and Restore in the same window puts it back exactly. Reset takes those same parameters to the parent's values, which is the way out when the recording is gone. For working through layers one at a time before the whole set exists |
| **Fit UV Scale To Landscape...** | reads the landscape's own quad size and writes every layer's `UVScale` so a tile measures the metres you asked for. A resized landscape does not have the quads the defaults assume, and then every layer tiles wrongly at once |
| **Remap Texture Channels...** | repacks incoming art into the HRC a landscape layer reads or the CRM a surface reads. Common layouts are presets; anything else is three slots you set yourself |
| **Snap Selected Actor To Landscape Centre** | drops the selection in the middle of the nearest landscape, sitting on the surface. Where a test mesh wants to be, and otherwise three numbers to work out by hand |
| **Fit Selected Box Volume To Landscape** | centres and scales the selected volume to cover the nearest landscape exactly, streaming proxies included. Clears its rotation, since a turned box cannot be scaled to cover an axis aligned one |
| **Rebake Landscape Physical Materials** | rebakes which physical material the ground reports underfoot. The physical material output is baked into collision data rather than read per trace, so a regenerated master changes nothing about what a footstep hears until this runs. Leaves the mobile preview first, because the landscape refuses to bake below SM5. Save the level afterwards |
| **Open \<MI\>, Open \<Master\>** | the material instance the open level's landscape renders with, and the master behind it. Otherwise it is select a proxy, read a property, hunt in the Content Browser |
| **Open MPC_MobWeather** | straight to the wetness and snowfall dials |
| **Editor Preferences** | per-developer settings, not checked in |
| **Hide This Menu** | takes the Mat button off your toolbar. Turn it back on under Editor Preferences, Plugins, Mob Materials Editor |

Regenerating is idempotent: existing assets are emptied and rebuilt in place, so instances keep their references and their parameter values, and re-running is always safe.

> [!WARNING]
> The editor locks up while a master is generated. It runs on the game thread: a surface master takes a few seconds, but a landscape with a dozen layers rebuilds several hundred nodes and recompiles, which can take minutes with an unresponsive UI. It is working, not hung.

### Surface, environment, props

A surface recipe writes `M_<AssetName>` and four preset instances. Then:

1. Right-click `MI_<AssetName>_Prop` → **Create Material Instance**.
2. Set `Layer0_BC`, `Layer0_NRM`, `Layer0_CRM` to your textures.
3. Put it on a mesh.

That is the whole setup. Everything else is off and costs nothing.

Start from whichever preset is closest instead of turning switches on yourself - each distinct set of switches is another shader permutation, and parenting to a preset is what keeps that count down:

| Preset | |
|---|---|
| `_Prop` | everything off. Three texture samples |
| `_Building` | two layers, vertex paint, wetness |
| `_Rock` | triplanar, wetness |
| `_Lamp` | emissive |

To make it rain, set `Wetness` on the recipe's weather collection from Blueprint or code. Point several recipes at one collection and their materials go wet together.

**Mob → Open MPC_MobWeather** goes straight to it: drag `Wetness` 0 to 1 and every instance with wetness on follows, live in the viewport. The menu lists every collection any recipe names.

> [!NOTE]
> **You do not have to fill a mesh with black before painting.** Most layered materials read vertex colour straight, which means every layer at full weight.
> <br>
> <br>Here the channels are inverted: **black adds, white is neutral**, on all four. Unpainted is the base layer, dry and unshaded. The brush is subtractive - you paint black to add a layer.

> [!NOTE]
> Full documentation: [`MOBMATERIALS_SURFACE.md`](./MOBMATERIALS_SURFACE.md).

### Landscape

Set the recipe's **Layers** - the first is the base, and it holds whatever weight the painted layers leave behind. Generate, then:

1. Assign `MI_<AssetName>` to your landscape.
2. In Landscape mode, press **+** beside each target layer to create its layer info asset.
3. Point each layer's `_BC`, `_NRM`, `_HRC` parameters at your textures.
4. Paint.

Layer count is built into the material graph, so it cannot be changed on an instance - edit the recipe and generate again.

Past about four layers, tick **Texture Array Layers**: every layer is then one slice of three arrays rather than three textures of its own, so eight layers is three texture objects instead of twenty-four. Set **Layer Texture Root**, run **Mob → Pack Layers**, then generate.

> [!NOTE]
> Full documentation: [`MOBMATERIALS_LANDSCAPE.md`](./MOBMATERIALS_LANDSCAPE.md).

### Try it

**Test Level** in the generate window builds a level demonstrating a surface master, one feature per object - plain, three-layer blend, vertex paint, triplanar, wetness, per-object variation, emissive - using the placeholder textures and separating the layers by tint, so nothing needs art to read.

It opens a new level, so it asks first.

### Then what

[`MOBMATERIALS_TUTORIAL.md`](./MOBMATERIALS_TUTORIAL.md) walks the whole feature set in the order that needs the least backtracking - layers and blending, tiling break, detail, parallax, per-instance variation, weather, foliage, terrain - with the wrong turns people actually hit. Start there once the above works.

### Runtime virtual texture, footsteps, grass

These reference assets a game owns, so they are off until asked for. Tick **Build Project Outputs** on a landscape recipe and set the four paths beneath it. That adds the RVT output and sample, the footstep physical material output and the grass output to the master, and authors `MF_<AssetName>RVTBlend` for fading meshes into the terrain.

### Footprints

Place an **AMobTrampleVolume** over the ground, point it at the generated `RT_<AssetName>Trample`, `MPC_MobTrample` and the two stamp materials, set the same render target on the instance's `TrampleMask`, and call `Add Trample` from wherever a foot lands. Full walkthrough in [`MOBMATERIALS_WEATHER.md`](./MOBMATERIALS_WEATHER.md#trample).

---

## If it's wrong

### Landscape

In order of likelihood.

| Symptom | Cause |
|---|---|
| Layers cross-fade into mush | `BlendHeightAmount` is 0, which is the plain cross-fade. Raise it, then widen `BlendHeightContrast` |
| A transition is a hard crunchy edge | `BlendHeightContrast` too low. Contrast only widens the interlock band, it never softens the interlock itself |
| Hex tiling looks washed out | Three taps average the variance away. Raise `VarianceRestore` and set `MeanColor` to the texture's actual average |
| A layer will not paint | Its layer info asset does not exist yet. Create it from Landscape mode with the **+** beside the target layer, then run `configure_layer_infos()` |
| Mip seams across hex cells | A tap lost its explicit gradients. Every tier needs them: the samplers are Shared:Wrap and the coordinates are discontinuous per cell |
| Grass or footsteps do nothing | **Build Project Outputs** is off on the recipe, so those outputs are not on the material |
| Meshes cut a hard silhouette into the ground | `MF_<AssetName>RVTBlend` is only authored with Build Project Outputs on, and has to be added to the mesh's own material |
| Terrain renders but ignores paint | `bUseRVT` is on and the RVT volume is missing or unfitted. Run `wire_landscape_rvt()`, or turn `bUseRVT` off |

### Surface

In order of likelihood.

| Symptom | Cause |
|---|---|
| An unpainted mesh is covered in the top layer | Vertex colour is inverted from what you expect. Black adds, white is neutral. Fill white, paint black |
| A mesh is soaking wet with wetness at 0 | Same cause: vertex colour blue is a wetness boost and it reads black as wet |
| Triplanar texture swims or lights from the wrong side | The layer is triplanar but the mesh is being scaled non-uniformly *and* rotated. Triplanar is world-aligned by definition and does not rotate with the object |
| Distant surfaces sparkle | The distance clamp is disabled by its scalars. Raise `DistanceNormalFlatten` and `DistanceRoughnessFloor` |
| Masked instance clips everything | The opacity mask is read from **alpha**, not a colour channel. A greyscale mask in RGB samples as 1 in alpha, or as 0 if the texture has none |
| Metal everywhere | The `_CRM` blue channel is metallic. A pack that puts occlusion there needs `MetallicScale` at 0 |
| Foliage went flat and dark when `bTransmissionMap` was ticked | The pack in `Layer0_CRT` has metallic in blue, not transmission, and metallic is 0 on a plant - so no light gets through. Repack it: **Remap Texture Channels**, target **CRT** |
| Macro variation does nothing | `MacroNoiseTexture` defaults to a flat 4x4. It needs a real tiling noise, which the plugin does not ship |
| Emissive does nothing | `EmissiveMask` defaults to black. Assign a mask, or a white texture for a flat glow |

---

> [!NOTE]
> Generating needs the **Python Editor Script Plugin**, which this plugin enables. A material that has already been authored works without it. The generators can also be driven straight from the console - see `Python/author_landscape.py` and `Python/author_surface.py`.

## Performance and profiling

Measured from the generated HLSL for the surface master:

| | Texture taps | PS instructions |
|---|---|---|
| Everything off | 3 | 996 |
| One extra layer | 6 | 1082 |
| One triplanar layer | 9 | 1051 |
| Three layers, all triplanar | 27 | 1289 |

Samplers stay at **4 in every permutation**.

> [!WARNING]
> The Material Editor's sampler stat over-reports. It counts the three `Texture2DSample` overload declarations and the engine's own BRDF lookup alongside the material's actual taps - 12 where the shader really contains 9. Dump the shader with `r.DumpShaderDebugInfo 1` and count `Texture2DSample(Material_` when the number matters.

Every distinct set of static switches is a distinct shader map, and PSOs are per vertex factory times material. Parenting to a small fixed set of presets is what keeps the PSO count from growing with the art.

## Changelog

### 1.0.0
* Initial release
* Landscape master: paint layers with height-interlocked blending, stochastic hex tiling and a cheap dual-scale tier, slope rock, moss placed by cavity and slope, slope and altitude layer masks, wetness, optional texture array layers
* Surface master: three layers, vertex paint, triplanar, height blending, detail normals, distance tiling break, parallax offset and occlusion, wetness with puddles and rain ripples, accumulation for snow, dust and ash, colour and macro variation, custom primitive data, emissive
* Foliage master: masked, two-sided foliage shading, subsurface with an optional per-texel transmission mask in the blue channel, wind on world position offset
* Project integration: runtime virtual texture with mesh blending, physical material output for footsteps, grass output
* Debug views on both masters, sent to emissive with base colour blacked out
* Recipe assets drive generation, so a project carries as many masters as it needs
* Mat toolbar menu: generate, pack layer arrays, test level, contract verification, cost reporting, weather collection, and a per-developer hide
* Fit UV Scale To Landscape: every layer's tile size worked out from the landscape's own quad size
* Remap Texture Channels: repacks ORM, MRAO, RMA or separate maps into HRC, CRM or CRT, with a custom slot per output channel
