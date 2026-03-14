# Toolpath Inertia Analysis — Handoff Notes

## Branch / Repo State
- Repo: `git@github.com:aldabro/OrcaSlicer.git`
- Upstream: `https://github.com/SoftFever/OrcaSlicer`
- Branch: `feature/toolpath-inertia-analysis`
- Last stable checkpoint before this round: `2e687f60 feat: add prototype native toolpath inertia analysis`

## What was achieved

### 1. Native OrcaSlicer prototype implemented and proven to build
A native C++ inertia-analysis prototype was added and compiled successfully into OrcaSlicer.

Added files:
- `src/libslic3r/GCode/InertiaAnalyzer.hpp`
- `src/libslic3r/GCode/InertiaAnalyzer.cpp`

Build integration:
- `src/libslic3r/CMakeLists.txt`

Initial GUI wiring:
- `src/slic3r/GUI/GUI_Preview.*`
- `src/slic3r/GUI/Plater.*`
- `src/slic3r/GUI/MainFrame.cpp`

The app built successfully and the first prototype feature was manually confirmed by the user to launch and work from the menu.

### 2. Correct internal architecture path identified
The important design discovery is:
- **Do not reparse exported G-code text if possible.**
- Use Orca's native `GCodeProcessorResult::moves` instead.

Relevant native data already available in Orca preview moves:
- move type
- extrusion role
- position
- width / height
- `mm3_per_mm`
- `travel_dist`
- extruder id
- `object_label_id`

This makes native analysis feasible.

### 3. Current direction changed from popup report to export-oriented JSON workflow
The feature was refactored toward:
- menu name: **Export Toolpath Inertia Analysis**
- JSON export instead of just popup text
- per-object/per-instance groundwork
- intent to report data in object coordinates and CoM coordinates

### 4. Current implementation direction for v2
Work in progress already added for:
- grouping by `object_label_id`
- mapping toolpath object labels back to `PrintInstance` / `ModelInstance`
- transforming preview/toolpath coordinates back into the object's own frame using inverse model-instance transform
- computing per-object:
  - mass
  - volume
  - CoM offset in object frame
  - inertia about object origin
  - inertia about CoM
  - material usage per extruder / density

## Current user-facing status
- Orca builds successfully with the modified code.
- The menu item exists as export-style action.
- Export currently fails at runtime with:
  - `no per-object part extrusions could be analyzed from the preview toolpaths`

So the code compiles, but the new per-object export path is **not yet functionally correct**.

## Most likely current blocker
The likely failure is in **label mapping**, not file export and not build system.

Current assumption in code:
- `GCodeProcessorResult::MoveVertex.object_label_id`
  maps directly to
- `PrintInstance.model_instance->get_labeled_id()`

This assumption appears too strict or wrong for the real runtime path.

Possible causes:
1. preview move labels are missing or different than expected
2. labels exist but do not match current plate's `PrintInstance` labels
3. current preview result and current plate print object universe are not aligned the way assumed
4. transform path is too aggressive and filters all usable segments out after matching

## Files currently modified in working tree
Uncommitted changes exist in:
- `src/libslic3r/GCode/InertiaAnalyzer.cpp`
- `src/libslic3r/GCode/InertiaAnalyzer.hpp`
- `src/slic3r/GUI/GUI_Preview.cpp`
- `src/slic3r/GUI/GUI_Preview.hpp`
- `src/slic3r/GUI/MainFrame.cpp`
- `src/slic3r/GUI/Plater.cpp`
- `src/slic3r/GUI/Plater.hpp`

These changes include:
- export-oriented menu naming
- JSON export path
- improved error reporting in export flow
- extra debug logging around object-label mapping

## What still needs to be done

### A. Finish runtime debugging of object mapping
Most urgent task.

Recommended next steps:
1. Stop relying on terminal logs only.
2. Surface detailed diagnostic info directly in the popup/export error message, e.g.:
   - distinct preview move `object_label_id`s seen
   - distinct current-plate instance labels seen
   - count of labeled extrusion moves
   - count of matched moves
3. Verify whether label spaces actually match.
4. If not, find the correct mapping source.

Likely places to inspect:
- `GCodeProcessorResult::moves`
- `PrintInstance.model_instance->get_labeled_id()`
- current plate print object access through `PartPlateList::get_current_fff_print()`
- any Orca code that relates preview labels back to instances, especially existing object-cancel / label-object paths

### B. Validate coordinate-frame semantics
Once per-object matching works, verify that object-frame numbers are actually correct.

Need to confirm:
- object origin is really the intended design/model origin
- plate origin is removed correctly
- instance transform inversion is correct
- user plate rotation for printability does not poison exported object-frame inertia

### C. Confirm multi-material handling
Current implementation uses `gcode_result.filament_densities[extruder_id]` per segment.
This is the right direction, but still needs runtime validation with a real multi-material print.

### D. Improve export schema
Current JSON direction is reasonable, but should be cleaned and stabilized.
Suggested target fields:
- schema version
- plate index
- object name
- instance id
- model object id
- mass
- volume
- CoM in object frame
- origin-to-CoM offset
- inertia about object origin
- inertia about CoM
- material/extruder breakdown
- possibly debug plate-frame values for sanity checking

### E. Consider adding a URDF-friendly helper later
Not necessary yet, but eventually useful:
- optional URDF-ish snippet or clearly URDF-compatible naming

## Build / environment notes
- Orca dependency bootstrap completed successfully on this machine.
- Full Orca build works with `-j1` reliably on the current RAM budget.
- Higher parallelism previously triggered OOM / `cc1plus` killed.
- Typical successful build command on this box:
  - `./build_linux.sh -rs -1 -p`

## Known good launch path
Latest built binary was launched via:
- `/home/walc/.openclaw/workspace/OrcaSlicer/build/src/Release/orca-slicer`

## Recommendation to next agent
1. Commit current WIP state first.
2. Keep current branch.
3. Focus entirely on **per-object label mapping diagnostics** before touching more math.
4. Only after mapping is proven, validate transforms and exported tensors.
5. Avoid major UI polish until object/frame correctness is trustworthy.
