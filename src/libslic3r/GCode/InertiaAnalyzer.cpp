#include "InertiaAnalyzer.hpp"

#include "Print.hpp"
#include "Model.hpp"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <boost/log/trivial.hpp>

namespace Slic3r {
namespace {

constexpr double PI = 3.14159265358979323846;

using Arr3 = std::array<double, 3>;
using Mat3d = std::array<std::array<double, 3>, 3>;
using OrderedJson = nlohmann::ordered_json;

static double dot(const Arr3& a, const Arr3& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static Arr3 sub(const Arr3& a, const Arr3& b)
{
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

static Arr3 add(const Arr3& a, const Arr3& b)
{
    return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };
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
        for (size_t j = 0; j < 3; ++j)
            mat[i][j] = ((i == j) ? i_perp : 0.0) + (i_parallel - i_perp) * uu[i][j];
    }
    return mat;
}

static Mat3d parallel_axis(double mass_kg, const Arr3& displacement_mm)
{
    const double d2 = dot(displacement_mm, displacement_mm);
    const Mat3d dd = outer(displacement_mm);
    Mat3d mat{{ {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0} }};
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 3; ++j)
            mat[i][j] = mass_kg * (((i == j) ? d2 : 0.0) - dd[i][j]);
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

struct InstanceLookup
{
    const PrintInstance* print_instance{ nullptr };
    size_t instance_id{ 0 };
    std::string object_name;
    Eigen::Transform<double, 3, Eigen::Affine> object_to_plate_inv = Eigen::Transform<double, 3, Eigen::Affine>::Identity();
};

static Arr3 to_arr3(const Vec3d& v)
{
    return { v.x(), v.y(), v.z() };
}

static OrderedJson to_json_array(const Arr3& v)
{
    return OrderedJson::array({ v[0], v[1], v[2] });
}

static OrderedJson to_json_matrix(const Mat3d& m)
{
    return OrderedJson::array({
        OrderedJson::array({ m[0][0], m[0][1], m[0][2] }),
        OrderedJson::array({ m[1][0], m[1][1], m[1][2] }),
        OrderedJson::array({ m[2][0], m[2][1], m[2][2] })
    });
}

static OrderedJson to_json_named_inertia(const Mat3d& m)
{
    return {
        { "I_xx", m[0][0] },
        { "I_yy", m[1][1] },
        { "I_zz", m[2][2] },
        { "I_xy", m[0][1] },
        { "I_xz", m[0][2] },
        { "I_yz", m[1][2] },
        { "matrix_rows", to_json_matrix(m) }
    };
}

template <typename T>
static std::string join_values(const std::set<T>& values)
{
    std::ostringstream ss;
    bool first = true;
    for (const T& value : values) {
        if (! first)
            ss << ',';
        ss << value;
        first = false;
    }
    return ss.str();
}

static std::string join_instance_labels(const std::map<int, InstanceLookup>& lookup)
{
    std::set<int> labels;
    for (const auto& [label_id, _] : lookup) {
        (void)_;
        labels.insert(label_id);
    }
    return join_values(labels);
}

static std::map<int, InstanceLookup> build_instance_lookup(const Print& print)
{
    std::map<int, InstanceLookup> lookup;
    for (const PrintObject* print_object : print.objects()) {
        if (print_object == nullptr || print_object->model_object() == nullptr)
            continue;
        const PrintInstances& instances = print_object->instances();
        for (const PrintInstance& instance : instances) {
            if (instance.model_instance == nullptr)
                continue;
            const size_t exported_instance_id = (instance.model_instance->arrange_order > 0) ?
                size_t(instance.model_instance->arrange_order) :
                (instance.id + 1);
            lookup[int(instance.model_instance->get_labeled_id())] = InstanceLookup{
                &instance,
                exported_instance_id,
                print_object->model_object()->name,
                instance.model_instance->get_matrix().inverse()
            };
        }
    }
    return lookup;
}

static Arr3 reconstructed_source_frame_shift_mm(const PrintInstance& print_instance)
{
    Arr3 shift{ 0.0, 0.0, 0.0 };

    if (print_instance.print_object != nullptr) {
        const Point& center_offset = print_instance.print_object->center_offset();
        shift[0] -= unscale<double>(center_offset.x());
        shift[1] -= unscale<double>(center_offset.y());
    }

    if (print_instance.model_instance != nullptr && print_instance.model_instance->get_object() != nullptr) {
        const ModelObject* model_object = print_instance.model_instance->get_object();
        const Vec3d& origin_translation = model_object->origin_translation;
        shift[0] -= origin_translation.x();
        shift[1] -= origin_translation.y();
        shift[2] -= origin_translation.z();

        for (const ModelVolume* volume : model_object->volumes) {
            if (volume == nullptr || ! volume->is_model_part())
                continue;
            shift[0] += volume->source.mesh_offset.x();
            shift[1] += volume->source.mesh_offset.y();
            shift[2] += volume->source.mesh_offset.z();
            break;
        }
    }

    return shift;
}

static std::string export_timestamp_utc()
{
    const std::time_t now = std::time(nullptr);
    std::tm tm_utc{};
#ifdef _WIN32
    gmtime_s(&tm_utc, &now);
#else
    gmtime_r(&now, &tm_utc);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

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

        double volume_mm3 = double(curr.mm3_per_mm) * length_mm;
        if (volume_mm3 <= 0.0 && double(curr.travel_dist) > 0.0)
            volume_mm3 = double(curr.mm3_per_mm) * double(curr.travel_dist);
        if (volume_mm3 <= 0.0)
            continue;

        const size_t extruder_id = size_t(curr.extruder_id);
        const double density_g_cm3 = extruder_id < gcode_result.filament_densities.size() ? double(gcode_result.filament_densities[extruder_id]) : 1.24;
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

    const Arr3 global_com{ weighted_com[0] / total_mass_kg, weighted_com[1] / total_mass_kg, weighted_com[2] / total_mass_kg };
    Mat3d total_inertia{{ {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0} }};

    for (const SegmentRecord& rec : records) {
        total_inertia = add(total_inertia, add(axis_inertia_matrix(rec.mass_kg, rec.radius_mm, rec.length_mm, rec.axis), parallel_axis(rec.mass_kg, sub(rec.com, global_com))));
    }

    result.ok = true;
    result.volume_mm3 = total_volume_mm3;
    result.mass_kg = total_mass_kg;
    result.center_of_mass_mm = global_com;
    result.inertia_tensor_kg_mm2 = total_inertia;
    result.extruders_used.assign(extruders_used.begin(), extruders_used.end());
    return result;
}

GCodeInertiaPlateResult analyze_gcode_inertia_by_object(const GCodeProcessorResult& gcode_result, const Print& print, const GCodeInertiaOptions& options)
{
    GCodeInertiaPlateResult plate_result;
    plate_result.plate_index = size_t(std::max(0, print.get_plate_index()));

    if (gcode_result.moves.size() < 2) {
        plate_result.error_message = "Not enough preview move data available.";
        return plate_result;
    }

    const std::map<int, InstanceLookup> instance_lookup = build_instance_lookup(print);
    if (instance_lookup.empty()) {
        plate_result.error_message = "Could not map preview object labels to sliced print instances for the current plate.";
        BOOST_LOG_TRIVIAL(info) << "Inertia analysis: instance lookup is empty for plate " << plate_result.plate_index;
        return plate_result;
    }

    const std::string instance_labels = join_instance_labels(instance_lookup);
    BOOST_LOG_TRIVIAL(info) << "Inertia analysis: plate " << plate_result.plate_index << " instance labels = [" << instance_labels << "]";

    struct ObjectAccumulator {
        GCodeInertiaObjectResult result;
        std::vector<SegmentRecord> records;
        Arr3 weighted_com{ 0.0, 0.0, 0.0 };
        Arr3 source_frame_shift_mm{ 0.0, 0.0, 0.0 };
    };

    std::map<int, ObjectAccumulator> accumulators;
    const Vec3d plate_origin = print.get_plate_origin();
    std::set<int> seen_move_labels;
    size_t eligible_extrusion_moves = 0;
    size_t labeled_extrusion_moves = 0;
    size_t unlabeled_extrusion_moves = 0;
    size_t matched_extrusion_moves = 0;

    for (size_t i = 1; i < gcode_result.moves.size(); ++i) {
        const auto& prev = gcode_result.moves[i - 1];
        const auto& curr = gcode_result.moves[i];

        if (curr.type != EMoveType::Extrude)
            continue;
        if (is_excluded_role(curr.extrusion_role, options))
            continue;

        ++eligible_extrusion_moves;

        const Vec3d prev_plate = Vec3d(double(prev.position.x()) - plate_origin.x(), double(prev.position.y()) - plate_origin.y(), double(prev.position.z()) - plate_origin.z());
        const Vec3d curr_plate = Vec3d(double(curr.position.x()) - plate_origin.x(), double(curr.position.y()) - plate_origin.y(), double(curr.position.z()) - plate_origin.z());

        if (curr.object_label_id < 0) {
            ++unlabeled_extrusion_moves;
            continue;
        }
        ++labeled_extrusion_moves;
        seen_move_labels.insert(curr.object_label_id);

        const int effective_label_id = curr.object_label_id;
        const auto instance_it = instance_lookup.find(effective_label_id);
        if (instance_it == instance_lookup.end() || instance_it->second.print_instance == nullptr || instance_it->second.print_instance->model_instance == nullptr)
            continue;

        ++matched_extrusion_moves;

        const double dx_world = double(curr.position.x() - prev.position.x());
        const double dy_world = double(curr.position.y() - prev.position.y());
        const double dz_world = double(curr.position.z() - prev.position.z());
        const double world_len = std::sqrt(dx_world * dx_world + dy_world * dy_world + dz_world * dz_world);
        const double volume_mm3 = (double(curr.mm3_per_mm) > 0.0) ? std::max(double(curr.mm3_per_mm) * world_len, 0.0) : 0.0;
        const double final_volume_mm3 = volume_mm3 > 0.0 ? volume_mm3 : double(curr.mm3_per_mm) * double(curr.travel_dist);
        if (final_volume_mm3 <= 0.0)
            continue;

        const InstanceLookup* chosen_lookup = &instance_it->second;
        const ModelInstance* model_instance = chosen_lookup->print_instance->model_instance;
        const Vec3d prev_obj = chosen_lookup->object_to_plate_inv * prev_plate;
        const Vec3d curr_obj = chosen_lookup->object_to_plate_inv * curr_plate;
        const Vec3d delta_obj = curr_obj - prev_obj;
        const double length_mm = delta_obj.norm();
        if (length_mm <= 1e-9)
            continue;

        const size_t extruder_id = size_t(curr.extruder_id);
        const double density_g_cm3 = extruder_id < gcode_result.filament_densities.size() ? double(gcode_result.filament_densities[extruder_id]) : 1.24;
        const double mass_kg = final_volume_mm3 * density_g_cm3 * 1e-6;
        const double area_mm2 = final_volume_mm3 / length_mm;
        double radius_mm = 0.0;
        if (area_mm2 > 0.0)
            radius_mm = std::sqrt(area_mm2 / PI);
        if (! std::isfinite(radius_mm) || radius_mm <= 0.0)
            radius_mm = std::sqrt(std::max(1e-9, double(curr.width) * double(curr.height)) / PI);

        const Arr3 axis = to_arr3(delta_obj.normalized());
        const Arr3 com = to_arr3(0.5 * (prev_obj + curr_obj));

        ObjectAccumulator& acc = accumulators[effective_label_id];
        if (acc.result.object_name.empty()) {
            acc.result.object_label_id = effective_label_id;
            acc.result.plate_index = plate_result.plate_index;
            acc.result.instance_id = chosen_lookup->instance_id;
            acc.result.model_object_id = size_t(model_instance->get_object()->id().id);
            acc.result.object_name = chosen_lookup->object_name;
            acc.source_frame_shift_mm = reconstructed_source_frame_shift_mm(*chosen_lookup->print_instance);
        }

        ++acc.result.extrusion_moves_total;
        ++acc.result.extrusion_moves_used;
        acc.result.volume_mm3 += final_volume_mm3;
        acc.result.mass_kg += mass_kg;
        acc.weighted_com[0] += mass_kg * com[0];
        acc.weighted_com[1] += mass_kg * com[1];
        acc.weighted_com[2] += mass_kg * com[2];
        acc.records.push_back(SegmentRecord{ mass_kg, final_volume_mm3, com, radius_mm, length_mm, axis });

        auto material_it = std::find_if(acc.result.materials.begin(), acc.result.materials.end(), [extruder_id](const GCodeInertiaMaterialUsage& item) {
            return item.extruder_id == extruder_id;
        });
        if (material_it == acc.result.materials.end()) {
            acc.result.materials.push_back(GCodeInertiaMaterialUsage{ unsigned(extruder_id), density_g_cm3, mass_kg, final_volume_mm3 });
        } else {
            material_it->mass_kg += mass_kg;
            material_it->volume_mm3 += final_volume_mm3;
            material_it->density_g_cm3 = density_g_cm3;
        }
    }

    const std::string move_labels = join_values(seen_move_labels);
    BOOST_LOG_TRIVIAL(info) << "Inertia analysis: plate " << plate_result.plate_index
                            << " eligible part extrusion moves=" << eligible_extrusion_moves
                            << " labeled extrusion moves=" << labeled_extrusion_moves
                            << ", unlabeled=" << unlabeled_extrusion_moves
                            << ", matched=" << matched_extrusion_moves
                            << ", move labels=[" << move_labels << "]";

    for (auto& [label_id, acc] : accumulators) {
        (void)label_id;
        if (acc.result.mass_kg <= 0.0 || acc.records.empty()) {
            acc.result.error_message = "No usable part extrusion moves found for this object.";
            plate_result.objects.push_back(acc.result);
            continue;
        }

        const Arr3 com{
            acc.weighted_com[0] / acc.result.mass_kg,
            acc.weighted_com[1] / acc.result.mass_kg,
            acc.weighted_com[2] / acc.result.mass_kg
        };
        Mat3d inertia_com{{ {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0} }};
        Mat3d inertia_origin{{ {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0} }};

        for (const SegmentRecord& rec : acc.records) {
            const Mat3d segment_body = axis_inertia_matrix(rec.mass_kg, rec.radius_mm, rec.length_mm, rec.axis);
            inertia_com = add(inertia_com, add(segment_body, parallel_axis(rec.mass_kg, sub(rec.com, com))));
            inertia_origin = add(inertia_origin, add(segment_body, parallel_axis(rec.mass_kg, rec.com)));
        }

        const Arr3 source_com = add(com, acc.source_frame_shift_mm);

        acc.result.ok = true;
        acc.result.center_of_mass_in_slicer_object_frame_mm = com;
        acc.result.center_of_mass_in_source_frame_mm = source_com;
        acc.result.inertia_about_com_kg_mm2 = inertia_com;
        acc.result.inertia_about_object_origin_kg_mm2 = inertia_origin;
        acc.result.inertia_about_source_origin_kg_mm2 = add(inertia_com, parallel_axis(acc.result.mass_kg, source_com));
        plate_result.objects.push_back(acc.result);
    }

    std::sort(plate_result.objects.begin(), plate_result.objects.end(), [](const GCodeInertiaObjectResult& a, const GCodeInertiaObjectResult& b) {
        if (a.object_name != b.object_name)
            return a.object_name < b.object_name;
        return a.instance_id < b.instance_id;
    });

    plate_result.ok = ! plate_result.objects.empty();
    if (! plate_result.ok && plate_result.error_message.empty()) {
        std::ostringstream ss;
        ss << "No per-object part extrusions could be analyzed from the preview toolpaths."
           << "\nEligible part extrusion moves: " << eligible_extrusion_moves
           << "\nLabeled part extrusion moves: " << labeled_extrusion_moves
           << "\nUnlabeled part extrusion moves: " << unlabeled_extrusion_moves
           << "\nMatched part extrusion moves: " << matched_extrusion_moves
           << "\nPreview move object labels: [" << (move_labels.empty() ? std::string("none") : move_labels) << "]"
           << "\nCurrent plate instance labels: [" << instance_labels << "]";
        plate_result.error_message = ss.str();
    }
    return plate_result;
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

std::string format_gcode_inertia_json(const GCodeInertiaPlateResult& result)
{
    OrderedJson root;
    root["schema_version"] = 1;
    root["exported_at_utc"] = export_timestamp_utc();
    root["generator"] = "OrcaSlicer";
    root["analysis_type"] = "toolpath_inertia";
    root["plate_index"] = result.plate_index;
    root["ok"] = result.ok;
    if (! result.error_message.empty())
        root["error_message"] = result.error_message;
    root["reference_frames"] = {
        { "source_origin", "Reconstructed source/CAD-like object coordinate system before Orca centering transforms" },
        { "slicer_object_origin", "Internal Orca slicer object coordinate system after Orca centering transforms" },
        { "com_frame", "Same axis orientation as the chosen object frame, translated to that frame's center of mass" }
    };
    root["inertia_tensor_convention"] = {
        { "units", "kg*mm^2" },
        { "products_of_inertia", OrderedJson::array({ "I_xy", "I_xz", "I_yz" }) },
        { "matrix_layout", OrderedJson::array({
            OrderedJson::array({ "I_xx", "I_xy", "I_xz" }),
            OrderedJson::array({ "I_xy", "I_yy", "I_yz" }),
            OrderedJson::array({ "I_xz", "I_yz", "I_zz" })
        }) }
    };
    root["objects"] = OrderedJson::array();

    for (const GCodeInertiaObjectResult& object : result.objects) {
        OrderedJson j;
        j["object_name"] = object.object_name;
        j["ok"] = object.ok;
        j["instance_id"] = object.instance_id;
        j["model_object_id"] = object.model_object_id;
        j["object_label_id"] = object.object_label_id;
        j["plate_index"] = object.plate_index;
        j["mass_kg"] = object.mass_kg;
        j["volume_mm3"] = object.volume_mm3;
        j["used_density_from_extruders"] = true;
        if (! object.error_message.empty())
            j["error_message"] = object.error_message;
        j["center_of_mass_in_source_frame_mm"] = to_json_array(object.center_of_mass_in_source_frame_mm);
        j["origin_to_com_offset_mm"] = to_json_array(object.center_of_mass_in_source_frame_mm);
        j["center_of_mass_in_slicer_object_frame_mm"] = to_json_array(object.center_of_mass_in_slicer_object_frame_mm);
        j["materials"] = OrderedJson::array();
        for (const GCodeInertiaMaterialUsage& material : object.materials) {
            j["materials"].push_back({
                { "extruder_id", material.extruder_id },
                { "density_g_cm3", material.density_g_cm3 },
                { "mass_kg", material.mass_kg },
                { "volume_mm3", material.volume_mm3 }
            });
        }
        j["inertia_tensors_kg_mm2"] = {
            { "about_source_origin", to_json_named_inertia(object.inertia_about_source_origin_kg_mm2) },
            { "about_object_origin", to_json_named_inertia(object.inertia_about_object_origin_kg_mm2) },
            { "about_center_of_mass", to_json_named_inertia(object.inertia_about_com_kg_mm2) }
        };
        root["objects"].push_back(j);
    }

    return root.dump(2);
}

} // namespace Slic3r
