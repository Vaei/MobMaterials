# Landscape Reference

Setup and troubleshooting are in [`README.md`](README.md). The surface master is in [`MOBMASTER_SURFACE.md`](MOBMASTER_SURFACE.md).

| System | |
|---|---|
| [Texture packs](#texture-packs) | two textures per layer |
| [Paint layers](#paint-layers) | how many, and what the base layer is for |
| [Blending](#blending) | height interlock rather than cross-fade |
| [Tiling break](#tiling-break) | four tiers, from free to stochastic |
| [Layer masks](#layer-masks) | gating a layer by slope and altitude |
| [Slope rock](#slope-rock) | cliffs without painting them |
| [Moss](#moss) | cavity, shade and slope decide where it grows |
| [Wetness](#wetness) | damp, then standing water |
| [Global grade](#global-grade) | one place to shift the whole terrain |
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

## Paint layers

Edit **Layers** on the recipe. Each entry is a name, a physical surface and a physical material; the last two are only consumed by the project integration phases, never by the graph.

The **first layer is the base**: it holds whatever weight the painted layers leave behind. Without that, untouched ground sits at a painted weight of zero and the first brush stroke blends against nothing, landing as a hard edge.

Layers fold together through a chain of `MF_MobHeightBlendPair` rather than a `LandscapeLayerBlend` node, because a blend node exposes no weight to mask and the layers here have to be gated by slope and altitude.

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

## Global grade

One Custom node at the end of the chain: `GlobalHue`, `GlobalSaturation`, `GlobalValue`, `GlobalTint`, `GlobalNormalFlatten`, `GlobalRoughnessMin`, plus a distance desaturation and a distance colour for aerial perspective.

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
