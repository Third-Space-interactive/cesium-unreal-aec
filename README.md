# Cesium for Unreal — AEC point-cloud fork

> A fork of [Cesium for Unreal](https://github.com/CesiumGS/cesium-unreal) with runtime point-cloud collision, surface-accurate picking, and GPU compute-raster rendering — built for AEC / heritage digital-twin scenes running hundreds of streaming point-cloud tilesets at once. Everything below the divider is the upstream Cesium for Unreal README, unchanged.

## What this fork adds

All additions are additive — no existing Cesium source is modified — and land in the `CesiumRuntime` module. The work is organized into three milestones (see `git log`):

- **Phase 1 — Point-cloud collision proxies.** Whole-tileset runtime collision for point-cloud 3D Tilesets: one `QueryOnly` `UBoxComponent` per tileset (`UCesiumPointCloudCollisionProxy`) driven by a lifecycle manager, plus a Blueprint utility library for line-trace queries.
- **Phase 2 — Surface-accurate point-cloud picking.** Two-tier hit-testing on top of Phase 1: a cursor ray returns a precise point on the real cloud surface with a PCA-estimated normal, correct depth ordering, and whole-tileset identity — robust to outliers, riding the LOD stream, cheap at ~900 simultaneous tilesets. Tier 1 is a percentile-trimmed / MAD-rejection broadphase box; Tier 2 is a lazy per-tile voxel index with 3D-DDA ray-march. Blueprint API: `QueryPointCloudAlongRay` / `QueryPointCloudUnderCursor` → `FCesiumPointCloudHit`.
- **Phase 3 — GPU compute-raster point rendering.** Renders point clouds through a custom RDG compute-raster pass instead of the main geometry pass — a render-thread point-proxy registry feeds compute shaders that rasterize into a lit GBuffer (depth + octahedral normal + color), gated behind a CVar and a GPU capability probe.

## Setup

This is an Unreal **plugin** with **git submodules** (`cesium-native` and friends), not a standalone app — a plain `git clone` leaves the submodules empty and it won't build.

```sh
# Clone with submodules (checks out `main` by default):
git clone --recurse-submodules https://github.com/Third-Space-interactive/cesium-unreal-aec.git

# Already did a plain clone? Fill in the submodules without re-cloning:
cd cesium-unreal-aec
git submodule update --init --recursive
```

Then:

1. **Place it in a UE project's `Plugins/` folder.** Clone directly into `<YourProject>/Plugins/CesiumForUnreal`, or clone elsewhere and add a directory junction/symlink into `Plugins/`.
2. **Build `cesium-native` once per machine.** The compiled cesium-native headers/libs live under `Source/ThirdParty/` and are **git-ignored** — they are generated, never committed — so every developer builds them locally from the `extern/cesium-native` submodule. Requires **CMake 3.15+** and **Visual Studio 2022** (C++ toolchain); optionally [nasm](https://www.nasm.us/) for faster JPEG decoding. From an *"x64 Native Tools Command Prompt for VS 2022"*:

   ```bat
   cd <your-clone>\extern
   cmake -B build -S . -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release --target install
   ```

   The `install` target writes headers/libs into `Source/ThirdParty/…`. Expect **20–40+ min** the first time (it also builds the vcpkg dependencies). Add `--config Debug` for a debug build too if you'll debug in-editor. See the [Developer Setup Guide](Documentation/developer-setup.md) for macOS/Linux/Android and CMake-GUI variants.
3. **Then build the UE project.** Regenerate the Visual Studio project files from the `.uproject` and build — the cesium-native includes will now resolve.
4. **`EngineVersion` is pinned to `5.5.0`** in `CesiumForUnreal.uplugin` for broad compatibility. Running the headless automation suite needs it bumped to your editor's version locally (otherwise `-unattended` auto-declines the incompatible-plugin dialog and the module never loads) — keep that a local, uncommitted edit.

---

[![Cesium for Unreal Logo](Content/Cesium-for-Unreal-Logo-WhiteBGH.jpg)](https://cesium.com/unreal-marketplace?utm_source=cesium-unreal&utm_medium=github&utm_campaign=unreal)

Cesium for Unreal brings the 3D geospatial ecosystem to Unreal Engine. By combining a high-accuracy full-scale WGS84 globe, open APIs and open standards for spatial indexing such as 3D Tiles, and cloud-based real-world content from [Cesium ion](https://cesium.com/cesium-ion) with Unreal Engine, this project enables a new era of 3D geospatial software.

[Cesium for Unreal Homepage](https://cesium.com/cesium-for-unreal?utm_source=github&utm_medium=github&utm_campaign=unreal)

### 🚀 Get Started

**[Download Cesium for Unreal from Unreal Engine Marketplace](https://cesium.com/unreal-marketplace?utm_source=cesium-unreal&utm_medium=github&utm_campaign=unreal)**

**[Follow the Quickstart](https://cesium.com/docs/tutorials/cesium-unreal-quickstart/)**

Have questions? Ask them on the [community forum](https://community.cesium.com).

### 👏 Featured Demos

<p>
<a href="https://github.com/CesiumGS/cesium-unreal-samples"><img src="https://images.prismic.io/cesium/bfa9f768-26eb-4a6f-a427-8e9cecbe16b1_melbourne.jpg" width="48%" /></a>&nbsp;
<a href="https://cesium.com/blog/2020/11/30/project-anywhere/"><img src="https://images.prismic.io/cesium/2020-11-30-Project-Anywhere-3.jpg" width="48%" /></a>&nbsp;
<br/>
<br/>
</p>

### 🏡 Cesium for Unreal and the 3D Geospatial Ecosystem

Cesium for Unreal streams real-world 3D content such as high-resolution photogrammetry, terrain, imagery, and 3D buildings from [Cesium ion](https://cesium.com/cesium-ion) and other sources, available as optional commercial subscriptions. The plugin includes Cesium ion integration for instant access to global high-resolution 3D content ready for runtime streaming. Cesium ion users can also leverage cloud-based 3D tiling pipelines to create end-to-end workflows to transform massive heterogeneous content into semantically-rich 3D Tiles, ready for streaming to Unreal Engine.

Cesium for Unreal supports cloud and private network content and services based on open standards and APIs. You are free to use any combination of supported content sources, standards, APIs with Cesium for Unreal.

[![Cesium for Unreal Ecosystem Architecture](https://prismic-io.s3.amazonaws.com/cesium/b1505fbc-5769-4032-9233-364a4f52acf6_unreal-pipeline-ice-blue-background.png)](https://cesium.com/cesium-for-unreal?utm_source=cesium-unreal&utm_medium=github&utm_campaign=unreal)

Using Cesium ion helps support Cesium for Unreal development. ❤️

### ⛓️ Unreal Engine Integration

Cesium for Unreal is tightly integrated with Unreal Engine making it possible to visualize and interact with real-world content in editor and at runtime. The plugin also has support for Unreal Engine physics, collisions, character interaction, and landscaping tools. Leverage decades worth of cutting-edge advancements in Unreal Engine and geospatial to create cohesive, interactive, and realistic simulations and applications with Cesium for Unreal.

### 📗 License

[Apache 2.0](http://www.apache.org/licenses/LICENSE-2.0.html). Cesium for Unreal is free for both commercial and non-commercial use.

### 📦 Installing Cesium for Unreal

The easiest way to install Cesium for Unreal is by downloading the officially released version from the [Unreal Engine Marketplace](https://cesium.com/unreal-marketplace?utm_source=cesium-unreal&utm_medium=github&utm_campaign=unreal).

You can also find all releases on the [Releases](https://github.com/CesiumGS/cesium-unreal/releases) page. This is useful if you want an older version, or if you can't or don't want to use the Unreal Engine Marketplace. In particular, if you're using Linux, the Releases page is a better option. To install any of these releases:

1. If you previously installed the Cesium for Unreal plugin via the Unreal Engine Marketplace, uninstall it.
2. Extract the release ZIP to Unreal Engine's `Engine/Plugins/Marketplace` directory. For example, on Unreal Engine 5.3 on Windows, this is typically `C:\Program Files\Epic Games\UE_5.3\Engine\Plugins\Marketplace`. You may need to create the `Marketplace` directory yourself.
3. If you've done this correctly, you'll find a `CesiumForUnreal` sub-directory inside the `Marketplace` directory, and the plugin is ready to use.

You can also [use pre-release packages](Documentation/Pages/using-prerelease-packages.md).

### 💻 Developing with Unreal Engine

See the [Developer Setup Guide](Documentation/developer-setup.md) to learn how to set up a development environment for Cesium for Unreal, allowing you to compile it, customize it, and contribute to its development.

### 📜 Older versions of Unreal Engine

Cesium for Unreal's standard policy is to support the three most recent versions of Unreal Engine. Older versions of the plugin may be used with older versions of Unreal Engine, but we recommend staying up-to-date if at all possible.

The code for the last version of the plugin that supported Unreal Engine 4 can be found in the [ue4-main](https://github.com/CesiumGS/cesium-unreal/tree/ue4-main) branch.

### Documentation

<!--! \cond DOXYGEN_EXCLUDE !-->
Please see the [User and contributor documentation](https://cesium.com/learn/cesium-unreal/ref-doc/).
<!--! \endcond -->

<!--!
* \subpage changes
* \subpage user-guide
* \subpage contributor-guide
-->
