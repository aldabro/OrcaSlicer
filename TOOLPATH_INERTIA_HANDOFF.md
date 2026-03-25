# Toolpath Inertia / Toolpath OBJ Branch Handoff

## Branch State
- Repo: `C:\Users\aleks\Repositories\OrcaSlicer`
- Branch: `feature/toolpath-inertia-analysis`
- Branch is currently ahead of `origin/main` and pushed to `origin/feature/toolpath-inertia-analysis`
- Worktree state at the time of this handoff: intended to be clean except for this file update

Recent branch commits, newest first:
- `a802d1d752` `Refine inertia export reference frames`
- `dbef2d5a2c` `Use native preview labels for inertia export`
- `5357d0b0a6` `Improve inertia export JSON layout`
- `7fcca1f2ef` `Fix single-object inertia fallback`
- `558ce0ffed` `Fix unlabeled toolpath inertia export on Windows`
- `df47553ce4` `wip: refactor inertia analysis toward per-object JSON export`
- `2e687f6085` `feat: add prototype native toolpath inertia analysis`

## What Is In This Branch
This branch currently contains two user-facing export features:
- `Export Toolpaths as OBJ`
- `Export Toolpath Inertia Analysis`

The user does not want to split them into separate PR branches yet, but the likely future PR plan is:
- PR 1: OBJ export feature
- PR 2: inertia export feature plus the native preview label plumbing it depends on

## Files Changed On This Branch
- [TOOLPATH_INERTIA_HANDOFF.md](C:/Users/aleks/Repositories/OrcaSlicer/TOOLPATH_INERTIA_HANDOFF.md)
- [src/libslic3r/CMakeLists.txt](C:/Users/aleks/Repositories/OrcaSlicer/src/libslic3r/CMakeLists.txt)
- [src/libslic3r/GCode.cpp](C:/Users/aleks/Repositories/OrcaSlicer/src/libslic3r/GCode.cpp)
- [src/libslic3r/GCode.hpp](C:/Users/aleks/Repositories/OrcaSlicer/src/libslic3r/GCode.hpp)
- [src/libslic3r/GCode/InertiaAnalyzer.cpp](C:/Users/aleks/Repositories/OrcaSlicer/src/libslic3r/GCode/InertiaAnalyzer.cpp)
- [src/libslic3r/GCode/InertiaAnalyzer.hpp](C:/Users/aleks/Repositories/OrcaSlicer/src/libslic3r/GCode/InertiaAnalyzer.hpp)
- [src/slic3r/GUI/GUI_Preview.cpp](C:/Users/aleks/Repositories/OrcaSlicer/src/slic3r/GUI/GUI_Preview.cpp)
- [src/slic3r/GUI/GUI_Preview.hpp](C:/Users/aleks/Repositories/OrcaSlicer/src/slic3r/GUI/GUI_Preview.hpp)
- [src/slic3r/GUI/MainFrame.cpp](C:/Users/aleks/Repositories/OrcaSlicer/src/slic3r/GUI/MainFrame.cpp)
- [src/slic3r/GUI/Plater.cpp](C:/Users/aleks/Repositories/OrcaSlicer/src/slic3r/GUI/Plater.cpp)
- [src/slic3r/GUI/Plater.hpp](C:/Users/aleks/Repositories/OrcaSlicer/src/slic3r/GUI/Plater.hpp)

## Current User-Facing Status
The inertia export feature is working from the GUI on the current Windows build.

Current menu entry:
- `File -> Export -> Export Toolpath Inertia Analysis`

Current output:
- JSON file per current plate
- per-object / per-instance mass properties
- named inertia tensor entries
- timestamped export metadata

The OBJ export feature is also present on the branch, but this handoff focuses mainly on the inertia side because that received the most recent work.

## Build / Environment Notes
This branch has been actively built on Windows.

Known working binary:
- [orca-slicer.exe](C:/Users/aleks/Repositories/OrcaSlicer/build/src/Release/orca-slicer.exe)

Typical build command used on this machine:
```powershell
$env:CMAKE_POLICY_VERSION_MINIMUM='3.5'
cmake --build build --config Release --target OrcaSlicer -- /m:1
```

Common Windows gotcha:
- If build fails with `LNK1104` for `OrcaSlicer.dll`, Orca is still running. Close the app and rebuild.

## High-Level Inertia Architecture
The current implementation uses Orca's native preview data:
- `GCodeProcessorResult::moves`

This is the correct path. The feature does not reparse exported G-code text externally.

Core analyzer:
- [InertiaAnalyzer.hpp](C:/Users/aleks/Repositories/OrcaSlicer/src/libslic3r/GCode/InertiaAnalyzer.hpp)
- [InertiaAnalyzer.cpp](C:/Users/aleks/Repositories/OrcaSlicer/src/libslic3r/GCode/InertiaAnalyzer.cpp)

GUI/export entry points:
- [GUI_Preview.cpp](C:/Users/aleks/Repositories/OrcaSlicer/src/slic3r/GUI/GUI_Preview.cpp)
- [Plater.cpp](C:/Users/aleks/Repositories/OrcaSlicer/src/slic3r/GUI/Plater.cpp)
- [MainFrame.cpp](C:/Users/aleks/Repositories/OrcaSlicer/src/slic3r/GUI/MainFrame.cpp)

## Important Design Corrections Already Made

### 1. Native preview labels are now emitted and consumed
This was the main architectural fix.

Problem discovered:
- ordinary preview toolpaths did not reliably populate `MoveVertex.object_label_id`
- the earlier geometry fallback was only a temporary workaround

Current fix:
- preview G-code generation now emits native per-instance comments using the same label format already understood by `GCodeProcessor`
- see [GCode.cpp](C:/Users/aleks/Repositories/OrcaSlicer/src/libslic3r/GCode.cpp) and [GCode.hpp](C:/Users/aleks/Repositories/OrcaSlicer/src/libslic3r/GCode.hpp)

Important detail:
- the analyzer no longer depends on geometry-based object assignment
- the geometry fallback was removed again once native labels worked

### 2. Volume / mass overcounting bug was fixed
This was an important late-stage bug.

What was wrong:
- preview moves can be internally split for speed/time processing
- `travel_dist` on those split moves was not safe to use as if it were the true local segment length
- this caused material overcounting

Current fix:
- segment volume now uses the actual geometric segment length between `prev.position` and `curr.position`
- no correction or fudge factors were added
- the user explicitly asked about this and the answer should remain: no correction factors are in the implementation

### 3. JSON field order is now preserved
The analyzer now uses `nlohmann::ordered_json`.

Reason:
- plain `nlohmann::json` reordered object keys alphabetically
- the user wanted object blocks to read from general info to more complex info, with inertia last

### 4. Copyable error dialog was added
Export failure UI no longer uses an uncopyable message box.

Relevant file:
- [Plater.cpp](C:/Users/aleks/Repositories/OrcaSlicer/src/slic3r/GUI/Plater.cpp)

### 5. Source/CAD-like frame reconstruction was added
The original `origin_to_com_offset_mm` was misleading because Orca internally recenters models.

Current JSON now exposes:
- `center_of_mass_in_source_frame_mm`
- `origin_to_com_offset_mm` mapped to the reconstructed source/CAD-like frame
- `center_of_mass_in_slicer_object_frame_mm`
- inertia about:
  - source origin
  - slicer object origin
  - center of mass

This reconstruction currently uses:
- object centering info
- print centering info
- first model-part `source.mesh_offset`

## Current JSON Shape
At the top level:
- `schema_version`
- `exported_at_utc`
- `generator`
- `analysis_type`
- `plate_index`
- `ok`
- `reference_frames`
- `inertia_tensor_convention`
- `objects`

Each object block currently includes, in this order:
- `object_name`
- `ok`
- `instance_id`
- `model_object_id`
- `object_label_id`
- `plate_index`
- `mass_kg`
- `volume_mm3`
- `used_density_from_extruders`
- `center_of_mass_in_source_frame_mm`
- `origin_to_com_offset_mm`
- `center_of_mass_in_slicer_object_frame_mm`
- `materials`
- `inertia_tensors_kg_mm2`

Inside `inertia_tensors_kg_mm2`:
- `about_source_origin`
- `about_object_origin`
- `about_center_of_mass`

Named inertia components:
- `I_xx`
- `I_yy`
- `I_zz`
- `I_xy`
- `I_xz`
- `I_yz`
- `matrix_rows`

## What Has Been Verified With The User

### Verified working
- feature builds on Windows
- menu wiring works
- export produces JSON successfully
- missing-label problem was solved by native preview labels
- geometry fallback was removed
- JSON ordering now matches what the user wanted
- mass/volume on the 35 mm cube are now plausible without correction factors
- reconstructed source frame now gives approximately the expected CAD Z-origin offset for the cube
- rotated cube instances still produce sensible source-frame COM values
- `instance_id` was adjusted to align better with GUI numbering by preferring `arrange_order`

### Verified examples
User manually checked:
- single cube
- multiple different objects
- three cubes
- rotated instances

## Known Remaining Caveats

### 1. Source/CAD-like frame is reconstructed, not guaranteed mathematically exact for every model topology
It is much better than the earlier misleading "object origin" wording, but it is still a reconstruction.

It has been sanity-checked on cubes and rotated cube instances.

It has not yet been deeply validated on:
- complex multi-volume models
- assemblies with unusual source transforms
- nontrivial imported coordinate conventions

### 2. Rotated-instance validation is still only partly complete
Cube tests were good, but cubes are symmetric.

A better future validation target is a clearly asymmetric model, for example:
- Stanford bunny
- off-center asymmetric mechanical part

### 3. `instance_id` is now user-friendlier, but still deserves one final sanity pass
The current export uses:
- `ModelInstance.arrange_order` if present
- otherwise `PrintInstance.id + 1`

This is intended to align better with what the GUI shows.

It worked better in the final local user check, but another quick spot-check on a few plate arrangements would still be wise before PR.

### 4. Reference-frame wording may still be polishable
Current strings are much better than before, but if someone wants to polish naming before PR, that is low-risk work.

## Recommended Next Steps

### Highest-value validation tasks
1. Validate source-frame reconstruction on a clearly asymmetric rotated part
2. Sanity-check `instance_id` on a few more plate layouts
3. Compare `about_source_origin` against CAD on one or two more controlled models
4. Confirm multi-material density handling with a real multi-material test case

### Potential cleanup / polish
1. Revisit wording in `reference_frames`
2. Decide whether `about_object_origin` should be renamed to `about_slicer_object_origin` in JSON for clarity
3. Decide whether to add explicit `mass_g` as a convenience field
4. Consider whether to expose both raw/internal and user-facing IDs if needed

### Likely later PR split
When the user is ready:
1. split `Export Toolpaths as OBJ` into its own PR branch
2. keep inertia export plus native preview label plumbing in a second PR branch

## Important Non-Goals / Things Not To Reintroduce
- Do not reintroduce geometry-based object assignment as the primary solution
- Do not add hidden correction factors to make cube results match CAD
- Do not revert back to plain `nlohmann::json` if preserving field order matters

## Recommended Starting Point For The Next Agent
1. Read this handoff
2. Read [InertiaAnalyzer.cpp](C:/Users/aleks/Repositories/OrcaSlicer/src/libslic3r/GCode/InertiaAnalyzer.cpp)
3. Read [GCode.cpp](C:/Users/aleks/Repositories/OrcaSlicer/src/libslic3r/GCode.cpp) around the preview label emission
4. Re-export one asymmetric rotated test part before changing math again
5. Keep the current no-correction-factor principle intact
