# Mobile Forward-Rendering Materials <img align="right" width=128, height=128 src="https://github.com/Vaei/MobMaterials/blob/main/Resources/Icon128.png">

> [!IMPORTANT]
> Three AAA-tier master materials
> <br>Landscape, surface and foliage
> <br>Designed for the lightweight Mobile Forward Rendering pathway

**Suitable for realistic and stylized projects alike**

UE5.8+

---

> [!CAUTION]
> MobMaterials has not officially released. Expect terrible bugs, and updates to occur without versioning or changelog reflecting them. Documentation is incomplete and there are no images or videos yet either. **Come back soon!**

<!-- TODO(image): hero shot - terrain with layers interlocking, a rock blending into the ground, everything wet -->

## Documentation

**[vaei.github.io/MobMaterials](https://vaei.github.io/MobMaterials/)**

Or open [`docs/index.html`](docs/index.html) from a clone - it is a static site with no build step and no network, so it works straight off disk.

| | |
|---|---|
| [Install](https://vaei.github.io/MobMaterials/install.html) | plugin, project settings, your first master |
| [Building a world](https://vaei.github.io/MobMaterials/workflow.html) | the order of operations that avoids backtracking |
| [Techniques](https://vaei.github.io/MobMaterials/techniques.html) | triplanar, hex tiling, tiling break, parallax - when each is the right answer |
| [Stylized](https://vaei.github.io/MobMaterials/stylized.html) / [Realistic](https://vaei.github.io/MobMaterials/realistic.html) | the two art directions, with numbers |
| [Surface](https://vaei.github.io/MobMaterials/surface.html), [Landscape](https://vaei.github.io/MobMaterials/landscape.html), [Foliage](https://vaei.github.io/MobMaterials/foliage.html), [Weather](https://vaei.github.io/MobMaterials/weather.html) | every parameter |
| [If it is wrong](https://vaei.github.io/MobMaterials/troubleshooting.html) | symptom to cause |

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
* [MobLights Plugin](https://github.com/Vaei/MobLights)
  * Local lights for the mobile forward rendering path, and fog to put them in
* [MobFort Plugin](https://github.com/Vaei/MobFort)
  * Stylized unlit character masters for the same renderer. These materials are the world those characters stand in
* [MobWater Plugin](https://github.com/Vaei/MobWater)
  * Ponds, lakes, rivers and ocean on the same renderer, that characters wade and swim in

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

## Quick start

1. Enable the plugin.
2. Set the [four required project settings](https://vaei.github.io/MobMaterials/install.html#required).
3. **Mat &rarr; Generate Materials...**, make a recipe, set Kind, Output Path and Asset Name, **Generate**.
4. Child a preset instance, set `Layer0_BC` / `_NRM` / `_CRM`, put it on a mesh.

Everything else, including the landscape and foliage paths, is on the [documentation site](https://vaei.github.io/MobMaterials/).

## License

MIT. Documentation ships IBM Plex under the SIL Open Font License 1.1 - see `docs/assets/fonts/OFL.txt`.
