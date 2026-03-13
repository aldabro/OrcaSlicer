#ifndef slic3r_GCode_InertiaAnalyzer_hpp_
#define slic3r_GCode_InertiaAnalyzer_hpp_

#include "GCodeProcessor.hpp"

#include <array>
#include <string>
#include <vector>

namespace Slic3r {

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

GCodeInertiaResult analyze_gcode_inertia(const GCodeProcessorResult& gcode_result, const GCodeInertiaOptions& options = {});
std::string format_gcode_inertia_report(const GCodeInertiaResult& result);

} // namespace Slic3r

#endif
