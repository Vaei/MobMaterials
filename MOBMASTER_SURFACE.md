# Surface Reference

Setup and troubleshooting are in [`README.md`](README.md). The landscape master is in [`MOBMASTER_LANDSCAPE.md`](MOBMASTER_LANDSCAPE.md).

| System | |
|---|---|
| [Texture packs](#texture-packs) | what the three samplers expect, and why there is no AO |
| [Layers](#layers) | three of them, and how they interlock |
| [Vertex paint](#vertex-paint) | which channel drives what, and why black adds |
| [Projection](#projection) | mesh UV or triplanar, per layer |
| [Tiling break](#tiling-break) | stopping a tiled surface reading as a grid at range |
| [Parallax](#parallax) | depth without geometry, cheap and expensive |
| [Wetness](#wetness) | one global value, and what it does to a surface |
| [Accumulation](#accumulation) | snow, dust, ash - settling by which way a surface faces |
| [Colour variation](#colour-variation) | per object, and across a single mesh |
| [Per-instance data](#per-instance-data) | telling one actor to be a different colour |
| [Cavity](#cavity) | micro shadowing, and why it never reaches the AO pin |
| [Emissive](#emissive) | a masked glow |
| [Detail](#detail) | the second normal that makes a surface hold up close |
| [Distance](#distance) | the clamp that stops speculars crawling |
| [Blend modes](#blend-modes) | opaque, masked, two-sided |
| [Foliage](#foliage) | a different master, and why it has to be |
| [Debug views](#debug-views) | seeing the blend instead of guessing at it |
| [Cost](#cost) | what each feature actually costs, measured |

---

## Texture packs

Three samplers per layer.

| Suffix | Content | Compression |
|---|---|---|
| `_BaseColor` | albedo | Default, sRGB |
| `_Normal` | tangent normal | Normalmap |
| `_CRM` | **C**avity, **R**oughness, **M**etallic | Masks, sRGB off |

There is no ambient occlusion channel, on purpose. The renderer supplies its own occlusion and a baked AO map on top of it darkens twice. Cavity takes that slot instead, and it does double duty as the height field for layer blending, so height blending needs no fourth texture.

Art almost never arrives packed as CRM. **Mob → Remap Texture Channels...** repacks it: choose how the incoming maps are laid out - packed ORM, MRAO, RMA, or a texture each - and it fills the three output slots, which stay editable for anything it does not recognise. No preset wires ambient occlusion anywhere, for the reason above.

Every texture parameter defaults to a 4x4 neutral in `Textures/`. That matters more than it sounds: a parameter left at its default still holds a reference and still loads, even when its sample has been compiled out of a dead branch. Defaulting to a 4x4 makes an unused slot free.

## Layers

Three, each a full pack with its own `UVScale`, `UVRotation`, `UVOffsetU`/`V`, and its own grading. Layers 1 and 2 are gated by `bLayer1` and `bLayer2`.

Blending is height-aware, resolved against cavity, so layers interlock along their detail rather than cross-fading:

| | |
|---|---|
| `BlendHeightAmount` | 0 is a plain cross-fade, 1 is a pure height interlock |
| `BlendContrast` | how wide a band the interlock happens over. Low reads as a hard crunchy edge |
| `LayerN_HeightBias` | pushes one layer up or down the blend |

## Vertex paint

**Black adds, white is neutral.**

| Channel | White | Black |
|---|---|---|
| R | layer 0 | layer 1 |
| G | layer 0 | layer 2 |
| B | dry | wet |
| A | no darkening | darkened |

> [!IMPORTANT]
> A mesh with no vertex colour buffer gets white from the vertex factory. That is why neutral is white rather than black: any mesh that reaches this material unpainted would otherwise arrive as the top layer, soaking wet, with nothing to say so. It costs the usual convention - the brush is subtractive, so you paint black to add a layer - and buys a default that is right.

`bVertexPaint` off swaps the channels for `Layer1Weight` / `Layer2Weight` / `WetnessPaint` / `VertexShade` scalars, for meshes that cannot be painted.

## Projection

Per layer, `LayerN_Triplanar`. Off is a single mesh-UV tap. On is world-aligned triplanar: three taps per texture, correct under non-uniform scale, no UVs needed.

`TriplanarScale` is world units to UV. `TriplanarSharpness` is how narrowly the three projections cross-fade.

Normals are handled properly rather than blended flat: each projection is reoriented against the geometric normal with a whiteout blend before the three are combined, then the result is taken back to tangent space. A face that is mostly but not exactly axis-aligned keeps the slope the other two projections were carrying.

## Tiling break

`LayerN_TileBreak`. A second tiling of the same texture, at a scale and rotation incommensurate with the first, crossfaded in with distance.

The repeat only becomes visible once enough tiles are on screen at once, so the break is bound to distance rather than applied everywhere: near keeps the detail it was authored with, far dissolves the grid.

| | |
|---|---|
| `TileBreakScale` | second tiling relative to the first. Keep it irrational-ish - 0.37 rather than 0.5, or the two repeats line up and you have made a bigger grid |
| `TileBreakStart`, `TileBreakFalloff` | where it comes in |
| `TileBreakAmount` | how far it goes |

The two tilings combine as an **overlay**, not a lerp. A lerp of two scales halves the contrast and reads as blur, trading one artefact for another; overlay keeps the detail and only disturbs the low frequency that made the repeat visible. Normals blend rather than multiply, which would flatten them.

**Mesh-UV path only.** With triplanar on it does nothing: that already samples three ways, and breaking it too would be nine taps to solve a repeat the projection has largely hidden.

Three extra samples on any layer that uses it. Leave **Distance Tiling Break** off on the recipe and it is not in the master at all.

## Parallax

Two modes per layer, both shifting the UV along the view direction so a flat surface reads as though it has depth. Neither changes the silhouette: at a grazing angle the edge is still flat, which is the honest limit of the trick.

`LayerN_Parallax` turns it on. `LayerN_ParallaxOcclusion` picks the expensive mode.

| | |
|---|---|
| **Offset** (default) | one step. A multiply and an add on top of one extra height tap. Sells brick, cobbles and plank gaps at anything but a grazing angle. This is the one to reach for here |
| **Occlusion** | raymarched. Walks the view ray through the height field until it goes under, then refines between the last two samples so the step count does not band. Genuinely expensive |

| | |
|---|---|
| `ParallaxAmount` | depth in UV units. Small - 0.02 to 0.06. Past that the surface swims |
| `ParallaxSteps` | raymarch steps, occlusion only |

Depth comes from the CRM red channel, the same cavity the layers blend against, tapped once at the unshifted coordinate before the offset is applied - so parallax costs one extra sample even in the cheap mode.

> [!CAUTION]
> Occlusion mode is the **only** thing in either master that spends a sampler slot. A raymarch cannot be expressed as graph taps, so its Custom node samples the height texture itself and brings its own sampler. Everything else here shares one. Budget for it, and do not put it on more than a hero surface.

Leave **Parallax** off on the recipe and neither mode is in the master.

## Wetness

`bWetness`. The amount comes from a Material Parameter Collection, so weather moves the whole world at once rather than per instance.

| | |
|---|---|
| `Wetness` (collection) | the global value. Drive it from gameplay |
| `Wetness_LocalAmount` | per instance, multiplies the global. 0 opts an interior out |
| vertex paint B | adds on top, for a mesh that holds water somewhere specific |

Water reaches crevices before high points, because porosity comes from cavity (`PorosityAmount`). On top of that: `Darkening`, `RoughnessTarget`, `NormalFlatten`, `SpecularTarget`.

Standing water is a harder threshold than the damp darkening around it: `PuddleDepth`, `PuddleRoughness`, and `PuddleFacing`, which gates puddles to up-facing surfaces so water does not cling to a wall.

## Accumulation

`bAccumulation`. A covering that settles by which way a surface faces - snow on a ledge, dust on a shelf, ash on a sill.

| | |
|---|---|
| `Accumulation_Amount` | how much has fallen. The one to drive |
| `Accumulation_Colour`, `Accumulation_CoverRoughness` | what it is |
| `Accumulation_Facing` | how up-facing a surface must be to hold it. This is what makes it snow rather than paint |
| `Accumulation_CavityBias` | how much it favours crevices, which is where a thin covering starts and the last of it survives |
| `Accumulation_NoiseAmount` | breaks the line between covered and bare so it does not read as a threshold |

No extra samples: it reuses the cavity the layers already blended and the surface normal the material already has. Snow, dust and ash are the same system with different numbers - see [`MOBMASTER_WEATHER.md`](./MOBMASTER_WEATHER.md) for values.

It runs **after** wetness, so a covering sits on top of a wet surface rather than being darkened by it. That is the right order: snow that has just landed is not wet yet.

## Colour variation

Two independent sources, `bColorVariation` and `bMacroVariation`.

**Per object** hashes `ObjectPositionWS`, so every copy of a mesh differs with no per-actor setup. `HueRange`, `SaturationRange`, `ValueRange`. This is the one for crates, barrels, roof tiles.

**Macro** is low-frequency world-space noise, and it is the only mode that varies *across* a single mesh, which is what a long wall or a building needs. `NoiseScale`, `MacroTintA`/`B`, `MacroTintAmount`, `MacroValueAmount`.

Per layer there is also `HueShift`, `Saturation`, `Value`, `Contrast` and `Tint`, so one texture set can serve several materials without costing more memory.

## Per-instance data

`bPrimitiveData`. Reads tint, a roughness offset and a wetness offset from **custom primitive data** on the component.

This is the cheapest per-instance variation there is: no new material instance, no new shader permutation, and it can be set from Blueprint at runtime. It is also the one that scales - a thousand actors sharing one instance still draw as one material.

Colour variation by position hash and this are not the same tool. The hash gives every copy *a* different tint; primitive data gives a copy *the* tint somebody chose.

| Index | |
|---|---|
| 0, 1, 2, 3 | `PrimitiveTint`, multiplied over whatever the layers and the variation decided. A vector takes four |
| 4 | `PrimitiveRoughness`, added |
| 5 | `PrimitiveWetness`, added, before the global weather value scales it |

Indices are fixed rather than exposed, because they are a contract with whatever sets them: change them here and every Blueprint calling `SetCustomPrimitiveDataFloat` silently means something else.

Custom primitive data is a flag on a parameter rather than a node of its own, which is worth knowing: the parameter keeps its default when nothing sets the data, so an actor that was never told anything looks the way the instance says it should rather than black.

On the component, set **Num Custom Data Floats** to at least 5.

## Cavity

Cavity is micro shadowing. It multiplies BaseColor and modulates Specular, and it never reaches the AO pin, which stays at 1.

`CavityColorAmount`, `CavitySpecularAmount`, per-layer `CavityContrast`.

## Emissive

`bEmissive`. A mask texture times `EmissiveColor` times `EmissiveIntensity`. Off, the mask is never sampled and the pin is never written.

## Detail

`bDetail`. A second, much finer normal laid over whatever the layers blended to, faded out with distance.

This is most of what separates a base layer from a finished surface at arm's length, and it is one texture sample for the whole material rather than one per layer - a detail normal is the same high-frequency break whichever layer it lands on.

| | |
|---|---|
| `DetailNormal` | the texture. Tiles far tighter than the layers do |
| `DetailScale` | multiplies the mesh UV. 8 is a reasonable start |
| `DetailStrength` | how much slope it contributes |

It fades on the same curve as the distance clamp, and for the same reason: kept at range it is exactly the sub-pixel noise the clamp exists to remove.

Leave **Detail Maps** off on the recipe and none of this is in the master at all - no parameters, no sample, no switch.

## Distance

Always on, no switch. Normal detail and specular highlights finer than a pixel have nothing to resolve them without a temporal filter, and read as a crawling sparkle under FXAA. Taking them out in the material is the only place that can be fixed.

`FadeStart`, `FadeLength`, `DistanceNormalFlatten`, `DistanceRoughnessFloor`.

## Blend modes

Opaque and one-sided by default. Masked and two-sided are per-instance **base property overrides**, not static switches, so the common path pays nothing for them.

The opacity mask is read from **alpha**, which skips the sRGB decode, so the threshold authored in the mask is the threshold the clip uses. A masked prop can point it straight at its own BaseColor.

> [!CAUTION]
> Masked geometry costs real overdraw wherever the project runs without a depth prepass. Stay opaque unless the silhouette needs it.

## Debug views

`bDebug`, then `DebugMode`, which is a named list on the instance rather than an index:

| | |
|---|---|
| Layer Weights | one per channel. Red is layer 0, green layer 1, blue layer 2 |
| Cavity | blended cavity, which is also what height blending reads |
| Normal | blended normal, in tangent space, as a normal map reads |
| Wetness | where the surface counts as wet |
| Height | blended height. Flat grey here means nothing to blend along |
| Vertex Colour | as painted |

`DebugExposure` scales the view before it is drawn. Several of these sit near white on their own - a height that never leaves the top of its range, a weight parked at one - and turning it down is what brings the variation in them back into a range the eye can read.

The result goes to **emissive** with base colour blacked out, so what you see is the value itself rather than the value times whatever the light was doing.

Layer weights are the one worth reaching for. A wrong weight is invisible in the final image precisely when it matters, because it looks like a texture choice rather than a mistake - and the weights are the one thing no other view can reconstruct, which is why the blend hands them out.

## Foliage

Tick **Foliage** on a recipe and it authors a foliage master instead of a standard one: masked, two-sided, two-sided-foliage shading, a subsurface colour for light coming through a leaf, and wind on world position offset.

This is the one feature that **cannot** be a switch. Shading model, two-sidedness and blend mode are material properties rather than parameters, so foliage has to be a material of its own. Point a second recipe at the same output folder with a different asset name and generate - you get `M_MyFoliage` beside `M_MySurface`, sharing one copy of the functions.

### Wind

`bWind`, then two scales of the same motion:

| | |
|---|---|
| `WindDirection` | which way the sway goes |
| `WindStrength`, `WindSpeed` | the trunk sway, slow, carrying the whole plant |
| `WindFlutterStrength`, `WindFlutterSpeed` | the leaf flutter, fast and small, across the sway direction so leaves do not all travel along one line |

Movement is weighted by height above the object's own origin, so the base stays planted and the tips move - no skeleton, no painted weight needed. **Vertex colour red** scales it where it has been painted; unpainted it is white, which is already the tips-move-most case.

Phase comes from world position, so two plants side by side never move together.

> [!NOTE]
> Wind is world position offset, which runs in the vertex shader on every vertex whether or not the plant is on screen edge. Foliage meshes should be low enough poly that this is free; a dense mesh with wind is a vertex-bound mesh.

## Cost

Feature gating is done with static bools on the material function inputs, not switches in the master. A material function input that no live branch reads is never compiled, so switching a layer off takes its texture samples and its grade with it rather than multiplying them by zero.

Measured from the generated HLSL:

| | Texture taps | PS instructions |
|---|---|---|
| Everything off | 3 | 996 |
| One extra layer | 6 | 1082 |
| One triplanar layer | 9 | 1051 |
| Three layers, all triplanar | 27 | 1289 |

**Samplers stay at 4 in every permutation.** Every sample is Shared:Wrap, so they collapse onto one sampler state and the 16-sampler limit never binds no matter how many layers are on.

> [!NOTE]
> The Material Editor's sampler stat over-reports. It counted 12 for the triplanar case above, where the shader really contains 9: the stat also counts the three `Texture2DSample` overload declarations and the engine's own BRDF lookup. Dump the shader and count `Texture2DSample(Material_` if the number matters.

Every distinct set of static switches is a distinct shader map, and PSOs are per vertex factory times material. Parent asset instances to a small fixed set of presets rather than letting every asset flip its own switches, or the PSO count grows with the art.
