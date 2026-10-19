# Jurassic Stumble Project Structure

## Content organization

Game-owned assets belong under `Content/JurassicStumble/`:

```text
JurassicStumble/
├── Blueprints/
│   ├── Player/
│   ├── GameModes/
│   ├── Obstacles/
│   └── Checkpoints/
├── Characters/Dinosaur/
├── Animations/Player/
├── Input/
│   ├── Actions/
│   └── MappingContexts/
├── Maps/
├── Materials/
├── Physics/
├── UI/
└── Audio/
```

Imported vendor assets belong under `Content/ThirdParty/KayKit/`, separated into `Characters/`, `Platformer/`, and `Resources/`. Keep imported names and internal layout unless a change is required for Unreal compatibility. Never edit the original Unity source packs.

The KayKit content currently occupies:

```text
Content/ThirdParty/KayKit/
├── Characters/
│   ├── Monster/                 # SK_Monster, SKEL_Monster, M_Monster, T_Monster_Atlas
│   └── Rig_Medium/Animations/   # 132 A_ animation sequences in eight category folders
├── Platformer/
│   ├── Meshes/                  # 525 SM_ static meshes
│   ├── Materials/               # M_Platformer, M_Platformer_Threads
│   └── Textures/                # T_Platformer_Atlas, T_Platformer_Threads
└── Resources/
    ├── Meshes/                  # 132 SM_ static meshes
    ├── Materials/               # M_ResourceBits
    └── Textures/                # T_ResourceBits_Atlas
```

`Monster` is the intended playable character source. Its existing humanoid `Rig_Medium` skeleton is intentional. KayKit source animation sequences remain in the vendor folder and all reference the existing `SKEL_Monster`; future game-owned Animation Blueprints, Blend Spaces, Montages, and gameplay logic belong under `Content/JurassicStumble/Animations/Player`. The environment source-to-asset mapping is maintained in `Documentation/KayKitAssetManifest.csv`, and the animation import is documented in `Documentation/AnimationImport.md`.

KayKit conversion work must use Unreal's supported FBX/texture import pipeline. Extracted source files and Unity metadata are working inputs only; do not place `.unitypackage`, `.prefab`, `.mat`, `.meta`, or Unity `.anim` files in the Unreal Content tree. Shared Unreal materials and textures should live once within the relevant pack folder and be reused by its meshes.

Empty folders may not appear in the Content Browser and are not retained by Git. Create asset subfolders when their first asset is needed; do not add placeholder assets solely to preserve folders.

## Folder purposes

| Folder | Purpose |
| --- | --- |
| `Blueprints/Player` | Player gameplay Blueprints |
| `Blueprints/GameModes` | Game mode and rules Blueprints |
| `Blueprints/Obstacles` | Reusable course obstacle Blueprints |
| `Blueprints/Checkpoints` | Checkpoint and respawn Blueprints |
| `Characters/Dinosaur` | Game-owned dinosaur character assets |
| `Animations/Player` | Player animation assets and animation Blueprints |
| `Input/Actions` | Enhanced Input actions |
| `Input/MappingContexts` | Enhanced Input mapping contexts |
| `Maps` | Jurassic Stumble levels and test maps |
| `Materials` | Game-owned materials and material instances |
| `Physics` | Physics assets and related configuration assets |
| `UI` | Widget Blueprints and UI assets |
| `Audio` | Game-owned sound assets |
| `ThirdParty/KayKit` | Imported KayKit content, isolated from game-owned assets |

## Naming conventions

| Asset type | Prefix | Example |
| --- | --- | --- |
| Player Blueprint | `BP_` | `BP_Player` |
| Game Mode | `GM_` | `GM_GameMode` |
| Obstacle | `BP_` | `BP_Obstacle_RotatingDisc` |
| Checkpoint | `BP_` | `BP_Checkpoint` |
| Input Action | `IA_` | `IA_Jump` |
| Input Mapping Context | `IMC_` | `IMC_Player` |
| Animation Blueprint | `ABP_` | `ABP_Player` |
| Animation Sequence | `A_` | `A_Walking_A` |
| Skeletal Mesh | `SK_` | `SK_Dinosaur` |
| Skeleton | `SKEL_` | `SKEL_Monster` |
| Static Mesh | `SM_` | `SM_Platform` |
| Material | `M_` | `M_Dinosaur` |
| Material Instance | `MI_` | `MI_Dinosaur` |
| Texture | `T_` | `T_Dinosaur_BaseColor` |
| Physics Asset | `PHYS_` | `PHYS_Dinosaur` |
| Widget Blueprint | `WBP_` | `WBP_MainMenu` |

Use descriptive PascalCase names after the prefix. Task 03D applied final Unreal-safe names before importing the KayKit environment assets, preserving useful source identifiers and avoiding redirectors and original-name duplicates.

For future KayKit imports, apply the same prefixes at import time: `SM_`, `SK_`, `SKEL_`, `M_`, `MI_`, `T_`, `PHYS_`, `A_`, `ABP_`, `BP_`, `GM_`, `IA_`, `IMC_`, and `WBP_`. Retain useful source identifiers such as dimensions, variants, and colours (`SM_Platform_4x4x1_Blue`). Avoid ambiguous abbreviations, duplicate names, and post-import renames where the final name can be chosen safely during import.

## Guidelines for future assets

- Keep gameplay implementations simple, modular, and independently testable.
- Prefer Blueprints and Details-panel configuration for gameplay work.
- Add systems, interfaces, managers, components, and data assets only when a current requirement needs them.
- Keep game-owned assets separate from imported vendor content.
- Move or rename Unreal assets only through the Content Browser or supported Editor automation so references are preserved.
- Fix up redirectors after approved asset moves and verify references before deleting redirectors.
- Record the actual Blueprint nodes, components, settings, and input mappings used in future implementation tasks.

## Prototype map

The current gameplay test level is `/Game/JurassicStumble/Maps/Level1`. It contains a Player Start and passes Unreal Map Check. The project-wide default Game Mode is `/Game/JurassicStumble/Blueprints/GameModes/GM_GameMode`, whose default pawn is `BP_Player`.

Task 04 added these game-owned assets:

```text
Content/JurassicStumble/
├── Blueprints/
│   ├── Player/BP_Player
│   └── GameModes/GM_GameMode
└── Input/
    ├── Actions/
    │   ├── IA_Move
    │   ├── IA_Look
    │   └── IA_Jump
    └── MappingContexts/IMC_Player
```

`BP_Player` uses the vendor-owned `/Game/ThirdParty/KayKit/Characters/Monster/SK_Monster` skeletal mesh. Character setup and input wiring are documented in `Documentation/PlayerSetup.md`.

