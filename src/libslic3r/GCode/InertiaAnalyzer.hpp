#ifndef slic3r_GCode_InertiaAnalyzer_hpp_
#define slic3r_GCode_InertiaAnalyzer_hpp_

#include "GCodeProcessor.hpp"

#include <array>
#include <string>
#include <vector>

namespace Slic3r {

class Print;

struct GCodeInertiaOptions
{
    bool exclude_non_part_features{ true };
    bool exclude_wipe_tower{ true };
    bool exclude_custom{ true };
};

struct GCodeInertiaResult
{
    bool ok{ false };
    std::string error_message;

    size_t total_moves{ 0 };
    size_t extrusion_moves_total{ 0 };
    size_t extrusion_moves_used{ 0 };
    double volume_mm3{ 0.0 };
    double mass_kg{ 0.0 };
    std::array<double, 3> center_of_mass_mm{ 0.0, 0.0, 0.0 };
    std::array<std::array<double, 3>, 3> inertia_tensor_kg_mm2{{ {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0} }};
    std::vector<unsigned int> extruders_used;
};

struct GCodeInertiaMaterialUsage
{
    unsigned int extruder_id{ 0 };
    double density_g_cm3{ 0.0 };
    double mass_kg{ 0.0 };
    double volume_mm3{ 0.0 };
};

struct GCodeInertiaObjectResult
{
    bool ok{ false };
    int object_label_id{ -1 };
    size_t plate_index{ 0 };
    size_t instance_id{ 0 };
    size_t model_object_id{ 0 };
    std::string object_name;
    std::string error_message;

    size_t extrusion_moves_total{ 0 };
    size_t extrusion_moves_used{ 0 };
    double volume_mm3{ 0.0 };
    double mass_kg{ 0.0 };
    std::array<double, 3> center_of_mass_in_slicer_object_frame_mm{ 0.0, 0.0, 0.0 };
    std::array<double, 3> center_of_mass_in_source_frame_mm{ 0.0, 0.0, 0.0 };
    std::array<std::array<double, 3>, 3> inertia_about_object_origin_kg_mm2{{ {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0} }};
    std::array<std::array<double, 3>, 3> inertia_about_source_origin_kg_mm2{{ {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0} }};
    std::array<std::array<double, 3>, 3> inertia_about_com_kg_mm2{{ {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0} }};
    std::vector<GCodeInertiaMaterialUsage> materials;
};

struct GCodeInertiaPlateResult
{
    bool ok{ false };
    size_t plate_index{ 0 };
    std::string error_message;
    std::vector<GCodeInertiaObjectResult> objects;
};

GCodeInertiaResult analyze_gcode_inertia(const GCodeProcessorResult& gcode_result, const GCodeInertiaOptions& options = {});
GCodeInertiaPlateResult analyze_gcode_inertia_by_object(const GCodeProcessorResult& gcode_result, const Print& print, const GCodeInertiaOptions& options = {});
std::string format_gcode_inertia_report(const GCodeInertiaResult& result);
std::string format_gcode_inertia_json(const GCodeInertiaPlateResult& result);

} // namespace Slic3r

#endif
