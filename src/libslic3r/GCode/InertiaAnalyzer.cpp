#include "InertiaAnalyzer.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace Slic3r {
namespace {

constexpr double PI = 3.14159265358979323846;

using Arr3 = std::array<double, 3>;
using Mat3d = std::array<std::array<double, 3>, 3>;

static double dot(const Arr3& a, const Arr3& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static Arr3 sub(const Arr3& a, const Arr3& b)
{
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

static Mat3d add(const Mat3d& a, const Mat3d& b)
{
    Mat3d out{{ {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0} }};
    for (size_t i = 0; i < 3; ++i)
        for (size_t j = 0; j < 3; ++j)
            out[i][j] = a[i][j] + b[i][j];
    return out;
}

static Mat3d outer(const Arr3& v)
{
    return {{
        { v[0] * v[0], v[0] * v[1], v[0] * v[2] },
        { v[1] * v[0], v[1] * v[1], v[1] * v[2] },
        { v[2] * v[0], v[2] * v[1], v[2] * v[2] }
    }};
}

static Mat3d axis_inertia_matrix(double mass_kg, double radius_mm, double length_mm, const Arr3& axis)
{
    const double i_parallel = 0.5 * mass_kg * radius_mm * radius_mm;
    const double i_perp = mass_kg * (3.0 * radius_mm * radius_mm + length_mm * length_mm) / 12.0;
    const Mat3d uu = outer(axis);
    Mat3d mat{{ {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0} }};
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            mat[i][j] = ((i == j) ? i_perp : 0.0) + (i_parallel - i_perp) * uu[i][j];
        }
    }
    return mat;
}

static Mat3d parallel_axis(double mass_kg, const Arr3& displacement_mm)
{
    const double d2 = dot(displacement_mm, displacement_mm);
    const Mat3d dd = outer(displacement_mm);
    Mat3d mat{{ {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0} }};
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            mat[i][j] = mass_kg * (((i == j) ? d2 : 0.0) - dd[i][j]);
        }
    }
    return mat;
}

static bool is_excluded_role(ExtrusionRole role, const GCodeInertiaOptions& options)
{
    if (! options.exclude_non_part_features && ! options.exclude_wipe_tower && ! options.exclude_custom)
        return false;

    switch (role) {
    case erSkirt:
    case erBrim:
    case erSupportMaterial:
    case erSupportMaterialInterface:
    case erSupportTransition:
        return options.exclude_non_part_features;
    case erWipeTower:
        return options.exclude_wipe_tower;
    case erCustom:
        return options.exclude_custom;
    default:
        return false;
    }
}

struct SegmentRecord
{
    double mass_kg{ 0.0 };
    double volume_mm3{ 0.0 };
    Arr3 com{ 0.0, 0.0, 0.0 };
    double radius_mm{ 0.0 };
    double length_mm{ 0.0 };
    Arr3 axis{ 0.0, 0.0, 1.0 };
};

} // namespace

GCodeInertiaResult analyze_gcode_inertia(const GCodeProcessorResult& gcode_result, const GCodeInertiaOptions& options)
{
    GCodeInertiaResult result;
    result.total_moves = gcode_result.moves.size();

    if (gcode_result.moves.size() < 2) {
        result.error_message = "Not enough preview move data available.";
        return result;
    }

    std::vector<SegmentRecord> records;
    records.reserve(gcode_result.moves.size());
    std::set<unsigned int> extruders_used;

    double total_mass_kg = 0.0;
    double total_volume_mm3 = 0.0;
    Arr3 weighted_com{ 0.0, 0.0, 0.0 };

    for (size_t i = 1; i < gcode_result.moves.size(); ++i) {
        const auto& prev = gcode_result.moves[i - 1];
        const auto& curr = gcode_result.moves[i];

        if (curr.type != EMoveType::Extrude)
            continue;

        ++result.extrusion_moves_total;

        if (is_excluded_role(curr.extrusion_role, options))
            continue;

        const double dx = double(curr.position.x() - prev.position.x());
        const double dy = double(curr.position.y() - prev.position.y());
        const double dz = double(curr.position.z() - prev.position.z());
        const double length_mm = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (length_mm <= 1e-9)
            continue;

        double volume_mm3 = double(curr.mm3_per_mm) * double(curr.travel_dist);
        if (volume_mm3 <= 0.0)
            volume_mm3 = double(curr.mm3_per_mm) * length_mm;
        if (volume_mm3 <= 0.0)
            continue;

        const size_t extruder_id = size_t(curr.extruder_id);
        const double density_g_cm3 = extruder_id < gcode_result.filament_densities.size() ?
            double(gcode_result.filament_densities[extruder_id]) : 1.24;
        const double mass_kg = volume_mm3 * density_g_cm3 * 1e-6;
        const double area_mm2 = volume_mm3 / length_mm;
        double radius_mm = 0.0;
        if (area_mm2 > 0.0)
            radius_mm = std::sqrt(area_mm2 / PI);
        if (! std::isfinite(radius_mm) || radius_mm <= 0.0)
            radius_mm = std::sqrt(std::max(1e-9, double(curr.width) * double(curr.height)) / PI);

        const Arr3 axis{ dx / length_mm, dy / length_mm, dz / length_mm };
        const Arr3 com{
            0.5 * double(curr.position.x() + prev.position.x()),
            0.5 * double(curr.position.y() + prev.position.y()),
            0.5 * double(curr.position.z() + prev.position.z())
        };

        records.push_back(SegmentRecord{ mass_kg, volume_mm3, com, radius_mm, length_mm, axis });
        extruders_used.insert(curr.extruder_id);
        ++result.extrusion_moves_used;

        total_mass_kg += mass_kg;
        total_volume_mm3 += volume_mm3;
        weighted_com[0] += mass_kg * com[0];
        weighted_com[1] += mass_kg * com[1];
        weighted_com[2] += mass_kg * com[2];
    }

    if (total_mass_kg <= 0.0 || records.empty()) {
        result.error_message = "No usable part extrusion moves found in preview data.";
        return result;
    }

    const Arr3 global_com{
        weighted_com[0] / total_mass_kg,
        weighted_com[1] / total_mass_kg,
        weighted_com[2] / total_mass_kg
    };
    Mat3d total_inertia{{ {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0} }};

    for (const SegmentRecord& rec : records) {
        total_inertia = add(total_inertia, add(
            axis_inertia_matrix(rec.mass_kg, rec.radius_mm, rec.length_mm, rec.axis),
            parallel_axis(rec.mass_kg, sub(rec.com, global_com))));
    }

    result.ok = true;
    result.volume_mm3 = total_volume_mm3;
    result.mass_kg = total_mass_kg;
    result.center_of_mass_mm = global_com;
    result.inertia_tensor_kg_mm2 = total_inertia;
    result.extruders_used.assign(extruders_used.begin(), extruders_used.end());
    return result;
}

std::string format_gcode_inertia_report(const GCodeInertiaResult& result)
{
    std::ostringstream ss;
    if (! result.ok) {
        ss << "Inertia analysis failed: " << result.error_message;
        return ss.str();
    }

    ss.setf(std::ios::fixed);
    ss.precision(6);
    ss << "Native toolpath inertia analysis\n\n";
    ss << "Used extrusion moves: " << result.extrusion_moves_used << " / " << result.extrusion_moves_total << "\n";
    ss << "Volume: " << result.volume_mm3 << " mm^3\n";
    ss << "Mass: " << result.mass_kg << " kg (" << result.mass_kg * 1000.0 << " g)\n";
    ss << "Center of mass [mm]:\n";
    ss << "  x=" << result.center_of_mass_mm[0] << "\n";
    ss << "  y=" << result.center_of_mass_mm[1] << "\n";
    ss << "  z=" << result.center_of_mass_mm[2] << "\n";
    ss << "Inertia tensor about COM [kg*mm^2]:\n";
    ss << "  Ixx=" << result.inertia_tensor_kg_mm2[0][0] << "\n";
    ss << "  Iyy=" << result.inertia_tensor_kg_mm2[1][1] << "\n";
    ss << "  Izz=" << result.inertia_tensor_kg_mm2[2][2] << "\n";
    ss << "  Ixy=" << result.inertia_tensor_kg_mm2[0][1] << "\n";
    ss << "  Ixz=" << result.inertia_tensor_kg_mm2[0][2] << "\n";
    ss << "  Iyz=" << result.inertia_tensor_kg_mm2[1][2] << "\n";
    if (! result.extruders_used.empty()) {
        ss << "Extruders used:";
        for (unsigned int extruder_id : result.extruders_used)
            ss << ' ' << (extruder_id + 1);
        ss << "\n";
    }
    ss << "\nExcluded by default: skirt, brim, support, support interface/transition, wipe tower, custom.\n";
    ss << "Reference frame: current preview/toolpath coordinates.\n";
    return ss.str();
}

} // namespace Slic3r
