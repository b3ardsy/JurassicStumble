# Player Setup

## Assets

- Player Character: `/Game/JurassicStumble/Blueprints/Player/BP_Player`
- Game Mode: `/Game/JurassicStumble/Blueprints/GameModes/GM_GameMode`
- Skeletal Mesh: `/Game/ThirdParty/KayKit/Characters/Monster/SK_Monster`
- Input Actions: `IA_Move` (`Axis2D`), `IA_Look` (`Axis2D`), and `IA_Jump` (`Boolean`)
- Mapping Context: `/Game/JurassicStumble/Input/MappingContexts/IMC_Player`

## BP_Player components

```text
CapsuleComponent
├── Mesh (SK_Monster)
└── SpringArm
    └── Camera
```

- Capsule radius: `70 cm`
- Capsule half height: `125 cm`
- Mesh relative location: `(0, 0, -125)`
- Mesh relative rotation: `(0, -90, 0)`
- Mesh relative scale: `(1, 1, 1)`
- Spring Arm target length: `350 cm`
- Spring Arm uses Pawn Control Rotation and inherits controller pitch and yaw, but not roll
- Spring Arm collision testing remains enabled
- Position lag is enabled at speed `12`; rotation lag is enabled at speed `10`
- Camera is attached to the Spring Arm `SpringEndpoint` socket
- Camera relative location and rotation are both zero; relative scale is `(1, 1, 1)`
- Camera does not independently use Pawn Control Rotation

The location aligns the imported mesh base, which is near local Z `0`, with the bottom of the capsule. The `-90°` yaw is the manually verified visual-facing correction and must be preserved.

## Character movement

- Orient Rotation to Movement: enabled
- Use Controller Desired Rotation: disabled
- Use Controller Rotation Yaw: disabled
- Use Controller Rotation Pitch: disabled
- Use Controller Rotation Roll: disabled
- Rotation Rate Yaw: `540 degrees/second`
- Max Walk Speed: `500 cm/second`
- Jump Z Velocity: `500 cm/second`
- Gravity Scale: `1.0`
- Air Control: `0.35`

Movement is camera-relative on the horizontal plane. The Movement section gets the Controller's Control Rotation, breaks the Rotator, constructs a new Rotator from Yaw only with zero Pitch/Roll, then derives Forward and Right vectors from that yaw-only rotation. `IA_Move` Y scales the forward `Add Movement Input`; X scales the right input. The execution chain remains `IA_Move Triggered` → forward input → right input.

All pawn controller-rotation flags remain disabled, so mouse-only movement never rotates the Monster. Character Movement's built-in Orient Rotation to Movement now turns the character toward its actual acceleration at `540 degrees/second`; Use Controller Desired Rotation remains disabled. This provides smooth cardinal and diagonal facing without manual rotation, interpolation, or Tick logic.

On Begin Play, `BP_Player` runs `Get Controller` → `Cast To PlayerController` → `Get Enhanced Input Local Player Subsystem` → `Add Mapping Context`, adding `IMC_Player` at priority `0`.

The Look section runs `IA_Look Triggered` → `Add Controller Pitch Input` → `Add Controller Yaw Input`; Y supplies pitch only and X supplies yaw only. One Float Multiply per axis sits between `Break Vector2D` and the controller-input nodes. Task 04G sets yaw sensitivity to `0.35` and pitch sensitivity to `0.25`; `IMC_Player` applies no additional Look scaling. The Jumping section connects Started to `Jump` and Completed to the single `Stop Jumping` node.

The Event Graph is organized into five labeled comment sections: **Input Initialization**, **Camera Pitch Limits**, **Movement**, **Look**, and **Jumping**.

## IMC_Player mappings

| Input | Action | Modifier |
| --- | --- | --- |
| W | `IA_Move` | Swizzle to positive Y |
| S | `IA_Move` | Swizzle to Y, then negate |
| D | `IA_Move` | Positive X |
| A | `IA_Move` | Negate X |
| Mouse XY | `IA_Look` | None |
| Space Bar | `IA_Jump` | None |

Task 04B corrected the saved mapping modifiers. The original mapping objects had no serialized modifiers, so every digital movement key supplied the same default positive X value even though the Event Graph wiring was correct. The persisted movement contributions are now:

- W: `Swizzle Input Axis Values (YXZ)` → positive Y
- S: `Swizzle Input Axis Values (YXZ)` followed by `Negate` → negative Y
- D: no modifier → positive X
- A: `Negate` → negative X

Because opposite keys produce equal and opposite values on the same axis, W+S and A+D cancel through Enhanced Input.

## Camera behaviour

The Spring Arm follows controller pitch/yaw while the pawn ignores controller rotation. Mouse movement therefore orbits the camera independently without rotating the Monster. Movement uses controller yaw only, so camera pitch never tilts the movement plane.

Rotation lag provides modest smoothing without snapping the camera behind the character every frame. No automatic recentering logic was added. Forced recentering would continually change the same controller yaw used as the movement basis and can fight mouse input or create feedback; add an optional delayed recenter feature only if later playtesting shows it is necessary.

On BeginPlay, after adding `IMC_Player`, `BP_Player` gets the existing standard PlayerCameraManager for player index `0` and sets View Pitch Min to `-35°` and View Pitch Max to `25°`. This uses Unreal's built-in pitch limiting; no custom PlayerController, custom PlayerCameraManager, or Tick clamp is required.

Task 04G diagnosed the unnatural orbit as a camera-component transform issue rather than input-axis mixing. Before correction, the Camera was parented to the Spring Arm but had no attachment socket, relative location approximately `(-181.9, 0, 289.7)`, and relative pitch `-20°`. This offset compounded boom rotation and could make horizontal orbit appear to gain or lose height. The existing Camera is now snapped to the Spring Arm's `SpringEndpoint` socket with identity relative transform, allowing the arm length and collision test to control framing normally.

## Game Mode and level

`GM_GameMode` derives from `GameModeBase` and sets `BP_Player` as its Default Pawn Class. `DefaultEngine.ini` also sets it as the project-wide `GlobalDefaultGameMode`, and `/Game/JurassicStumble/Maps/Level1` explicitly selects it in World Settings.

## Validation

An Unreal Engine 5.8 commandlet validation completed successfully on 2026-10-19:

- Loaded all six new assets.
- Compiled `BP_Player` and `GM_GameMode` with no Blueprint compile errors.
- Verified the player Event Graph and required input event bindings at that task stage; Task 04D later replaced it with the current 18-node classroom-aligned graph.
- Verified component hierarchy and configured values.
- Verified all six Enhanced Input key mappings and their movement modifiers.
- Loaded `Level1`, found exactly one Player Start, and completed Map Check with zero errors and zero warnings.

The commandlet used NullRHI. It did not perform PIE or render the character. Current Task 04D manual checks are listed below.

Task 04D revalidated the saved assets in a new Unreal process. It confirmed that only `IMC_Player` remains, all six mappings and modifiers persist, the 18-node actor-relative graph and four comment sections exist, both Blueprints compile, the mesh transform is preserved, `Level1` explicitly uses `GM_GameMode`, one Player Start exists, no `BP_Player` is placed in the map, and Map Check reports zero errors and warnings.

The commandlet used NullRHI and did not perform PIE. The current Task 04E manual checks are listed below.

Task 04E revalidated the current 23-node, five-comment graph in a fresh Unreal process. All three pawn controller-rotation flags are disabled, the Spring Arm remains controller-driven, pitch limits are `-60°/+45°`, Look sensitivity is applied once at `0.5` per axis, movement and jumping are unchanged, both Blueprints compile, and Map Check remains clean. PIE is still required to confirm the subjective orbit feel, conventional up/down direction, pitch-stop behaviour, and absence of character rotation during mouse movement.

Task 04F revalidated the current 27-node graph in a fresh Unreal process. The Movement section contains controller Control Rotation → yaw-only Rotator → Forward/Right vectors; no Actor Forward/Right nodes remain. Orient Rotation to Movement is enabled, Controller Desired Rotation and all pawn controller-rotation flags are disabled, and yaw rotation rate is `540`. Task 04E camera limits/sensitivity, jumping, mappings, mesh transform, Game Mode, and clean Map Check remain unchanged. PIE is required to validate runtime cardinal/diagonal facing and subjective turn smoothness.

Task 04G revalidated the camera and Look configuration in a fresh Unreal process. `IA_Look` remains Axis2D with one unmodified Mouse2D mapping and one Look event; X reaches only yaw at `0.35`, Y reaches only pitch at `0.25`, and no axis reaches roll. The Camera persists on `SpringEndpoint` with identity relative transform, pitch limits persist at `-35°/+25°`, and all Task 04F movement/rotation and Jump settings remain intact. PIE is required to confirm the apparent vertical drift is resolved and evaluate the refined orbit feel.
