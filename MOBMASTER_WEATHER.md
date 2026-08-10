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
> Ripples do nothing without puddles. If `PuddleDepth` is low or the surface is not facing up, there is no puddle mask to gate them and nothing will move. Check the puddle mask first - **debug view 4** shows the wetness mask.

## Snow

`bAccumulation`. A covering that settles by which way a surface faces.

| | |
|---|---|
| `Accumulation_Amount` | how much has fallen. This is the one to drive |
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
4. Drive `Accumulation_Amount` 0 to 1 and watch it fill from the cracks outward, then cover the flats.

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

Terrain has no accumulation layer. Snow on the ground is better done as a paint layer with a slope mask, because on terrain you usually want to control where it drifts rather than have it settle uniformly.

## What it costs

| | |
|---|---|
| Wetness | no extra samples. Arithmetic on values the material already has |
| Puddles | included in the above |
| Ripples | **two samples**, and only where an instance turns them on |
| Accumulation | no extra samples |

All three are static switches, so an instance that does not use them does not compile them. A world where only the outdoor materials have wetness on pays nothing indoors.
