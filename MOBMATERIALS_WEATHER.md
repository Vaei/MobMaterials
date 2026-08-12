# Weather

Making a world get wet, then snowed on, then dusty, and having it look like weather rather than like someone swapped a texture.

Everything here is already in the surface and landscape masters. None of it needs new art, and all of it is off until asked for.

| | |
|---|---|
| [Rain](#rain) | one value, the whole world |
| [Puddles](#puddles) | water that gathers where water gathers |
| [Ripples](#ripples) | the thing that makes it read as rain |
| [Snow](#snow) | settling by which way a surface faces |
| [Dust and ash](#dust-and-ash) | the same system, different numbers |
| [Trample](#trample) | footprints, and what walks out of the snow |
| [Driving it](#driving-it) | from Blueprint, and over time |
| [Terrain](#terrain) | the landscape does this too, separately |
| [What it costs](#what-it-costs) | measured |

---

## Rain

Turn on `bWetness` on any instance that should respond, then drive one value.

**Mob → Open MPC_MobWeather**, and drag `Wetness` from 0 to 1. Everything with wetness on darkens and tightens together.

That single value is the whole point. Wetness is not a per-material look, it is a state the world is in, so it lives in a parameter collection rather than on each instance. Point several recipes at one collection and a whole project follows it.

Per instance, on top of the global value:

| | |
|---|---|
| `Wetness_LocalAmount` | scales the global. **0 opts out entirely** - use it for interiors, undersides, anything sheltered |
| `Darkening` | how much the albedo drops. Wet things are darker because the water is doing the reflecting instead of the surface |
| `RoughnessTarget` | what roughness wet reaches. Low. This is most of the effect |
| `NormalFlatten` | water filling in the fine detail |
| `PorosityAmount` | how much cavity decides where it wets first |

Vertex colour **blue** adds on top, painted per mesh, for a surface that holds water in one particular place.

> [!TIP]
> Start with only `RoughnessTarget` and `Darkening`. Wet is mostly a roughness change - if it does not read at `Wetness` 1 with those two alone, the problem is the lighting, not the material.

## Puddles

Standing water is a harder threshold than the damp darkening around it, and it needs somewhere to stand.

| | |
|---|---|
| `PuddleDepth` | how deep into the cavity water has to pool before it counts as standing |
| `PuddleRoughness` | near 0. A puddle is a mirror |
| `PuddleFacing` | how up-facing a surface must be. This is what stops water clinging to walls |

Porosity comes from cavity, so water reaches the crevices before the high points, and puddles form in the low ones. That is why a surface with a flat cavity channel gets uniform wetness and no puddles - the material has nothing to tell it where low is.

## Ripples

`bRipples`. Two scrolling normals, gated by the puddle mask.

This is the difference between wetness that reads as weather and wetness that reads as a darker texture. A still puddle is a wet-looking surface; a moving one is rain.

| | |
|---|---|
| `RippleNormal` | any tiling normal. A generic noise normal works; a real ripple texture works better |
| `RippleScale` | tiling. Higher than you expect - ripples are small |
| `RippleSpeed` | how fast the two layers drift |
| `RippleStrength` | how much they disturb the surface |

The two sets scroll at different rates and directions, so their interference never repeats on a period anyone can see. They only appear where `Puddle` is non-zero, so dry ground stays still and you can leave this on.

> [!IMPORTANT]
> Ripples do nothing without puddles. If `PuddleDepth` is low or the surface is not facing up, there is no puddle mask to gate them and nothing will move. Check the puddle mask first - the **Wetness** debug view shows it.

## Snow

Snow is one number for the whole world, and everything else follows it: the terrain, the props standing on it, the footprints through it, and what a step sounds like.

**Mob → Open MPC_MobWeather**, and drag `Snow` 0 to 1.

That single value is the point. How much has fallen is a state the world is in, not a per-material look, so it lives in the collection beside `Wetness` and every master reads it. `bAccumulation` on a surface instance, or the recipe's **Landscape Accumulation** on the terrain, decides who listens.

| | |
|---|---|
| `Snow` (collection) | how much has fallen. **This is the one to drive** |
| `Accumulation_LocalAmount` | scales the global per instance. **0 opts out entirely** - use it for anything sheltered |
| `Accumulation_Colour` | white, slightly blue. The default is snow |
| `Accumulation_Facing` | how up-facing a surface must be to hold it. Raise it and only flat tops catch |
| `Accumulation_CavityBias` | how much it favours crevices. A thin covering starts in the cracks and the last of it survives there |
| `Accumulation_NoiseAmount` | breaks the line between covered and bare, so it does not read as a threshold |
| `Accumulation_CoverRoughness` | snow is rough. High |

It costs no extra samples: it reuses the cavity the layers already blended and the surface normal the material already has.

**A worked snowfall:**

1. `Accumulation_Colour` to near-white, `Accumulation_CoverRoughness` to 0.85.
2. `Accumulation_Facing` to 0.6 - only surfaces facing meaningfully upward.
3. `Accumulation_CavityBias` to 0.5, so it starts in the crevices.
4. Drive the collection's `Snow` 0 to 1 and watch it fill from the cracks outward, then cover the flats.

### Walking through it

Snow is the case trample was built for. A print clears the covering back to the ground underneath rather than denting it - `Accumulation_TrampleErase` at 1 - so a trail through fresh snow reads as bare earth showing, which is what it is.

Three things want setting for snow rather than mud:

| | |
|---|---|
| `Trample_RoughnessTarget` | **0.9**. The default 0.35 is the mud case: wet, and smoother than its surroundings. Broken snow is rougher |
| volume `FadeHalfLife` | **0**. Snow keeps prints until something covers them again |
| `bTrampleWPO` | optional. The ground sinks into the print for real rather than only shading like it |

**And it sounds like snow.** `UNQFootstepComponent` reads the same `Snow` value the material does and, where the surface faces up enough to hold a covering and nothing has walked it off yet, plays the snow footstep instead of the ground's own. Walk your own trail back and you hear the earth again. Nothing about that is authored twice: the audio reaches the same conclusion from the same three numbers the shader uses.

For snow **and** wet ground together, turn both on. Accumulation runs after wetness, so snow sits on top of a wet surface rather than being darkened by it - which is the right order: snow that has just landed is not wet yet.

## Dust and ash

The same system, different numbers. Nothing about it is snow-specific except the defaults.

| | Colour | Facing | Cavity bias | Cover roughness |
|---|---|---|---|---|
| **Snow** | near white, cool | 0.6 | 0.4 | 0.85 |
| **Dust** | pale warm grey | 0.3 | 0.7 | 0.95 |
| **Ash** | mid grey, desaturated | 0.4 | 0.6 | 0.9 |
| **Sand drift** | warm ochre | 0.5 | 0.8 | 0.8 |

Dust wants a **low facing** and a **high cavity bias**: it settles on anything roughly horizontal and hides in every crack, where snow wants surfaces genuinely facing the sky. That one difference is most of what separates them.

## Trample

`bTrample`. Where the ground has been walked through: darker, rougher or smoother, dented, and with the snow taken back off it.

Everything above is a function of the surface itself, so it can be worked out per pixel from what the material already has. Trample cannot: it is a record of what happened, so something has to keep that record. `AMobTrampleVolume` is a box with a render target behind it, projected straight down, and the material reads it back.

### Setting one up

1. Place an **AMobTrampleVolume** over the ground that should take prints. **Mat → Fit Selected Box Volume To Landscape** sizes it to the terrain, or set the box by hand for a smaller area.
2. Point its **Mask** at a render target. Generating authors `RT_<Master>Trample` beside the master at 2048 square; duplicate it per area if two volumes need different resolutions.
3. Point its **Collection**, **Stamp Material** and **Fade Material** at `MPC_MobTrample`, `M_MobTrampleStamp` and `M_MobTrampleFade`.
4. On the material instance, set **Trample Mask** to the *same* render target, and turn `bTrample` on.
5. Call `Add Trample` wherever a foot lands.

```
Add Trample (World Location, Radius 24, Strength 1.0, Rotation Degrees)
```

It knows nothing about who asked, so a bot leaves the prints a player does, and a cart could leave a rut without any of this changing. A position no volume covers does nothing, which is the common case and not an error.

For anything that scrapes rather than steps, `Add Trample Trail` marks a line between two points at a spacing. A frame at a run is metres of ground, so stamping only where something is now leaves a dotted line behind it. **UMobTrampleTrailComponent** wraps that for a dragged body: add it, `Set Trail Active` while the dragging lasts, and it traces down to the ground and marks its own path. Off, it does not tick.

### What it does

| | |
|---|---|
| `Trample_Depth` | scales what the target holds into how far the surface tilts |
| `Trample_Darkening` | base colour multiplier where the ground is fully broken |
| `Trample_RoughnessTarget` | wet mud is smoother, broken snow is rougher. Both live here |
| `Trample_NormalStrength` | how far the trench tilts the surface |
| `Accumulation_TrampleErase` | how completely a print clears the covering |

On the volume, `FadeHalfLife` is how long a print takes to lose half its depth. **Zero never fades**, which is what snow that settles once wants; a few seconds is right for wet mud that runs back in.

### Geometry

`bTrampleWPO`, on the landscape master, off by default. The ground actually sinks into a print rather than only shading like it.

| | |
|---|---|
| `Trample_WPODepth` | how far a full strength print sinks, in world units. 8 is a boot in snow |
| `Trample_WPOFadeStart`, `Trample_WPOFadeLength` | where the trench flattens out again with distance |

One vertex tap, and about 47 vertex instructions - but paid on every landscape vertex, in the base pass and in every shadow pass, which is why it is opt-in and why the distance fade is not optional. Nothing is added to the pixel shader.

What it can resolve is the landscape's own vertex spacing. A quad is one unit of the landscape actor's scale, so a boot at 24 cm across a landscape scaled 15.87 spans about three vertices: enough for a soft dimple and a trail, not enough for a boot shape. The print's *shape* comes from the normal imprint either way, which is why this sits on top of trample rather than replacing it.

Vertices sink and never lift, because collision does not move with them. At these depths a foot lands a few centimetres above the trench floor and nothing notices; at ten times the depth it would.

### What it cannot do

Without `bTrampleWPO` the mask is a shading imprint, not geometry: the silhouette of the ground does not change, so at a grazing angle a print is flat - the same honest limit parallax has.

Collision never follows, switch or no switch. The heightfield is untouched, so foot IK traces and footstep queries hit the undeformed ground.

Resolution is the render target against the volume: 2048 across an 80 m box is 4 cm a texel, which is about as coarse as a boot survives. A larger volume at the same resolution reads as a trail rather than as prints.

The target is RGBA8 and only red is ever read, which looks like three channels of waste. A single channel target is not the saving it appears to be: drawn through a canvas it comes back saturated, so the format that works is the one that carries channels nobody wants.

One volume is read at a time. The bounds live in a parameter collection, which holds one set of values, so two overlapping volumes would each be describing the other's texture. Every master that should take prints from a volume has to name the **same** collection, or only one of them gets any.

> [!IMPORTANT]
> The render target on the volume and the `TrampleMask` parameter on the instance have to be the same asset. Nothing checks it: a mismatch reads as trample simply never appearing, because the material is sampling a target nobody is drawing into.

## Driving it

The collection is a normal `UMaterialParameterCollection`.

```
Set Scalar Parameter Value (Collection: MPC_MobWeather, Parameter: Wetness, Value: 0..1)
```

For a storm rolling in, interpolate it rather than setting it - wetness arriving instantly reads as a bug. A minute from 0 to 1 is not too slow.

Accumulation is a **material instance** parameter rather than a collection one, because snow depth is usually per-surface: a roof and the ground under it fill at different rates. Drive it with a dynamic material instance, or promote it to your own collection parameter if you want one global snow level.

> [!TIP]
> Wetness and accumulation are the two ends of the same storm. Ramp wetness up as rain starts, hold it, then ramp accumulation as it turns to snow while wetness falls away. The order the material applies them - wetness, then ripples, then accumulation - is already the order weather happens in.

## Terrain

The landscape master has its own wetness, and it is not the same system.

| | |
|---|---|
| `Wetness_Amount` | the global dial, plus a paintable `Wetness` layer for places that are always damp |
| `Darkening`, `RoughnessTarget`, `NormalFlatten` | as above |
| `PuddleDepth`, `PuddleRoughness` | standing water in the terrain's own cavity |

The wet mask also feeds the **physical material output**, so wet dirt reads as mud underfoot - the footstep sound follows the weather without anyone wiring it up.

Accumulation and trample are on the landscape master too, and behave as they do above. Terrain accumulation is gated by its `Amount` rather than by a switch, since it costs no samples either way; a paint layer with a slope mask is still the better answer where snow has to drift in one particular place rather than fall on everything at once.

## What it costs

| | |
|---|---|
| Wetness | no extra samples. Arithmetic on values the material already has |
| Puddles | included in the above |
| Ripples | **two samples**, and only where an instance turns them on |
| Accumulation | no extra samples |
| Trample | **three samples** of one render target, and one sampler |
| Trample geometry | **one vertex sample**, about 47 vertex instructions, nothing in the pixel shader |

Measured on the surface master: everything off is 3 taps, 4 samplers, 999 pixel instructions. Accumulation on is still 3 taps and 4 samplers, at 1025. Trample on is 6 taps, 5 samplers, 1050. The extra sampler is the clamp group, which everything clamped then shares; the wrap group the layers use is untouched.

All of these are static switches except terrain accumulation, so an instance that does not use one does not compile it. A world where only the outdoor materials have wetness on pays nothing indoors.
