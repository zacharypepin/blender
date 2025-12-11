#include "zachary.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <variant>
#include "BLI_listbase.h"
#include "BLI_map.hh"
#include "BLI_set.hh"
#include "BLI_stack.hh"
#include "BLI_vector.hh"
#include "BLI_listbase_wrapper.hh"
#include "BLI_math_euler.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector_types.hh"
#include "BLI_string_ref.hh"

#include "DNA_action_types.h"
#include "DNA_anim_types.h"
#include "DNA_armature_types.h"
#include "DNA_colorband_types.h"
#include "DNA_curve_types.h"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_nla_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_packedFile_types.h"
#include "DNA_scene_types.h"

#include "BKE_armature.hh"
#include "BKE_attribute.hh"
#include "BKE_collection.hh"
#include "BKE_context.hh"
#include "BKE_deform.hh"
#include "BKE_image.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_mesh.hh"
#include "BKE_node.hh"
#include "BKE_node_runtime.hh"

#include "ANIM_action.hh"
#include "ANIM_action_iterators.hh"

#include "IMB_colormanagement.hh"
#include "IMB_imbuf.hh"

#include "CLG_log.h"

#include "bb_archive/bb_archive.h"
#include "zp_c/arena.h"

#include <string>
#include <array>

// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
namespace
{
    static CLG_LogRef LOG = {"zachary"};

    struct vertex_t
    {
        blender::float3 position;
        blender::float2 uv0;
        blender::float2 uv1;
        blender::float2 uv2;
        std::array<uint8_t, 4> joint_indices = {0, 0, 0, 0};
        std::array<float, 4> joint_weights   = {0.0f, 0.0f, 0.0f, 0.0f};
    };

    struct triangle_t
    {
        std::array<vertex_t, 3> vertices;
    };

    struct submesh_t
    {
        std::string material_name;
        blender::Vector<triangle_t> triangles;
    };

    struct mesh_t
    {
        std::string name;
        blender::Vector<submesh_t> submeshes;
    };

    struct scene_elem_t
    {
        std::string name;
        std::optional<std::string> parent_name;
        blender::float3 pos;
        blender::float3 euler_rot;
        blender::float3 scale;
        std::optional<std::string> mesh_name;
        std::optional<std::string> skel_name;
    };

    struct scene_t
    {
        std::string name;
        blender::Map<std::string, scene_elem_t> elems;
    };

    struct image_t
    {
        std::string name;
        blender::Vector<uint8_t> rgba_data;
        uint32_t width;
        uint32_t height;
        bool is_srgb;
    };

    struct socket_value_float_t
    {
        float value;
    };
    struct socket_value_int_t
    {
        int value;
    };
    struct socket_value_bool_t
    {
        bool value;
    };
    struct socket_value_vector_t
    {
        blender::float3 value;
    };
    struct socket_value_rgba_t
    {
        blender::float4 value;
    };
    struct socket_value_tex_t
    {
        std::string value;
    };
    struct socket_value_uv_map_t
    {
        std::string value;
    };

    using socket_default_value_t = std::variant<socket_value_float_t, socket_value_int_t, socket_value_bool_t, socket_value_vector_t, socket_value_rgba_t, socket_value_tex_t, socket_value_uv_map_t>;

    struct shader_node_socket_t
    {
        std::string identifier;
        std::string idname;
        std::optional<socket_default_value_t> default_value;
    };

    struct shader_node_prop_t
    {
        std::string identifier;
        socket_default_value_t value;
    };

    struct shader_node_t
    {
        std::string name;
        std::string idname;
        blender::Vector<shader_node_socket_t> inputs;
        blender::Vector<shader_node_socket_t> outputs;
        blender::Vector<shader_node_prop_t> props;
    };

    struct shader_link_t
    {
        std::string from_node;
        std::string from_socket;
        std::string to_node;
        std::string to_socket;
    };

    struct material_t
    {
        std::string name;
        blender::Vector<shader_node_t> nodes;
        blender::Vector<shader_link_t> links;
    };

    struct bone_t
    {
        std::string name;
        int parent_index;
        std::array<float, 16> bind_local_matrix;
        std::array<float, 16> inverse_bind_matrix;
    };

    struct skeleton_t
    {
        std::string name;
        blender::Vector<bone_t> bones;
    };

    enum class anim_channel_type_t
    {
        LOCATION = 0,
        ROTATION = 1,
        SCALE    = 2,
    };

    enum class anim_interpolation_type_t
    {
        UNSUPPORTED = 0,
        STEP        = 1,
        LINEAR      = 2,
        CUBICSPLINE = 3,
    };

    struct anim_channel_t
    {
        uint32_t bone_index;
        anim_channel_type_t channel_type;
        anim_interpolation_type_t interpolation_type;
        blender::Vector<float> keyframe_times;
        blender::Vector<std::array<float, 4>> keyframe_values;
    };

    struct animation_t
    {
        std::string name;
        std::string armature_name;
        blender::Vector<anim_channel_t> channels;
    };

    struct data_t
    {
        blender::Vector<scene_t> scenes;
        blender::Map<std::string, mesh_t> meshes;
        blender::Map<std::string, image_t> images;
        blender::Map<std::string, material_t> materials;
        blender::Map<std::string, skeleton_t> skeletons;
        blender::Vector<animation_t> animations;
    };

    const std::set<std::string> supported_sockets                             = {"Base Color", "Emission Color", "Emission Strength", "Roughness", "Metallic", "Specular IOR Level", "Normal", "Alpha"};

    const std::map<std::string, std::set<std::string>> supported_node_outputs = {
        {       "ShaderNodeNewGeometry",                     {"Position", "Normal", "Tangent", "True Normal"}},
        {          "ShaderNodeTexCoord",                              {"Generated", "UV", "Object", "Normal"}},
        {        "ShaderNodeObjectInfo",                                                         {"Location"}},
        {             "ShaderNodeUVMap",                                                               {"UV"}},
        {    "ShaderNodeBsdfPrincipled",                                                             {"BSDF"}},
        {              "ShaderNodeMath",                                                            {"Value"}},
        {        "ShaderNodeVectorMath",                                                  {"Vector", "Value"}},
        {               "ShaderNodeMix", {"Result_Float", "Result_Vector", "Result_Color", "Result_Rotation"}},
        {     "ShaderNodeSeparateColor",                                             {"Red", "Green", "Blue"}},
        {      "ShaderNodeCombineColor",                                                            {"Color"}},
        {          "ShaderNodeTexNoise",                                                     {"Fac", "Color"}},
        {          "ShaderNodeTexImage",                                                   {"Color", "Alpha"}},
        {          "ShaderNodeValToRGB",                                                   {"Color", "Alpha"}},
        {  "ShaderNodeAmbientOcclusion",                                                      {"Color", "AO"}},
        {              "ShaderNodeBump",                                                           {"Normal"}},
        {             "ShaderNodeClamp",                                                           {"Result"}},
        {      "ShaderNodeDisplacement",                                                     {"Displacement"}},
        {          "ShaderNodeMapRange",                                                 {"Result", "Vector"}},
        {           "ShaderNodeMapping",                                                           {"Vector"}},
        {         "ShaderNodeNormalMap",                                                           {"Normal"}},
        {           "ShaderNodeTangent",                                                          {"Tangent"}},
        {          "ShaderNodeTexBrick",                                                     {"Color", "Fac"}},
        {    "ShaderNodeTexEnvironment",                                                            {"Color"}},
        {          "ShaderNodeTexGabor",                                      {"Value", "Phase", "Intensity"}},
        {       "ShaderNodeTexGradient",                                                     {"Color", "Fac"}},
        {          "ShaderNodeTexMagic",                                                     {"Color", "Fac"}},
        {        "ShaderNodeTexVoronoi",                     {"Distance", "Color", "Position", "W", "Radius"}},
        {           "ShaderNodeTexWave",                                                     {"Color", "Fac"}},
        {     "ShaderNodeTexWhiteNoise",                                                   {"Value", "Color"}},
        {"ShaderNodeVectorDisplacement",                                                     {"Displacement"}},
        {      "ShaderNodeVectorRotate",                                                           {"Vector"}},
        {   "ShaderNodeVectorTransform",                                                           {"Vector"}},
        {             "ShaderNodeValue",                                                            {"Value"}},
        {             "ShaderNodeGamma",                                                            {"Color"}},
        {    "ShaderNodeOutputMaterial",                                                                   {}},
        {        "ShaderNodeCombineXYZ",                                                           {"Vector"}},
        {       "ShaderNodeSeparateXYZ",                                                      {"X", "Y", "Z"}},
        {               "ShaderNodeRGB",                                                            {"Color"}},
        {    "ShaderNodeBrightContrast",                                                            {"Color"}},
        {           "ShaderNodeRGBToBW",                                                              {"Val"}},
        {     "ShaderNodeHueSaturation",                                                            {"Color"}},
        {            "ShaderNodeInvert",                                                            {"Color"}},
        {           "ShaderNodeFresnel",                                                              {"Fac"}},
    };

    const std::unordered_map<std::string, bb_shader_node_type_e> node_type_map = {
        {             "ShaderNodeUVMap",              BB_SHADER_NODE_TYPE_UV_MAP},
        {              "ShaderNodeMath",                BB_SHADER_NODE_TYPE_MATH},
        {        "ShaderNodeVectorMath",         BB_SHADER_NODE_TYPE_VECTOR_MATH},
        {               "ShaderNodeMix",                 BB_SHADER_NODE_TYPE_MIX},
        {     "ShaderNodeSeparateColor",      BB_SHADER_NODE_TYPE_SEPARATE_COLOR},
        {      "ShaderNodeCombineColor",       BB_SHADER_NODE_TYPE_COMBINE_COLOR},
        {          "ShaderNodeTexNoise",           BB_SHADER_NODE_TYPE_TEX_NOISE},
        {          "ShaderNodeTexImage",           BB_SHADER_NODE_TYPE_TEX_IMAGE},
        {          "ShaderNodeValToRGB",          BB_SHADER_NODE_TYPE_VAL_TO_RGB},
        {  "ShaderNodeAmbientOcclusion",   BB_SHADER_NODE_TYPE_AMBIENT_OCCLUSION},
        {              "ShaderNodeBump",                BB_SHADER_NODE_TYPE_BUMP},
        {             "ShaderNodeClamp",               BB_SHADER_NODE_TYPE_CLAMP},
        {      "ShaderNodeDisplacement",        BB_SHADER_NODE_TYPE_DISPLACEMENT},
        {          "ShaderNodeMapRange",           BB_SHADER_NODE_TYPE_MAP_RANGE},
        {           "ShaderNodeMapping",             BB_SHADER_NODE_TYPE_MAPPING},
        {         "ShaderNodeNormalMap",          BB_SHADER_NODE_TYPE_NORMAL_MAP},
        {           "ShaderNodeTangent",             BB_SHADER_NODE_TYPE_TANGENT},
        {          "ShaderNodeTexBrick",           BB_SHADER_NODE_TYPE_TEX_BRICK},
        {    "ShaderNodeTexEnvironment",     BB_SHADER_NODE_TYPE_TEX_ENVIRONMENT},
        {          "ShaderNodeTexGabor",           BB_SHADER_NODE_TYPE_TEX_GABOR},
        {       "ShaderNodeTexGradient",        BB_SHADER_NODE_TYPE_TEX_GRADIENT},
        {          "ShaderNodeTexMagic",           BB_SHADER_NODE_TYPE_TEX_MAGIC},
        {        "ShaderNodeTexVoronoi",         BB_SHADER_NODE_TYPE_TEX_VORONOI},
        {           "ShaderNodeTexWave",            BB_SHADER_NODE_TYPE_TEX_WAVE},
        {     "ShaderNodeTexWhiteNoise",     BB_SHADER_NODE_TYPE_TEX_WHITE_NOISE},
        {"ShaderNodeVectorDisplacement", BB_SHADER_NODE_TYPE_VECTOR_DISPLACEMENT},
        {      "ShaderNodeVectorRotate",       BB_SHADER_NODE_TYPE_VECTOR_ROTATE},
        {   "ShaderNodeVectorTransform",    BB_SHADER_NODE_TYPE_VECTOR_TRANSFORM},
        {             "ShaderNodeValue",               BB_SHADER_NODE_TYPE_VALUE},
        {             "ShaderNodeGamma",               BB_SHADER_NODE_TYPE_GAMMA},
        {        "ShaderNodeCombineXYZ",         BB_SHADER_NODE_TYPE_COMBINE_XYZ},
        {       "ShaderNodeSeparateXYZ",        BB_SHADER_NODE_TYPE_SEPARATE_XYZ},
        {               "ShaderNodeRGB",                 BB_SHADER_NODE_TYPE_RGB},
        {    "ShaderNodeBrightContrast",     BB_SHADER_NODE_TYPE_BRIGHT_CONTRAST},
        {           "ShaderNodeRGBToBW",           BB_SHADER_NODE_TYPE_RGB_TO_BW},
        {     "ShaderNodeHueSaturation",      BB_SHADER_NODE_TYPE_HUE_SATURATION},
        {            "ShaderNodeInvert",              BB_SHADER_NODE_TYPE_INVERT},
        {           "ShaderNodeFresnel",             BB_SHADER_NODE_TYPE_FRESNEL},
        {    "ShaderNodeOutputMaterial",              BB_SHADER_NODE_TYPE_OUTPUT},
        {    "ShaderNodeBsdfPrincipled",              BB_SHADER_NODE_TYPE_OUTPUT},
        {          "ShaderNodeTexCoord",               BB_SHADER_NODE_TYPE_INPUT},
        {       "ShaderNodeNewGeometry",               BB_SHADER_NODE_TYPE_INPUT},
        {        "ShaderNodeObjectInfo",               BB_SHADER_NODE_TYPE_INPUT},
    };

    const std::unordered_map<bb_shader_node_type_e, std::unordered_map<std::string, bb_shader_input_field_type_e>> input_field_map = {
        {             BB_SHADER_NODE_TYPE_OUTPUT,
         {
         {"Base Color", BB_SHADER_INPUT_FIELD_OUTPUT_BASE_COLOUR},
         {"Emission Color", BB_SHADER_INPUT_FIELD_OUTPUT_EMISSION},
         {"Emission Strength", BB_SHADER_INPUT_FIELD_OUTPUT_EMISSION_STRENGTH},
         {"Roughness", BB_SHADER_INPUT_FIELD_OUTPUT_ROUGHNESS},
         {"Metallic", BB_SHADER_INPUT_FIELD_OUTPUT_METALLIC},
         {"Specular IOR Level", BB_SHADER_INPUT_FIELD_OUTPUT_SPECULAR},
         {"Normal", BB_SHADER_INPUT_FIELD_OUTPUT_NORMAL},
         {"Alpha", BB_SHADER_INPUT_FIELD_OUTPUT_ALPHA},
         }},
        {               BB_SHADER_NODE_TYPE_MATH,
         {
         {"Value", BB_SHADER_INPUT_FIELD_MATH_VALUE},
         {"Value_001", BB_SHADER_INPUT_FIELD_MATH_VALUE_001},
         {"Value_002", BB_SHADER_INPUT_FIELD_MATH_VALUE_002},
         }},
        {        BB_SHADER_NODE_TYPE_VECTOR_MATH,
         {
         {"Vector", BB_SHADER_INPUT_FIELD_VECTOR_MATH_VECTOR},
         {"Vector_001", BB_SHADER_INPUT_FIELD_VECTOR_MATH_VECTOR_001},
         {"Vector_002", BB_SHADER_INPUT_FIELD_VECTOR_MATH_VECTOR_002},
         {"Scale", BB_SHADER_INPUT_FIELD_VECTOR_MATH_SCALE},
         }},
        {                BB_SHADER_NODE_TYPE_MIX,
         {
         {"Factor_Float", BB_SHADER_INPUT_FIELD_MIX_FACTOR_FLOAT},
         {"Factor_Vector", BB_SHADER_INPUT_FIELD_MIX_FACTOR_VECTOR},
         {"A_Float", BB_SHADER_INPUT_FIELD_MIX_A_FLOAT},
         {"B_Float", BB_SHADER_INPUT_FIELD_MIX_B_FLOAT},
         {"A_Vector", BB_SHADER_INPUT_FIELD_MIX_A_VECTOR},
         {"B_Vector", BB_SHADER_INPUT_FIELD_MIX_B_VECTOR},
         {"A_Color", BB_SHADER_INPUT_FIELD_MIX_A_COLOR},
         {"B_Color", BB_SHADER_INPUT_FIELD_MIX_B_COLOR},
         {"A_Rotation", BB_SHADER_INPUT_FIELD_MIX_A_ROTATION},
         {"B_Rotation", BB_SHADER_INPUT_FIELD_MIX_B_ROTATION},
         }},
        {     BB_SHADER_NODE_TYPE_SEPARATE_COLOR,
         {
         {"Color", BB_SHADER_INPUT_FIELD_SEPARATE_COLOR_COLOR},
         }},
        {      BB_SHADER_NODE_TYPE_COMBINE_COLOR,
         {
         {"Red", BB_SHADER_INPUT_FIELD_COMBINE_COLOR_RED},
         {"Green", BB_SHADER_INPUT_FIELD_COMBINE_COLOR_GREEN},
         {"Blue", BB_SHADER_INPUT_FIELD_COMBINE_COLOR_BLUE},
         }},
        {       BB_SHADER_NODE_TYPE_SEPARATE_XYZ,
         {
         {"Vector", BB_SHADER_INPUT_FIELD_SEPARATE_XYZ_VECTOR},
         }},
        {        BB_SHADER_NODE_TYPE_COMBINE_XYZ,
         {
         {"X", BB_SHADER_INPUT_FIELD_COMBINE_XYZ_X},
         {"Y", BB_SHADER_INPUT_FIELD_COMBINE_XYZ_Y},
         {"Z", BB_SHADER_INPUT_FIELD_COMBINE_XYZ_Z},
         }},
        {          BB_SHADER_NODE_TYPE_TEX_NOISE,
         {
         {"Vector", BB_SHADER_INPUT_FIELD_TEX_NOISE_VECTOR},
         {"W", BB_SHADER_INPUT_FIELD_TEX_NOISE_W},
         {"Scale", BB_SHADER_INPUT_FIELD_TEX_NOISE_SCALE},
         {"Detail", BB_SHADER_INPUT_FIELD_TEX_NOISE_DETAIL},
         {"Roughness", BB_SHADER_INPUT_FIELD_TEX_NOISE_ROUGHNESS},
         {"Lacunarity", BB_SHADER_INPUT_FIELD_TEX_NOISE_LACUNARITY},
         {"Offset", BB_SHADER_INPUT_FIELD_TEX_NOISE_OFFSET},
         {"Gain", BB_SHADER_INPUT_FIELD_TEX_NOISE_GAIN},
         {"Distortion", BB_SHADER_INPUT_FIELD_TEX_NOISE_DISTORTION},
         }},
        {          BB_SHADER_NODE_TYPE_TEX_IMAGE,
         {
         {"Vector", BB_SHADER_INPUT_FIELD_TEX_IMAGE_VECTOR},
         }},
        {         BB_SHADER_NODE_TYPE_VAL_TO_RGB,
         {
         {"Fac", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_FAC},
         }},
        {  BB_SHADER_NODE_TYPE_AMBIENT_OCCLUSION,
         {
         {"Color", BB_SHADER_INPUT_FIELD_AMBIENT_OCCLUSION_COLOR},
         {"Distance", BB_SHADER_INPUT_FIELD_AMBIENT_OCCLUSION_DISTANCE},
         {"Normal", BB_SHADER_INPUT_FIELD_AMBIENT_OCCLUSION_NORMAL},
         }},
        {               BB_SHADER_NODE_TYPE_BUMP,
         {
         {"Strength", BB_SHADER_INPUT_FIELD_BUMP_STRENGTH},
         {"Distance", BB_SHADER_INPUT_FIELD_BUMP_DISTANCE},
         {"Filter Width", BB_SHADER_INPUT_FIELD_BUMP_FILTER_WIDTH},
         {"Height", BB_SHADER_INPUT_FIELD_BUMP_HEIGHT},
         {"Normal", BB_SHADER_INPUT_FIELD_BUMP_NORMAL},
         }},
        {              BB_SHADER_NODE_TYPE_CLAMP,
         {
         {"Value", BB_SHADER_INPUT_FIELD_CLAMP_VALUE},
         {"Min", BB_SHADER_INPUT_FIELD_CLAMP_MIN},
         {"Max", BB_SHADER_INPUT_FIELD_CLAMP_MAX},
         }},
        {       BB_SHADER_NODE_TYPE_DISPLACEMENT,
         {
         {"Height", BB_SHADER_INPUT_FIELD_DISPLACEMENT_HEIGHT},
         {"Midlevel", BB_SHADER_INPUT_FIELD_DISPLACEMENT_MIDLEVEL},
         {"Scale", BB_SHADER_INPUT_FIELD_DISPLACEMENT_SCALE},
         {"Normal", BB_SHADER_INPUT_FIELD_DISPLACEMENT_NORMAL},
         }},
        {          BB_SHADER_NODE_TYPE_MAP_RANGE,
         {
         {"Value", BB_SHADER_INPUT_FIELD_MAP_RANGE_VALUE},
         {"From Min", BB_SHADER_INPUT_FIELD_MAP_RANGE_FROM_MIN},
         {"From Max", BB_SHADER_INPUT_FIELD_MAP_RANGE_FROM_MAX},
         {"To Min", BB_SHADER_INPUT_FIELD_MAP_RANGE_TO_MIN},
         {"To Max", BB_SHADER_INPUT_FIELD_MAP_RANGE_TO_MAX},
         {"Steps", BB_SHADER_INPUT_FIELD_MAP_RANGE_STEPS},
         {"Vector", BB_SHADER_INPUT_FIELD_MAP_RANGE_VECTOR},
         {"From_Min_FLOAT3", BB_SHADER_INPUT_FIELD_MAP_RANGE_FROM_MIN_FLOAT3},
         {"From_Max_FLOAT3", BB_SHADER_INPUT_FIELD_MAP_RANGE_FROM_MAX_FLOAT3},
         {"To_Min_FLOAT3", BB_SHADER_INPUT_FIELD_MAP_RANGE_TO_MIN_FLOAT3},
         {"To_Max_FLOAT3", BB_SHADER_INPUT_FIELD_MAP_RANGE_TO_MAX_FLOAT3},
         {"Steps_FLOAT3", BB_SHADER_INPUT_FIELD_MAP_RANGE_STEPS_FLOAT3},
         }},
        {            BB_SHADER_NODE_TYPE_MAPPING,
         {
         {"Vector", BB_SHADER_INPUT_FIELD_MAPPING_VECTOR},
         {"Location", BB_SHADER_INPUT_FIELD_MAPPING_LOCATION},
         {"Rotation", BB_SHADER_INPUT_FIELD_MAPPING_ROTATION},
         {"Scale", BB_SHADER_INPUT_FIELD_MAPPING_SCALE},
         }},
        {         BB_SHADER_NODE_TYPE_NORMAL_MAP,
         {
         {"Strength", BB_SHADER_INPUT_FIELD_NORMAL_MAP_STRENGTH},
         {"Color", BB_SHADER_INPUT_FIELD_NORMAL_MAP_COLOR},
         }},
        {          BB_SHADER_NODE_TYPE_TEX_BRICK,
         {
         {"Vector", BB_SHADER_INPUT_FIELD_TEX_BRICK_VECTOR},
         {"Color1", BB_SHADER_INPUT_FIELD_TEX_BRICK_COLOR1},
         {"Color2", BB_SHADER_INPUT_FIELD_TEX_BRICK_COLOR2},
         {"Mortar", BB_SHADER_INPUT_FIELD_TEX_BRICK_MORTAR},
         {"Scale", BB_SHADER_INPUT_FIELD_TEX_BRICK_SCALE},
         {"Mortar Size", BB_SHADER_INPUT_FIELD_TEX_BRICK_MORTAR_SIZE},
         {"Mortar Smooth", BB_SHADER_INPUT_FIELD_TEX_BRICK_MORTAR_SMOOTH},
         {"Bias", BB_SHADER_INPUT_FIELD_TEX_BRICK_BIAS},
         {"Brick Width", BB_SHADER_INPUT_FIELD_TEX_BRICK_BRICK_WIDTH},
         {"Row Height", BB_SHADER_INPUT_FIELD_TEX_BRICK_ROW_HEIGHT},
         }},
        {              BB_SHADER_NODE_TYPE_GAMMA,
         {
         {"Color", BB_SHADER_INPUT_FIELD_GAMMA_COLOR},
         {"Gamma", BB_SHADER_INPUT_FIELD_GAMMA_GAMMA},
         }},
        {    BB_SHADER_NODE_TYPE_BRIGHT_CONTRAST,
         {
         {"Color", BB_SHADER_INPUT_FIELD_BRIGHT_CONTRAST_COLOR},
         {"Bright", BB_SHADER_INPUT_FIELD_BRIGHT_CONTRAST_BRIGHT},
         {"Contrast", BB_SHADER_INPUT_FIELD_BRIGHT_CONTRAST_CONTRAST},
         }},
        {          BB_SHADER_NODE_TYPE_RGB_TO_BW,
         {
         {"Color", BB_SHADER_INPUT_FIELD_RGB_TO_BW_COLOR},
         }},
        {     BB_SHADER_NODE_TYPE_HUE_SATURATION,
         {
         {"Hue", BB_SHADER_INPUT_FIELD_HUE_SATURATION_HUE},
         {"Saturation", BB_SHADER_INPUT_FIELD_HUE_SATURATION_SATURATION},
         {"Value", BB_SHADER_INPUT_FIELD_HUE_SATURATION_VALUE},
         {"Fac", BB_SHADER_INPUT_FIELD_HUE_SATURATION_FAC},
         {"Color", BB_SHADER_INPUT_FIELD_HUE_SATURATION_COLOR},
         }},
        {             BB_SHADER_NODE_TYPE_INVERT,
         {
         {"Fac", BB_SHADER_INPUT_FIELD_INVERT_FAC},
         {"Color", BB_SHADER_INPUT_FIELD_INVERT_COLOR},
         }},
        {            BB_SHADER_NODE_TYPE_FRESNEL,
         {
         {"IOR", BB_SHADER_INPUT_FIELD_FRESNEL_IOR},
         {"Normal", BB_SHADER_INPUT_FIELD_FRESNEL_NORMAL},
         }},
        {              BB_SHADER_NODE_TYPE_VALUE,
         {
         {"Value", BB_SHADER_INPUT_FIELD_VALUE_VALUE},
         }},
        {                BB_SHADER_NODE_TYPE_RGB,
         {
         {"Color", BB_SHADER_INPUT_FIELD_RGB_COLOR},
         }},
        {             BB_SHADER_NODE_TYPE_UV_MAP,
         {
         {"UV", BB_SHADER_INPUT_FIELD_UV_MAP_UV},
         }},
        {    BB_SHADER_NODE_TYPE_TEX_ENVIRONMENT,
         {
         {"Vector", BB_SHADER_INPUT_FIELD_TEX_ENVIRONMENT_VECTOR},
         }},
        {          BB_SHADER_NODE_TYPE_TEX_GABOR,
         {
         {"Vector", BB_SHADER_INPUT_FIELD_TEX_GABOR_VECTOR},
         {"Scale", BB_SHADER_INPUT_FIELD_TEX_GABOR_SCALE},
         {"Frequency", BB_SHADER_INPUT_FIELD_TEX_GABOR_FREQUENCY},
         {"Anisotropy", BB_SHADER_INPUT_FIELD_TEX_GABOR_ANISOTROPY},
         {"Orientation 2D", BB_SHADER_INPUT_FIELD_TEX_GABOR_ORIENTATION_2D},
         {"Orientation 3D", BB_SHADER_INPUT_FIELD_TEX_GABOR_ORIENTATION_3D},
         }},
        {       BB_SHADER_NODE_TYPE_TEX_GRADIENT,
         {
         {"Vector", BB_SHADER_INPUT_FIELD_TEX_GRADIENT_VECTOR},
         }},
        {          BB_SHADER_NODE_TYPE_TEX_MAGIC,
         {
         {"Vector", BB_SHADER_INPUT_FIELD_TEX_MAGIC_VECTOR},
         {"Scale", BB_SHADER_INPUT_FIELD_TEX_MAGIC_SCALE},
         {"Distortion", BB_SHADER_INPUT_FIELD_TEX_MAGIC_DISTORTION},
         }},
        {        BB_SHADER_NODE_TYPE_TEX_VORONOI,
         {
         {"Vector", BB_SHADER_INPUT_FIELD_TEX_VORONOI_VECTOR},
         {"W", BB_SHADER_INPUT_FIELD_TEX_VORONOI_W},
         {"Scale", BB_SHADER_INPUT_FIELD_TEX_VORONOI_SCALE},
         {"Detail", BB_SHADER_INPUT_FIELD_TEX_VORONOI_DETAIL},
         {"Roughness", BB_SHADER_INPUT_FIELD_TEX_VORONOI_ROUGHNESS},
         {"Lacunarity", BB_SHADER_INPUT_FIELD_TEX_VORONOI_LACUNARITY},
         {"Smoothness", BB_SHADER_INPUT_FIELD_TEX_VORONOI_SMOOTHNESS},
         {"Exponent", BB_SHADER_INPUT_FIELD_TEX_VORONOI_EXPONENT},
         {"Randomness", BB_SHADER_INPUT_FIELD_TEX_VORONOI_RANDOMNESS},
         }},
        {           BB_SHADER_NODE_TYPE_TEX_WAVE,
         {
         {"Vector", BB_SHADER_INPUT_FIELD_TEX_WAVE_VECTOR},
         {"Scale", BB_SHADER_INPUT_FIELD_TEX_WAVE_SCALE},
         {"Distortion", BB_SHADER_INPUT_FIELD_TEX_WAVE_DISTORTION},
         {"Detail", BB_SHADER_INPUT_FIELD_TEX_WAVE_DETAIL},
         {"Detail Scale", BB_SHADER_INPUT_FIELD_TEX_WAVE_DETAIL_SCALE},
         {"Detail Roughness", BB_SHADER_INPUT_FIELD_TEX_WAVE_DETAIL_ROUGHNESS},
         {"Phase Offset", BB_SHADER_INPUT_FIELD_TEX_WAVE_PHASE_OFFSET},
         }},
        {    BB_SHADER_NODE_TYPE_TEX_WHITE_NOISE,
         {
         {"Vector", BB_SHADER_INPUT_FIELD_TEX_WHITE_NOISE_VECTOR},
         {"W", BB_SHADER_INPUT_FIELD_TEX_WHITE_NOISE_W},
         }},
        {BB_SHADER_NODE_TYPE_VECTOR_DISPLACEMENT,
         {
         {"Vector", BB_SHADER_INPUT_FIELD_VECTOR_DISPLACEMENT_VECTOR},
         {"Midlevel", BB_SHADER_INPUT_FIELD_VECTOR_DISPLACEMENT_MIDLEVEL},
         {"Scale", BB_SHADER_INPUT_FIELD_VECTOR_DISPLACEMENT_SCALE},
         }},
        {      BB_SHADER_NODE_TYPE_VECTOR_ROTATE,
         {
         {"Vector", BB_SHADER_INPUT_FIELD_VECTOR_ROTATE_VECTOR},
         {"Center", BB_SHADER_INPUT_FIELD_VECTOR_ROTATE_CENTER},
         {"Axis", BB_SHADER_INPUT_FIELD_VECTOR_ROTATE_AXIS},
         {"Angle", BB_SHADER_INPUT_FIELD_VECTOR_ROTATE_ANGLE},
         {"Rotation", BB_SHADER_INPUT_FIELD_VECTOR_ROTATE_ROTATION},
         }},
        {   BB_SHADER_NODE_TYPE_VECTOR_TRANSFORM,
         {
         {"Vector", BB_SHADER_INPUT_FIELD_VECTOR_TRANSFORM_VECTOR},
         }},
    };

    const std::unordered_map<bb_shader_node_type_e, std::unordered_map<std::string, bb_shader_output_field_type_e>> output_field_map = {
        {              BB_SHADER_NODE_TYPE_INPUT,
         {
         {"Object", BB_SHADER_OUTPUT_FIELD_INPUT_OBJECT},
         {"Generated", BB_SHADER_OUTPUT_FIELD_INPUT_GENERATED},
         {"UV", BB_SHADER_OUTPUT_FIELD_INPUT_UV0},
         {"Position", BB_SHADER_OUTPUT_FIELD_INPUT_POSITION},
         {"Normal", BB_SHADER_OUTPUT_FIELD_INPUT_NORMAL},
         {"Tangent", BB_SHADER_OUTPUT_FIELD_INPUT_TANGENT},
         {"True Normal", BB_SHADER_OUTPUT_FIELD_INPUT_GEO_NORMAL},
         {"Location", BB_SHADER_OUTPUT_FIELD_INPUT_LOCATION},
         }},
        {             BB_SHADER_NODE_TYPE_UV_MAP,
         {
         {"UV", BB_SHADER_OUTPUT_FIELD_UV_MAP_UV},
         }},
        {               BB_SHADER_NODE_TYPE_MATH,
         {
         {"Value", BB_SHADER_OUTPUT_FIELD_MATH_VALUE},
         }},
        {        BB_SHADER_NODE_TYPE_VECTOR_MATH,
         {
         {"Vector", BB_SHADER_OUTPUT_FIELD_VECTOR_MATH_VECTOR},
         {"Value", BB_SHADER_OUTPUT_FIELD_VECTOR_MATH_VALUE},
         }},
        {                BB_SHADER_NODE_TYPE_MIX,
         {
         {"Result_Float", BB_SHADER_OUTPUT_FIELD_MIX_RESULT_FLOAT},
         {"Result_Vector", BB_SHADER_OUTPUT_FIELD_MIX_RESULT_VECTOR},
         {"Result_Color", BB_SHADER_OUTPUT_FIELD_MIX_RESULT_COLOR},
         {"Result_Rotation", BB_SHADER_OUTPUT_FIELD_MIX_RESULT_ROTATION},
         }},
        {     BB_SHADER_NODE_TYPE_SEPARATE_COLOR,
         {
         {"Red", BB_SHADER_OUTPUT_FIELD_SEPARATE_COLOR_RED},
         {"Green", BB_SHADER_OUTPUT_FIELD_SEPARATE_COLOR_GREEN},
         {"Blue", BB_SHADER_OUTPUT_FIELD_SEPARATE_COLOR_BLUE},
         }},
        {      BB_SHADER_NODE_TYPE_COMBINE_COLOR,
         {
         {"Color", BB_SHADER_OUTPUT_FIELD_COMBINE_COLOR_COLOR},
         }},
        {       BB_SHADER_NODE_TYPE_SEPARATE_XYZ,
         {
         {"X", BB_SHADER_OUTPUT_FIELD_SEPARATE_XYZ_X},
         {"Y", BB_SHADER_OUTPUT_FIELD_SEPARATE_XYZ_Y},
         {"Z", BB_SHADER_OUTPUT_FIELD_SEPARATE_XYZ_Z},
         }},
        {        BB_SHADER_NODE_TYPE_COMBINE_XYZ,
         {
         {"Vector", BB_SHADER_OUTPUT_FIELD_COMBINE_XYZ_VECTOR},
         }},
        {          BB_SHADER_NODE_TYPE_TEX_NOISE,
         {
         {"Fac", BB_SHADER_OUTPUT_FIELD_TEX_NOISE_FAC},
         {"Color", BB_SHADER_OUTPUT_FIELD_TEX_NOISE_COLOR},
         }},
        {          BB_SHADER_NODE_TYPE_TEX_IMAGE,
         {
         {"Color", BB_SHADER_OUTPUT_FIELD_TEX_IMAGE_COLOR},
         {"Alpha", BB_SHADER_OUTPUT_FIELD_TEX_IMAGE_ALPHA},
         }},
        {         BB_SHADER_NODE_TYPE_VAL_TO_RGB,
         {
         {"Color", BB_SHADER_OUTPUT_FIELD_VAL_TO_RGB_COLOR},
         {"Alpha", BB_SHADER_OUTPUT_FIELD_VAL_TO_RGB_ALPHA},
         }},
        {  BB_SHADER_NODE_TYPE_AMBIENT_OCCLUSION,
         {
         {"Color", BB_SHADER_OUTPUT_FIELD_AMBIENT_OCCLUSION_COLOR},
         {"AO", BB_SHADER_OUTPUT_FIELD_AMBIENT_OCCLUSION_AO},
         }},
        {               BB_SHADER_NODE_TYPE_BUMP,
         {
         {"Normal", BB_SHADER_OUTPUT_FIELD_BUMP_NORMAL},
         }},
        {              BB_SHADER_NODE_TYPE_CLAMP,
         {
         {"Result", BB_SHADER_OUTPUT_FIELD_CLAMP_RESULT},
         }},
        {       BB_SHADER_NODE_TYPE_DISPLACEMENT,
         {
         {"Displacement", BB_SHADER_OUTPUT_FIELD_DISPLACEMENT_DISPLACEMENT},
         }},
        {          BB_SHADER_NODE_TYPE_MAP_RANGE,
         {
         {"Result", BB_SHADER_OUTPUT_FIELD_MAP_RANGE_RESULT},
         {"Vector", BB_SHADER_OUTPUT_FIELD_MAP_RANGE_VECTOR},
         }},
        {            BB_SHADER_NODE_TYPE_MAPPING,
         {
         {"Vector", BB_SHADER_OUTPUT_FIELD_MAPPING_VECTOR},
         }},
        {         BB_SHADER_NODE_TYPE_NORMAL_MAP,
         {
         {"Normal", BB_SHADER_OUTPUT_FIELD_NORMAL_MAP_NORMAL},
         }},
        {            BB_SHADER_NODE_TYPE_TANGENT,
         {
         {"Tangent", BB_SHADER_OUTPUT_FIELD_TANGENT_TANGENT},
         }},
        {          BB_SHADER_NODE_TYPE_TEX_BRICK,
         {
         {"Color", BB_SHADER_OUTPUT_FIELD_TEX_BRICK_COLOR},
         {"Fac", BB_SHADER_OUTPUT_FIELD_TEX_BRICK_FAC},
         }},
        {              BB_SHADER_NODE_TYPE_GAMMA,
         {
         {"Color", BB_SHADER_OUTPUT_FIELD_GAMMA_COLOR},
         }},
        {    BB_SHADER_NODE_TYPE_BRIGHT_CONTRAST,
         {
         {"Color", BB_SHADER_OUTPUT_FIELD_BRIGHT_CONTRAST_COLOR},
         }},
        {          BB_SHADER_NODE_TYPE_RGB_TO_BW,
         {
         {"Val", BB_SHADER_OUTPUT_FIELD_RGB_TO_BW_VAL},
         }},
        {     BB_SHADER_NODE_TYPE_HUE_SATURATION,
         {
         {"Color", BB_SHADER_OUTPUT_FIELD_HUE_SATURATION_COLOR},
         }},
        {             BB_SHADER_NODE_TYPE_INVERT,
         {
         {"Color", BB_SHADER_OUTPUT_FIELD_INVERT_COLOR},
         }},
        {            BB_SHADER_NODE_TYPE_FRESNEL,
         {
         {"Fac", BB_SHADER_OUTPUT_FIELD_FRESNEL_FAC},
         }},
        {              BB_SHADER_NODE_TYPE_VALUE,
         {
         {"Value", BB_SHADER_OUTPUT_FIELD_VALUE_VALUE},
         }},
        {                BB_SHADER_NODE_TYPE_RGB,
         {
         {"Color", BB_SHADER_OUTPUT_FIELD_RGB_COLOR},
         }},
        {    BB_SHADER_NODE_TYPE_TEX_ENVIRONMENT,
         {
         {"Color", BB_SHADER_OUTPUT_FIELD_TEX_ENVIRONMENT_COLOR},
         }},
        {          BB_SHADER_NODE_TYPE_TEX_GABOR,
         {
         {"Value", BB_SHADER_OUTPUT_FIELD_TEX_GABOR_VALUE},
         {"Phase", BB_SHADER_OUTPUT_FIELD_TEX_GABOR_PHASE},
         {"Intensity", BB_SHADER_OUTPUT_FIELD_TEX_GABOR_INTENSITY},
         }},
        {       BB_SHADER_NODE_TYPE_TEX_GRADIENT,
         {
         {"Color", BB_SHADER_OUTPUT_FIELD_TEX_GRADIENT_COLOR},
         {"Fac", BB_SHADER_OUTPUT_FIELD_TEX_GRADIENT_FAC},
         }},
        {          BB_SHADER_NODE_TYPE_TEX_MAGIC,
         {
         {"Color", BB_SHADER_OUTPUT_FIELD_TEX_MAGIC_COLOR},
         {"Fac", BB_SHADER_OUTPUT_FIELD_TEX_MAGIC_FAC},
         }},
        {        BB_SHADER_NODE_TYPE_TEX_VORONOI,
         {
         {"Distance", BB_SHADER_OUTPUT_FIELD_TEX_VORONOI_DISTANCE},
         {"Color", BB_SHADER_OUTPUT_FIELD_TEX_VORONOI_COLOR},
         {"Position", BB_SHADER_OUTPUT_FIELD_TEX_VORONOI_POSITION},
         {"W", BB_SHADER_OUTPUT_FIELD_TEX_VORONOI_W},
         {"Radius", BB_SHADER_OUTPUT_FIELD_TEX_VORONOI_RADIUS},
         }},
        {           BB_SHADER_NODE_TYPE_TEX_WAVE,
         {
         {"Color", BB_SHADER_OUTPUT_FIELD_TEX_WAVE_COLOR},
         {"Fac", BB_SHADER_OUTPUT_FIELD_TEX_WAVE_FAC},
         }},
        {    BB_SHADER_NODE_TYPE_TEX_WHITE_NOISE,
         {
         {"Value", BB_SHADER_OUTPUT_FIELD_TEX_WHITE_NOISE_VALUE},
         {"Color", BB_SHADER_OUTPUT_FIELD_TEX_WHITE_NOISE_COLOR},
         }},
        {BB_SHADER_NODE_TYPE_VECTOR_DISPLACEMENT,
         {
         {"Displacement", BB_SHADER_OUTPUT_FIELD_VECTOR_DISPLACEMENT_DISPLACEMENT},
         }},
        {      BB_SHADER_NODE_TYPE_VECTOR_ROTATE,
         {
         {"Vector", BB_SHADER_OUTPUT_FIELD_VECTOR_ROTATE_VECTOR},
         }},
        {   BB_SHADER_NODE_TYPE_VECTOR_TRANSFORM,
         {
         {"Vector", BB_SHADER_OUTPUT_FIELD_VECTOR_TRANSFORM_VECTOR},
         }},
    };

    const std::unordered_map<bb_shader_node_type_e, std::unordered_map<std::string, bb_shader_input_field_type_e>> prop_field_map = {
        {               BB_SHADER_NODE_TYPE_MATH,
         {
         {"operation", BB_SHADER_INPUT_FIELD_MATH_OPERATION},
         {"use_clamp", BB_SHADER_INPUT_FIELD_MATH_USE_CLAMP},
         }},
        {        BB_SHADER_NODE_TYPE_VECTOR_MATH,
         {
         {"operation", BB_SHADER_INPUT_FIELD_VECTOR_MATH_OPERATION},
         }},
        {                BB_SHADER_NODE_TYPE_MIX,
         {
         {"blend_type", BB_SHADER_INPUT_FIELD_MIX_BLEND_TYPE},
         {"clamp_factor", BB_SHADER_INPUT_FIELD_MIX_CLAMP_FACTOR},
         {"clamp_result", BB_SHADER_INPUT_FIELD_MIX_CLAMP_RESULT},
         {"data_type", BB_SHADER_INPUT_FIELD_MIX_DATA_TYPE},
         {"factor_mode", BB_SHADER_INPUT_FIELD_MIX_FACTOR_MODE},
         }},
        {     BB_SHADER_NODE_TYPE_SEPARATE_COLOR,
         {
         {"mode", BB_SHADER_INPUT_FIELD_SEPARATE_COLOR_MODE},
         }},
        {      BB_SHADER_NODE_TYPE_COMBINE_COLOR,
         {
         {"mode", BB_SHADER_INPUT_FIELD_COMBINE_COLOR_MODE},
         }},
        {          BB_SHADER_NODE_TYPE_TEX_NOISE,
         {
         {"noise_dimensions", BB_SHADER_INPUT_FIELD_TEX_NOISE_NOISE_DIMENSIONS},
         {"noise_type", BB_SHADER_INPUT_FIELD_TEX_NOISE_NOISE_TYPE},
         {"normalize", BB_SHADER_INPUT_FIELD_TEX_NOISE_NORMALIZE},
         }},
        {          BB_SHADER_NODE_TYPE_TEX_IMAGE,
         {
         {"interpolation", BB_SHADER_INPUT_FIELD_TEX_IMAGE_INTERPOLATION},
         {"projection", BB_SHADER_INPUT_FIELD_TEX_IMAGE_PROJECTION},
         {"extension", BB_SHADER_INPUT_FIELD_TEX_IMAGE_EXTENSION},
         {"image", BB_SHADER_INPUT_FIELD_TEX_IMAGE_IMAGE},
         }},
        {         BB_SHADER_NODE_TYPE_VAL_TO_RGB,
         {
         {"interpolation", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_INTERPOLATION},
         {"element_count", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_ELEMENT_COUNT},
         {"position_0", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_0},
         {"position_1", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_1},
         {"position_2", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_2},
         {"position_3", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_3},
         {"position_4", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_4},
         {"position_5", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_5},
         {"position_6", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_6},
         {"position_7", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_7},
         {"color_0", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_0},
         {"color_1", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_1},
         {"color_2", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_2},
         {"color_3", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_3},
         {"color_4", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_4},
         {"color_5", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_5},
         {"color_6", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_6},
         {"color_7", BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_7},
         }},
        {  BB_SHADER_NODE_TYPE_AMBIENT_OCCLUSION,
         {
         {"samples", BB_SHADER_INPUT_FIELD_AMBIENT_OCCLUSION_SAMPLES},
         {"inside", BB_SHADER_INPUT_FIELD_AMBIENT_OCCLUSION_INSIDE},
         {"only_local", BB_SHADER_INPUT_FIELD_AMBIENT_OCCLUSION_ONLY_LOCAL},
         }},
        {               BB_SHADER_NODE_TYPE_BUMP,
         {
         {"invert", BB_SHADER_INPUT_FIELD_BUMP_INVERT},
         }},
        {              BB_SHADER_NODE_TYPE_CLAMP,
         {
         {"clamp_type", BB_SHADER_INPUT_FIELD_CLAMP_CLAMP_TYPE},
         }},
        {       BB_SHADER_NODE_TYPE_DISPLACEMENT,
         {
         {"space", BB_SHADER_INPUT_FIELD_DISPLACEMENT_SPACE},
         }},
        {          BB_SHADER_NODE_TYPE_MAP_RANGE,
         {
         {"data_type", BB_SHADER_INPUT_FIELD_MAP_RANGE_DATA_TYPE},
         {"interpolation_type", BB_SHADER_INPUT_FIELD_MAP_RANGE_INTERPOLATION_TYPE},
         {"clamp", BB_SHADER_INPUT_FIELD_MAP_RANGE_CLAMP},
         }},
        {            BB_SHADER_NODE_TYPE_MAPPING,
         {
         {"vector_type", BB_SHADER_INPUT_FIELD_MAPPING_VECTOR_TYPE},
         }},
        {         BB_SHADER_NODE_TYPE_NORMAL_MAP,
         {
         {"space", BB_SHADER_INPUT_FIELD_NORMAL_MAP_SPACE},
         {"uv_map", BB_SHADER_INPUT_FIELD_NORMAL_MAP_UV_MAP},
         }},
        {            BB_SHADER_NODE_TYPE_TANGENT,
         {
         {"direction_type", BB_SHADER_INPUT_FIELD_TANGENT_DIRECTION_TYPE},
         {"axis", BB_SHADER_INPUT_FIELD_TANGENT_AXIS},
         {"uv_map", BB_SHADER_INPUT_FIELD_TANGENT_UV_MAP},
         }},
        {          BB_SHADER_NODE_TYPE_TEX_BRICK,
         {
         {"offset", BB_SHADER_INPUT_FIELD_TEX_BRICK_OFFSET},
         {"offset_frequency", BB_SHADER_INPUT_FIELD_TEX_BRICK_OFFSET_FREQUENCY},
         {"squash", BB_SHADER_INPUT_FIELD_TEX_BRICK_SQUASH},
         {"squash_frequency", BB_SHADER_INPUT_FIELD_TEX_BRICK_SQUASH_FREQUENCY},
         }},
        {    BB_SHADER_NODE_TYPE_TEX_ENVIRONMENT,
         {
         {"interpolation", BB_SHADER_INPUT_FIELD_TEX_ENVIRONMENT_INTERPOLATION},
         {"projection", BB_SHADER_INPUT_FIELD_TEX_ENVIRONMENT_PROJECTION},
         {"image", BB_SHADER_INPUT_FIELD_TEX_ENVIRONMENT_IMAGE},
         }},
        {          BB_SHADER_NODE_TYPE_TEX_GABOR,
         {
         {"gabor_type", BB_SHADER_INPUT_FIELD_TEX_GABOR_GABOR_TYPE},
         }},
        {       BB_SHADER_NODE_TYPE_TEX_GRADIENT,
         {
         {"gradient_type", BB_SHADER_INPUT_FIELD_TEX_GRADIENT_GRADIENT_TYPE},
         }},
        {          BB_SHADER_NODE_TYPE_TEX_MAGIC,
         {
         {"turbulence_depth", BB_SHADER_INPUT_FIELD_TEX_MAGIC_TURBULENCE_DEPTH},
         }},
        {        BB_SHADER_NODE_TYPE_TEX_VORONOI,
         {
         {"voronoi_dimensions", BB_SHADER_INPUT_FIELD_TEX_VORONOI_VORONOI_DIMENSIONS},
         {"feature", BB_SHADER_INPUT_FIELD_TEX_VORONOI_FEATURE},
         {"distance", BB_SHADER_INPUT_FIELD_TEX_VORONOI_DISTANCE},
         }},
        {           BB_SHADER_NODE_TYPE_TEX_WAVE,
         {
         {"wave_type", BB_SHADER_INPUT_FIELD_TEX_WAVE_WAVE_TYPE},
         {"bands_direction", BB_SHADER_INPUT_FIELD_TEX_WAVE_BANDS_DIRECTION},
         {"rings_direction", BB_SHADER_INPUT_FIELD_TEX_WAVE_RINGS_DIRECTION},
         {"wave_profile", BB_SHADER_INPUT_FIELD_TEX_WAVE_WAVE_PROFILE},
         }},
        {    BB_SHADER_NODE_TYPE_TEX_WHITE_NOISE,
         {
         {"noise_dimensions", BB_SHADER_INPUT_FIELD_TEX_WHITE_NOISE_NOISE_DIMENSIONS},
         }},
        {BB_SHADER_NODE_TYPE_VECTOR_DISPLACEMENT,
         {
         {"space", BB_SHADER_INPUT_FIELD_VECTOR_DISPLACEMENT_SPACE},
         }},
        {      BB_SHADER_NODE_TYPE_VECTOR_ROTATE,
         {
         {"rotation_type", BB_SHADER_INPUT_FIELD_VECTOR_ROTATE_ROTATION_TYPE},
         {"invert", BB_SHADER_INPUT_FIELD_VECTOR_ROTATE_INVERT},
         }},
        {   BB_SHADER_NODE_TYPE_VECTOR_TRANSFORM,
         {
         {"vector_type", BB_SHADER_INPUT_FIELD_VECTOR_TRANSFORM_VECTOR_TYPE},
         {"convert_from", BB_SHADER_INPUT_FIELD_VECTOR_TRANSFORM_CONVERT_FROM},
         {"convert_to", BB_SHADER_INPUT_FIELD_VECTOR_TRANSFORM_CONVERT_TO},
         }},
        {             BB_SHADER_NODE_TYPE_UV_MAP,
         {
         {"uv_map", BB_SHADER_INPUT_FIELD_UV_MAP_UV},
         }},
    };
}

// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
template <typename T> T lookup_field(const std::unordered_map<bb_shader_node_type_e, std::unordered_map<std::string, T>>& map, bb_shader_node_type_e node_type, const std::string& id)
{
    auto type_it = map.find(node_type);
    if (type_it != map.end())
    {
        auto field_it = type_it->second.find(id);
        if (field_it != type_it->second.end())
        {
            return field_it->second;
        }
    }
    CLOG_FATAL(&LOG, "Unknown field '%s' for node type %d", id.c_str(), (int)node_type);
    __builtin_unreachable();
}

// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
static void dbg_data(const data_t& data)
{
    CLOG_DEBUG(&LOG, "=== DATA DUMP ===");

    // Scenes
    {
        CLOG_DEBUG(&LOG, "SCENES (%ld):", data.scenes.size());
        for (const auto& scene : data.scenes)
        {
            CLOG_DEBUG(&LOG, "  scene: %s", scene.name.c_str());
            for (auto elem_item : scene.elems.items())
            {
                const scene_elem_t& elem = elem_item.value;
                CLOG_DEBUG(&LOG, "    elem: %s", elem.name.c_str());
                if (elem.parent_name.has_value())
                {
                    CLOG_DEBUG(&LOG, "      parent: %s", elem.parent_name.value().c_str());
                }
                CLOG_DEBUG(&LOG, "      pos: (%.3f, %.3f, %.3f)", elem.pos.x, elem.pos.y, elem.pos.z);
                CLOG_DEBUG(&LOG, "      rot: (%.3f, %.3f, %.3f)", elem.euler_rot.x, elem.euler_rot.y, elem.euler_rot.z);
                CLOG_DEBUG(&LOG, "      scale: (%.3f, %.3f, %.3f)", elem.scale.x, elem.scale.y, elem.scale.z);
                if (elem.mesh_name.has_value())
                {
                    CLOG_DEBUG(&LOG, "      mesh: %s", elem.mesh_name.value().c_str());
                }
                if (elem.skel_name.has_value())
                {
                    CLOG_DEBUG(&LOG, "      skel: %s", elem.skel_name.value().c_str());
                }
            }
        }
    }

    // Meshes
    {
        CLOG_DEBUG(&LOG, "MESHES (%ld):", data.meshes.size());
        for (auto mesh_item : data.meshes.items())
        {
            const mesh_t& mesh = mesh_item.value;
            size_t total_tris  = 0;
            for (const auto& submesh : mesh.submeshes)
            {
                total_tris += submesh.triangles.size();
            }
            CLOG_DEBUG(&LOG, "  mesh: %s (%ld submeshes, %zu triangles)", mesh.name.c_str(), mesh.submeshes.size(), total_tris);
            for (size_t i = 0; i < mesh.submeshes.size(); i++)
            {
                const auto& submesh = mesh.submeshes[i];
                CLOG_DEBUG(&LOG, "    submesh[%zu]: material=%s, tris=%ld", i, submesh.material_name.c_str(), submesh.triangles.size());
            }
        }
    }

    // Images
    {
        CLOG_DEBUG(&LOG, "IMAGES (%ld):", data.images.size());
        for (auto image_item : data.images.items())
        {
            const image_t& image = image_item.value;
            CLOG_DEBUG(&LOG, "  image: %s (%ux%u, srgb=%s, %ld bytes)", image.name.c_str(), image.width, image.height, image.is_srgb ? "true" : "false", image.rgba_data.size());
        }
    }

    // Materials
    {
        CLOG_DEBUG(&LOG, "MATERIALS (%ld):", data.materials.size());
        for (auto mat_item : data.materials.items())
        {
            const material_t& mat = mat_item.value;
            CLOG_DEBUG(&LOG, "  material: %s (%ld nodes, %ld links)", mat.name.c_str(), mat.nodes.size(), mat.links.size());
            for (size_t i = 0; i < mat.nodes.size(); i++)
            {
                const auto& node = mat.nodes[i];
                CLOG_DEBUG(&LOG, "    node[%zu]: %s [%s] (inputs=%ld, outputs=%ld, props=%ld)", i, node.name.c_str(), node.idname.c_str(), node.inputs.size(), node.outputs.size(), node.props.size());
                auto format_default_value = [](const std::optional<socket_default_value_t>& val) -> std::string
                {
                    if (!val.has_value()) return "";
                    return std::visit(
                        [](auto&& v) -> std::string
                        {
                            using T = std::decay_t<decltype(v)>;
                            if constexpr (std::is_same_v<T, socket_value_float_t>) return " = " + std::to_string(v.value);
                            else if constexpr (std::is_same_v<T, socket_value_int_t>) return " = " + std::to_string(v.value);
                            else if constexpr (std::is_same_v<T, socket_value_bool_t>) return std::string(" = ") + (v.value ? "true" : "false");
                            else if constexpr (std::is_same_v<T, socket_value_vector_t>) return " = (" + std::to_string(v.value.x) + ", " + std::to_string(v.value.y) + ", " + std::to_string(v.value.z) + ")";
                            else if constexpr (std::is_same_v<T, socket_value_rgba_t>) return " = (" + std::to_string(v.value.x) + ", " + std::to_string(v.value.y) + ", " + std::to_string(v.value.z) + ", " + std::to_string(v.value.w) + ")";
                            else if constexpr (std::is_same_v<T, socket_value_tex_t>) return " = \"" + v.value + "\"";
                            else if constexpr (std::is_same_v<T, socket_value_uv_map_t>) return " = \"" + v.value + "\"";
                            else return "";
                        },
                        val.value()
                    );
                };
                for (const auto& input : node.inputs)
                {
                    CLOG_DEBUG(&LOG, "      in: %s (%s)%s", input.identifier.c_str(), input.idname.c_str(), format_default_value(input.default_value).c_str());
                }
                for (const auto& output : node.outputs)
                {
                    CLOG_DEBUG(&LOG, "      out: %s (%s)%s", output.identifier.c_str(), output.idname.c_str(), format_default_value(output.default_value).c_str());
                }
                for (const auto& prop : node.props)
                {
                    std::string value_str = std::visit(
                        [](auto&& v) -> std::string
                        {
                            using T = std::decay_t<decltype(v)>;
                            if constexpr (std::is_same_v<T, socket_value_float_t>) return std::to_string(v.value);
                            else if constexpr (std::is_same_v<T, socket_value_int_t>) return std::to_string(v.value);
                            else if constexpr (std::is_same_v<T, socket_value_bool_t>) return v.value ? "true" : "false";
                            else if constexpr (std::is_same_v<T, socket_value_vector_t>) return "(" + std::to_string(v.value.x) + ", " + std::to_string(v.value.y) + ", " + std::to_string(v.value.z) + ")";
                            else if constexpr (std::is_same_v<T, socket_value_rgba_t>) return "(" + std::to_string(v.value.x) + ", " + std::to_string(v.value.y) + ", " + std::to_string(v.value.z) + ", " + std::to_string(v.value.w) + ")";
                            else if constexpr (std::is_same_v<T, socket_value_tex_t>) return "\"" + v.value + "\"";
                            else if constexpr (std::is_same_v<T, socket_value_uv_map_t>) return "\"" + v.value + "\"";
                            else return "";
                        },
                        prop.value
                    );
                    CLOG_DEBUG(&LOG, "      prop: %s = %s", prop.identifier.c_str(), value_str.c_str());
                }
            }
            for (const auto& link : mat.links)
            {
                CLOG_DEBUG(&LOG, "    link: %s.%s -> %s.%s", link.from_node.c_str(), link.from_socket.c_str(), link.to_node.c_str(), link.to_socket.c_str());
            }
        }
    }

    // Skeletons
    {
        CLOG_DEBUG(&LOG, "SKELETONS (%ld):", data.skeletons.size());
        for (auto skel_item : data.skeletons.items())
        {
            const skeleton_t& skel = skel_item.value;
            CLOG_DEBUG(&LOG, "  skeleton: %s (%ld bones)", skel.name.c_str(), skel.bones.size());
            for (size_t i = 0; i < skel.bones.size(); i++)
            {
                const auto& bone = skel.bones[i];
                CLOG_DEBUG(&LOG, "    bone[%zu]: %s (parent=%d)", i, bone.name.c_str(), bone.parent_index);
                CLOG_DEBUG(&LOG, "      bind_local: [%.4f, %.4f, %.4f, %.4f]", bone.bind_local_matrix[0], bone.bind_local_matrix[1], bone.bind_local_matrix[2], bone.bind_local_matrix[3]);
                CLOG_DEBUG(&LOG, "                  [%.4f, %.4f, %.4f, %.4f]", bone.bind_local_matrix[4], bone.bind_local_matrix[5], bone.bind_local_matrix[6], bone.bind_local_matrix[7]);
                CLOG_DEBUG(&LOG, "                  [%.4f, %.4f, %.4f, %.4f]", bone.bind_local_matrix[8], bone.bind_local_matrix[9], bone.bind_local_matrix[10], bone.bind_local_matrix[11]);
                CLOG_DEBUG(&LOG, "                  [%.4f, %.4f, %.4f, %.4f]", bone.bind_local_matrix[12], bone.bind_local_matrix[13], bone.bind_local_matrix[14], bone.bind_local_matrix[15]);
                CLOG_DEBUG(&LOG, "      inv_bind:   [%.4f, %.4f, %.4f, %.4f]", bone.inverse_bind_matrix[0], bone.inverse_bind_matrix[1], bone.inverse_bind_matrix[2], bone.inverse_bind_matrix[3]);
                CLOG_DEBUG(&LOG, "                  [%.4f, %.4f, %.4f, %.4f]", bone.inverse_bind_matrix[4], bone.inverse_bind_matrix[5], bone.inverse_bind_matrix[6], bone.inverse_bind_matrix[7]);
                CLOG_DEBUG(&LOG, "                  [%.4f, %.4f, %.4f, %.4f]", bone.inverse_bind_matrix[8], bone.inverse_bind_matrix[9], bone.inverse_bind_matrix[10], bone.inverse_bind_matrix[11]);
                CLOG_DEBUG(&LOG, "                  [%.4f, %.4f, %.4f, %.4f]", bone.inverse_bind_matrix[12], bone.inverse_bind_matrix[13], bone.inverse_bind_matrix[14], bone.inverse_bind_matrix[15]);
            }
        }
    }

    // Animations
    {
        CLOG_DEBUG(&LOG, "ANIMATIONS (%ld):", data.animations.size());
        for (const auto& anim : data.animations)
        {
            CLOG_DEBUG(&LOG, "  animation: %s (armature=%s, %ld channels)", anim.name.c_str(), anim.armature_name.c_str(), anim.channels.size());
            for (size_t i = 0; i < anim.channels.size(); i++)
            {
                const auto& channel  = anim.channels[i];
                const char* type_str = channel.channel_type == anim_channel_type_t::LOCATION ? "location" : (channel.channel_type == anim_channel_type_t::ROTATION ? "rotation" : "scale");
                CLOG_DEBUG(&LOG, "    channel[%zu]: bone=%u, type=%s, keyframes=%ld", i, channel.bone_index, type_str, channel.keyframe_times.size());
                if (!channel.keyframe_times.is_empty())
                {
                    const auto& first_val = channel.keyframe_values.first();
                    const auto& last_val  = channel.keyframe_values.last();
                    CLOG_DEBUG(&LOG, "      first (t=%.3f): [%.4f, %.4f, %.4f, %.4f]", channel.keyframe_times.first(), first_val[0], first_val[1], first_val[2], first_val[3]);
                    CLOG_DEBUG(&LOG, "      last  (t=%.3f): [%.4f, %.4f, %.4f, %.4f]", channel.keyframe_times.last(), last_val[0], last_val[1], last_val[2], last_val[3]);
                }
            }
        }
    }

    CLOG_DEBUG(&LOG, "=== END DATA DUMP ===");
}

// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
struct socket_endpoint_t
{
    std::string node_name;
    std::string socket_id;
};

// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
static void recurs_flatten_node_tree(bNodeTree* tree, const std::string& prefix, material_t& material_data, const blender::Map<std::string, socket_endpoint_t>& input_mappings, blender::Map<std::string, socket_endpoint_t>& output_mappings)
{
    if (tree == nullptr) return;

    // Ensure topology cache is available for logically_linked_sockets()
    tree->ensure_topology_cache();

    // Helper to get the logical source for an input socket (resolves through reroutes automatically)
    auto get_logical_source = [](const bNodeSocket* input_sock) -> std::pair<const bNode*, const bNodeSocket*>
    {
        blender::Span<const bNodeSocket*> sources = input_sock->logically_linked_sockets();
        if (sources.is_empty())
        {
            return {nullptr, nullptr};
        }
        const bNodeSocket* source_sock = sources[0];
        return {&source_sock->owner_node(), source_sock};
    };

    // First pass: identify group nodes, build their io mappings, collect internal links for resolving group io
    blender::Map<std::string, bNodeTree*> group_trees;
    blender::Map<std::string, blender::Map<std::string, socket_endpoint_t>> group_input_maps;
    blender::Map<std::string, blender::Map<std::string, socket_endpoint_t>> group_output_maps;
    for (bNode* node : tree->group_nodes())
    {
        if (node->id == nullptr)
        {
            continue;
        }

        bNodeTree* group_tree = (bNodeTree*)node->id;
        group_trees.add(node->name, group_tree);

        // Ensure topology cache for the group tree to use all_links()
        group_tree->ensure_topology_cache();
        for (const bNodeLink* link : group_tree->all_links())
        {
            if (!link->fromnode || !link->tonode || !link->fromsock || !link->tosock) continue;
            if (link->fromnode->is_group_input()) group_input_maps.lookup_or_add_default(node->name).add(link->fromsock->identifier, {prefix + node->name + "." + link->tonode->name, link->tosock->identifier});
            if (link->tonode->is_group_output()) group_output_maps.lookup_or_add_default(node->name).add(link->tosock->identifier, {prefix + node->name + "." + link->fromnode->name, link->fromsock->identifier});
        }
    }

    // Second pass: process all nodes in topological order (upstream nodes first)
    // This ensures group output mappings are available when processing downstream groups
    for (bNode* node : tree->toposort_left_to_right())
    {
        if (node->is_group_input() || node->is_group_output() || node->is_reroute() || node->is_frame())
        {
            continue;
        }
        else if (node->is_group() && node->id != nullptr)
        {
            blender::Map<std::string, socket_endpoint_t> nested_input_mappings;
            for (bNodeSocket* input_sock : node->input_sockets())
            {
                // Use topology cache to get logical source (handles reroutes automatically)
                auto [resolved_node, resolved_sock] = get_logical_source(input_sock);
                if (!resolved_node) continue;

                if (resolved_node->is_group())
                {
                    blender::Map<std::string, socket_endpoint_t>* src_output_map = group_output_maps.lookup_ptr(resolved_node->name);
                    if (src_output_map)
                    {
                        const socket_endpoint_t* endpoint = src_output_map->lookup_ptr(resolved_sock->identifier);
                        if (endpoint)
                        {
                            nested_input_mappings.add(input_sock->identifier, *endpoint);
                            continue;
                        }
                    }
                }
                else if (resolved_node->is_group_input())
                {
                    const socket_endpoint_t* endpoint = input_mappings.lookup_ptr(resolved_sock->identifier);
                    if (endpoint)
                    {
                        nested_input_mappings.add(input_sock->identifier, *endpoint);
                        continue;
                    }
                }

                std::string from_node_name = prefix + resolved_node->name;
                nested_input_mappings.add(input_sock->identifier, {from_node_name, std::string(resolved_sock->identifier)});
            }

            // Recursively flatten the group
            bNodeTree* group_tree    = (bNodeTree*)node->id;
            std::string group_prefix = prefix + node->name + ".";
            blender::Map<std::string, socket_endpoint_t> nested_output_mappings;
            recurs_flatten_node_tree(group_tree, group_prefix, material_data, nested_input_mappings, nested_output_mappings);

            // Store output mappings for link resolution
            group_output_maps.add_overwrite(node->name, nested_output_mappings);
        }
        else
        {
            auto extract_default_value = [](const bNodeSocket* socket) -> std::optional<socket_default_value_t>
            {
                if (socket->default_value == nullptr) return std::nullopt;

                // Use socket->type enum instead of string comparisons
                switch (socket->type)
                {
                    case SOCK_FLOAT:
                    {
                        const auto* val = socket->default_value_typed<bNodeSocketValueFloat>();
                        return socket_value_float_t{val->value};
                    }
                    case SOCK_INT:
                    {
                        const auto* val = socket->default_value_typed<bNodeSocketValueInt>();
                        return socket_value_int_t{val->value};
                    }
                    case SOCK_BOOLEAN:
                    {
                        const auto* val = socket->default_value_typed<bNodeSocketValueBoolean>();
                        return socket_value_bool_t{val->value != 0};
                    }
                    case SOCK_VECTOR:
                    {
                        const auto* val = socket->default_value_typed<bNodeSocketValueVector>();
                        return socket_value_vector_t{blender::float3(val->value[0], val->value[1], val->value[2])};
                    }
                    case SOCK_RGBA:
                    {
                        const auto* val = socket->default_value_typed<bNodeSocketValueRGBA>();
                        return socket_value_rgba_t{blender::float4(val->value[0], val->value[1], val->value[2], val->value[3])};
                    }
                    default: return std::nullopt;
                }
            };

            shader_node_t node_data;
            node_data.name   = prefix + node->name;
            node_data.idname = node->idname;

            // Input sockets - use input_sockets() instead of ListBaseWrapper
            for (bNodeSocket* socket : node->input_sockets())
            {
                shader_node_socket_t socket_data;
                socket_data.identifier    = socket->identifier;
                socket_data.idname        = socket->idname;
                socket_data.default_value = extract_default_value(socket);
                node_data.inputs.append(socket_data);
            }

            // Output sockets - use output_sockets() instead of ListBaseWrapper
            for (bNodeSocket* socket : node->output_sockets())
            {
                shader_node_socket_t socket_data;
                socket_data.identifier    = socket->identifier;
                socket_data.idname        = socket->idname;
                socket_data.default_value = extract_default_value(socket);
                node_data.outputs.append(socket_data);
            }

            // Node properties
            auto add_int_prop    = [&](const char* id, int val) { node_data.props.append({id, socket_value_int_t{val}}); };
            auto add_bool_prop   = [&](const char* id, bool val) { node_data.props.append({id, socket_value_bool_t{val}}); };
            auto add_float_prop  = [&](const char* id, float val) { node_data.props.append({id, socket_value_float_t{val}}); };
            auto add_tex_prop    = [&](const char* id, const std::string& val) { node_data.props.append({id, socket_value_tex_t{val}}); };
            auto add_uv_map_prop = [&](const char* id, const std::string& val) { node_data.props.append({id, socket_value_uv_map_t{val}}); };
            auto add_rgba_prop   = [&](const char* id, float r, float g, float b, float a) { node_data.props.append({id, socket_value_rgba_t{blender::float4(r, g, b, a)}}); };

            if (node->is_type("ShaderNodeUVMap"))
            {
                NodeShaderUVMap* data = (NodeShaderUVMap*)node->storage;
                if (data) add_uv_map_prop("uv_map", data->uv_map);
            }
            else if (node->is_type("ShaderNodeMath"))
            {
                add_int_prop("operation", node->custom1);
                add_bool_prop("use_clamp", node->custom2 != 0);
            }
            else if (node->is_type("ShaderNodeVectorMath"))
            {
                add_int_prop("operation", node->custom1);
            }
            else if (node->is_type("ShaderNodeMix"))
            {
                NodeShaderMix* data = (NodeShaderMix*)node->storage;
                if (data)
                {
                    add_int_prop("blend_type", data->blend_type);
                    add_bool_prop("clamp_factor", data->clamp_factor != 0);
                    add_bool_prop("clamp_result", data->clamp_result != 0);
                    add_int_prop("data_type", data->data_type);
                    add_int_prop("factor_mode", data->factor_mode);
                }
            }
            else if (node->is_type("ShaderNodeSeparateColor") || node->is_type("ShaderNodeCombineColor"))
            {
                NodeCombSepColor* data = (NodeCombSepColor*)node->storage;
                if (data) add_int_prop("mode", data->mode);
            }
            else if (node->is_type("ShaderNodeTexNoise"))
            {
                NodeTexNoise* data = (NodeTexNoise*)node->storage;
                if (data)
                {
                    add_int_prop("noise_dimensions", data->dimensions);
                    add_int_prop("noise_type", data->type);
                    add_bool_prop("normalize", data->normalize != 0);
                }
            }
            else if (node->is_type("ShaderNodeTexImage"))
            {
                if (node->id != nullptr)
                {
                    Image* ima = (Image*)node->id;
                    add_tex_prop("image", ima->id.name);
                }
                NodeTexImage* data = (NodeTexImage*)node->storage;
                if (data)
                {
                    add_int_prop("interpolation", data->interpolation);
                    add_int_prop("projection", data->projection);
                    add_int_prop("extension", data->extension);
                }
            }
            else if (node->is_type("ShaderNodeValToRGB"))
            {
                ColorBand* coba = (ColorBand*)node->storage;
                if (coba)
                {
                    add_int_prop("interpolation", coba->ipotype);
                    add_int_prop("element_count", coba->tot);
                    for (int i = 0; i < coba->tot && i < 8; i++)
                    {
                        add_float_prop(("position_" + std::to_string(i)).c_str(), coba->data[i].pos);
                        add_rgba_prop(("color_" + std::to_string(i)).c_str(), coba->data[i].r, coba->data[i].g, coba->data[i].b, coba->data[i].a);
                    }
                }
            }
            else if (node->is_type("ShaderNodeAmbientOcclusion"))
            {
                add_int_prop("samples", node->custom1);
                add_bool_prop("inside", (node->custom2 & SHD_AO_INSIDE) != 0);
                add_bool_prop("only_local", (node->custom2 & SHD_AO_LOCAL) != 0);
            }
            else if (node->is_type("ShaderNodeBump"))
            {
                add_bool_prop("invert", node->custom1 != 0);
            }
            else if (node->is_type("ShaderNodeClamp"))
            {
                add_int_prop("clamp_type", node->custom1);
            }
            else if (node->is_type("ShaderNodeDisplacement"))
            {
                add_int_prop("space", node->custom1);
            }
            else if (node->is_type("ShaderNodeMapRange"))
            {
                NodeMapRange* data = (NodeMapRange*)node->storage;
                if (data)
                {
                    add_int_prop("data_type", data->data_type);
                    add_int_prop("interpolation_type", data->interpolation_type);
                    add_bool_prop("clamp", data->clamp != 0);
                }
            }
            else if (node->is_type("ShaderNodeMapping"))
            {
                add_int_prop("vector_type", node->custom1);
            }
            else if (node->is_type("ShaderNodeNormalMap"))
            {
                NodeShaderNormalMap* data = (NodeShaderNormalMap*)node->storage;
                if (data)
                {
                    add_int_prop("space", data->space);
                    add_uv_map_prop("uv_map", data->uv_map);
                }
            }
            else if (node->is_type("ShaderNodeTangent"))
            {
                NodeShaderTangent* data = (NodeShaderTangent*)node->storage;
                if (data)
                {
                    add_int_prop("direction_type", data->direction_type);
                    add_int_prop("axis", data->axis);
                    add_uv_map_prop("uv_map", data->uv_map);
                }
            }
            else if (node->is_type("ShaderNodeTexBrick"))
            {
                NodeTexBrick* data = (NodeTexBrick*)node->storage;
                if (data)
                {
                    add_float_prop("offset", data->offset);
                    add_int_prop("offset_frequency", data->offset_freq);
                    add_float_prop("squash", data->squash);
                    add_int_prop("squash_frequency", data->squash_freq);
                }
            }
            else if (node->is_type("ShaderNodeTexEnvironment"))
            {
                if (node->id != nullptr)
                {
                    Image* ima = (Image*)node->id;
                    add_tex_prop("image", ima->id.name);
                }
                NodeTexEnvironment* data = (NodeTexEnvironment*)node->storage;
                if (data)
                {
                    add_int_prop("interpolation", data->interpolation);
                    add_int_prop("projection", data->projection);
                }
            }
            else if (node->is_type("ShaderNodeTexGabor"))
            {
                NodeTexGabor* data = (NodeTexGabor*)node->storage;
                if (data)
                {
                    add_int_prop("gabor_type", data->type);
                }
            }
            else if (node->is_type("ShaderNodeTexGradient"))
            {
                NodeTexGradient* data = (NodeTexGradient*)node->storage;
                if (data)
                {
                    add_int_prop("gradient_type", data->gradient_type);
                }
            }
            else if (node->is_type("ShaderNodeTexMagic"))
            {
                NodeTexMagic* data = (NodeTexMagic*)node->storage;
                if (data)
                {
                    add_int_prop("turbulence_depth", data->depth);
                }
            }
            else if (node->is_type("ShaderNodeTexVoronoi"))
            {
                NodeTexVoronoi* data = (NodeTexVoronoi*)node->storage;
                if (data)
                {
                    add_int_prop("voronoi_dimensions", data->dimensions);
                    add_int_prop("feature", data->feature);
                    add_int_prop("distance", data->distance);
                }
            }
            else if (node->is_type("ShaderNodeTexWave"))
            {
                NodeTexWave* data = (NodeTexWave*)node->storage;
                if (data)
                {
                    add_int_prop("wave_type", data->wave_type);
                    add_int_prop("bands_direction", data->bands_direction);
                    add_int_prop("rings_direction", data->rings_direction);
                    add_int_prop("wave_profile", data->wave_profile);
                }
            }
            else if (node->is_type("ShaderNodeTexWhiteNoise"))
            {
                add_int_prop("noise_dimensions", node->custom1);
            }
            else if (node->is_type("ShaderNodeVectorDisplacement"))
            {
                add_int_prop("space", node->custom1);
            }
            else if (node->is_type("ShaderNodeVectorRotate"))
            {
                add_int_prop("rotation_type", node->custom1);
                add_bool_prop("invert", node->custom2 != 0);
            }
            else if (node->is_type("ShaderNodeVectorTransform"))
            {
                NodeShaderVectTransform* data = (NodeShaderVectTransform*)node->storage;
                if (data)
                {
                    add_int_prop("vector_type", data->type);
                    add_int_prop("convert_from", data->convert_from);
                    add_int_prop("convert_to", data->convert_to);
                }
            }

            material_data.nodes.append(node_data);
        }
    }

    // Third pass: process links and resolve group boundaries using topology cache

    // Handle group output node - build output_mappings
    if (bNode* group_output = tree->group_output_node())
    {
        for (bNodeSocket* input_sock : group_output->input_sockets())
        {
            auto [resolved_node, resolved_sock] = get_logical_source(input_sock);
            if (!resolved_node) continue;

            std::string from_node_name = prefix + resolved_node->name;
            std::string from_socket    = std::string(resolved_sock->identifier);

            if (resolved_node->is_group())
            {
                blender::Map<std::string, socket_endpoint_t>* src_output_map = group_output_maps.lookup_ptr(resolved_node->name);
                if (src_output_map)
                {
                    const socket_endpoint_t* endpoint = src_output_map->lookup_ptr(resolved_sock->identifier);
                    if (endpoint)
                    {
                        from_node_name = endpoint->node_name;
                        from_socket    = endpoint->socket_id;
                    }
                }
            }
            else if (resolved_node->is_group_input())
            {
                // Handle GroupInput -> Reroute -> GroupOutput (pass-through)
                const socket_endpoint_t* endpoint = input_mappings.lookup_ptr(resolved_sock->identifier);
                if (endpoint)
                {
                    from_node_name = endpoint->node_name;
                    from_socket    = endpoint->socket_id;
                }
            }

            output_mappings.add(input_sock->identifier, {from_node_name, from_socket});
        }
    }

    // Process regular nodes and build links
    for (bNode* node : tree->all_nodes())
    {
        // Skip reroutes, group input/output, groups, and frames - they're handled separately or are organizational
        if (node->is_reroute() || node->is_group_input() || node->is_group_output() || node->is_group() || node->is_frame())
        {
            continue;
        }

        // For regular nodes, create links from their logical sources
        for (bNodeSocket* input_sock : node->input_sockets())
        {
            auto [resolved_node, resolved_sock] = get_logical_source(input_sock);
            if (!resolved_node) continue;

            if (resolved_node->is_group())
            {
                blender::Map<std::string, socket_endpoint_t>* src_output_map = group_output_maps.lookup_ptr(resolved_node->name);
                if (src_output_map)
                {
                    const socket_endpoint_t* endpoint = src_output_map->lookup_ptr(resolved_sock->identifier);
                    if (endpoint)
                    {
                        shader_link_t link_data;
                        link_data.from_node   = endpoint->node_name;
                        link_data.from_socket = endpoint->socket_id;
                        link_data.to_node     = prefix + node->name;
                        link_data.to_socket   = input_sock->identifier;
                        material_data.links.append(link_data);
                    }
                }
                continue;
            }

            if (resolved_node->is_group_input())
            {
                const socket_endpoint_t* endpoint = input_mappings.lookup_ptr(resolved_sock->identifier);
                if (endpoint)
                {
                    shader_link_t link_data;
                    link_data.from_node   = endpoint->node_name;
                    link_data.from_socket = endpoint->socket_id;
                    link_data.to_node     = prefix + node->name;
                    link_data.to_socket   = input_sock->identifier;
                    material_data.links.append(link_data);
                }
                continue;
            }

            shader_link_t link_data;
            link_data.from_node   = prefix + resolved_node->name;
            link_data.from_socket = std::string(resolved_sock->identifier);
            link_data.to_node     = prefix + node->name;
            link_data.to_socket   = input_sock->identifier;
            material_data.links.append(link_data);
        }
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
void zachary_main(const struct bContext* C, const char* bb_archive_output_dir)
{
    // ===============================================================================================
    // ===============================================================================================
    // ===============================================================================================
    // ===============================================================================================
    Main* bmain;
    {
        if ((bmain = CTX_data_main(C)) == nullptr)
        {
            CLOG_ERROR(&LOG, "No Main database available");
            return;
        }
    }

    // ===============================================================================================
    // ===============================================================================================
    // ===============================================================================================
    // ===============================================================================================
    CLOG_INFO(&LOG, "zachary_main()");

    // ===============================================================================================
    // ===============================================================================================
    // ===============================================================================================
    // ===============================================================================================
    data_t data;

    // ===============================================================================================
    // ===============================================================================================
    // materials
    // - ShaderNodeGroup nodes are recursively flattened out
    // - node names are unique, uses dot separation for nested groups
    // ===============================================================================================
    // ===============================================================================================
    {
        LISTBASE_FOREACH(Material*, mat, &bmain->materials)
        {
            if (mat->nodetree == nullptr) continue;

            material_t material_data;
            material_data.name = mat->id.name;

            // Flatten tree
            {
                blender::Map<std::string, socket_endpoint_t> input_mappings;
                blender::Map<std::string, socket_endpoint_t> output_mappings;

                recurs_flatten_node_tree(mat->nodetree, "", material_data, input_mappings, output_mappings);
            }

            // Find Principled BSDF node, skip material if not found
            std::optional<std::string> principled_node_name;
            {
                for (const auto& node : material_data.nodes)
                {
                    if (node.idname == "ShaderNodeBsdfPrincipled")
                    {
                        principled_node_name = node.name;
                        break;
                    }
                }
                if (!principled_node_name.has_value())
                {
                    CLOG_INFO(&LOG, "Skipping material %s (no Principled BSDF)", mat->id.name);
                    continue;
                }
            }

            // Build reverse adjacency: for each node, which nodes feed into it
            blender::Map<std::string, blender::Vector<std::string>> reverse_adj;
            for (const auto& link : material_data.links)
            {
                reverse_adj.lookup_or_add_default(link.to_node).append(link.from_node);
            }

            // BFS backwards from supported Principled BSDF sockets only
            blender::Set<std::string> reachable_nodes;
            {
                reachable_nodes.add(principled_node_name.value());

                blender::Stack<std::string> stack;
                for (const auto& link : material_data.links)
                {
                    if (link.to_node == principled_node_name.value() && supported_sockets.count(link.to_socket))
                    {
                        if (reachable_nodes.add(link.from_node))
                        {
                            stack.push(link.from_node);
                        }
                    }
                }

                while (!stack.is_empty())
                {
                    std::string current = stack.pop();
                    if (const blender::Vector<std::string>* preds = reverse_adj.lookup_ptr(current))
                    {
                        for (const auto& pred : *preds)
                        {
                            if (reachable_nodes.add(pred))
                            {
                                stack.push(pred);
                            }
                        }
                    }
                }
            }

            // Filter nodes to only those reachable
            blender::Vector<shader_node_t> filtered_nodes;
            for (const auto& node : material_data.nodes)
            {
                if (reachable_nodes.contains(node.name))
                {
                    filtered_nodes.append(node);
                }
            }

            // Build set of actual node names from filtered_nodes
            blender::Set<std::string> filtered_node_names;
            for (const auto& node : filtered_nodes)
            {
                filtered_node_names.add(node.name);
            }

            // Filter links to only those between actual filtered nodes, and only supported sockets for Principled BSDF
            blender::Vector<shader_link_t> filtered_links;
            for (const auto& link : material_data.links)
            {
                // Must exist in actual filtered nodes (not just reachable_nodes which is from link names)
                if (!filtered_node_names.contains(link.from_node) || !filtered_node_names.contains(link.to_node))
                {
                    continue;
                }
                // For links going to Principled BSDF, only keep supported sockets
                if (link.to_node == principled_node_name.value() && !supported_sockets.count(link.to_socket))
                {
                    continue;
                }
                filtered_links.append(link);
            }

            bool is_supported = true;
            {
                for (const auto& node : filtered_nodes)
                {
                    if (supported_node_outputs.find(node.idname) == supported_node_outputs.end())
                    {
                        CLOG_INFO(&LOG, "Skipping material %s (unsupported node type: %s)", mat->id.name, node.idname.c_str());
                        is_supported = false;
                        break;
                    }
                }

                if (is_supported)
                {
                    blender::Map<std::string, std::string> node_name_to_idname;
                    for (const auto& node : filtered_nodes)
                    {
                        node_name_to_idname.add(node.name, node.idname);
                    }

                    for (const auto& link : filtered_links)
                    {
                        const std::string* idname_ptr = node_name_to_idname.lookup_ptr(link.from_node);
                        if (!idname_ptr)
                        {
                            // This should never happen - filtered_links should only contain links between filtered_nodes
                            CLOG_FATAL(&LOG, "BUG: Link from_node '%s' not in filtered_nodes for material %s", link.from_node.c_str(), mat->id.name);
                        }

                        const std::string& idname     = *idname_ptr;
                        const auto& supported_outputs = supported_node_outputs.at(idname);

                        if (supported_outputs.find(link.from_socket) == supported_outputs.end())
                        {
                            CLOG_INFO(&LOG, "Skipping material %s (unsupported output socket: %s.%s)", mat->id.name, idname.c_str(), link.from_socket.c_str());
                            is_supported = false;
                            break;
                        }
                    }
                }
            }

            if (!is_supported)
            {
                continue;
            }

            // Filter sockets to only supported ones
            for (auto& node : filtered_nodes)
            {
                // Filter node output sockets
                auto it = supported_node_outputs.find(node.idname);
                if (it != supported_node_outputs.end())
                {
                    const auto& allowed_outputs = it->second;
                    blender::Vector<shader_node_socket_t> new_outputs;
                    for (const auto& socket : node.outputs)
                    {
                        if (allowed_outputs.find(socket.identifier) != allowed_outputs.end())
                        {
                            new_outputs.append(socket);
                        }
                    }
                    node.outputs = new_outputs;
                }

                // Filter Principled BSDF input sockets
                if (node.idname == "ShaderNodeBsdfPrincipled")
                {
                    blender::Vector<shader_node_socket_t> new_inputs;
                    for (const auto& socket : node.inputs)
                    {
                        if (supported_sockets.find(socket.identifier) != supported_sockets.end())
                        {
                            new_inputs.append(socket);
                        }
                    }
                    node.inputs = new_inputs;
                }
            }

            // Topological sort: dependencies before dependents, Principled BSDF at the end
            {
                // Build adjacency list and in-degree count
                blender::Map<std::string, blender::Vector<std::string>> dependents;
                blender::Map<std::string, int> in_degree;

                // Initialize all nodes with in_degree 0
                for (const auto& node : filtered_nodes)
                {
                    in_degree.add(node.name, 0);
                }

                // Build graph from links
                for (const auto& link : filtered_links)
                {
                    dependents.lookup_or_add_default(link.from_node).append(link.to_node);
                    in_degree.lookup(link.to_node)++;
                }

                // Kahn's algorithm
                blender::Stack<std::string> stack;
                for (const auto item : in_degree.items())
                {
                    if (item.value == 0)
                    {
                        stack.push(item.key);
                    }
                }

                blender::Vector<std::string> sorted_order;
                while (!stack.is_empty())
                {
                    std::string current = stack.pop();
                    sorted_order.append(current);

                    if (const blender::Vector<std::string>* deps = dependents.lookup_ptr(current))
                    {
                        for (const auto& dependent : *deps)
                        {
                            int& deg = in_degree.lookup(dependent);
                            deg--;
                            if (deg == 0)
                            {
                                stack.push(dependent);
                            }
                        }
                    }
                }

                // Reorder filtered_nodes according to sorted_order
                blender::Map<std::string, shader_node_t> node_map;
                for (const auto& node : filtered_nodes)
                {
                    node_map.add(node.name, node);
                }

                filtered_nodes.clear();
                for (const auto& name : sorted_order)
                {
                    const shader_node_t* node_ptr = node_map.lookup_ptr(name);
                    if (node_ptr)
                    {
                        filtered_nodes.append(*node_ptr);
                    }
                }
            }

            material_data.nodes = filtered_nodes;
            material_data.links = filtered_links;

            data.materials.add(material_data.name, material_data);
        }
    }

    // ===============================================================================================
    // ===============================================================================================
    // meshes
    // ===============================================================================================
    // ===============================================================================================
    {
        LISTBASE_FOREACH(Mesh*, mesh, &bmain->meshes)
        {
            // Check for invalid material slots
            {
                if (mesh->totcol <= 0 || mesh->mat == nullptr)
                {
                    continue;
                }

                bool has_invalid_material = false;
                for (int i = 0; i < mesh->totcol; i++)
                {
                    if (mesh->mat[i] == nullptr)
                    {
                        CLOG_INFO(&LOG, "Skipping mesh %s (has empty material slot)", mesh->id.name);
                        has_invalid_material = true;
                        break;
                    }
                    std::string mat_name = mesh->mat[i]->id.name;
                    if (!data.materials.contains(mat_name))
                    {
                        CLOG_INFO(&LOG, "Skipping mesh %s (material %s not in map)", mesh->id.name, mat_name.c_str());
                        has_invalid_material = true;
                        break;
                    }
                }

                if (has_invalid_material)
                {
                    continue;
                }
            }

            // Find an object that uses this mesh to get vertex groups
            Object* mesh_obj = nullptr;
            LISTBASE_FOREACH(Object*, ob, &bmain->objects)
            {
                if (ob->type == OB_MESH && ob->data == mesh)
                {
                    mesh_obj = ob;
                    break;
                }
            }

            // Find the armature associated with this mesh (via parent)
            bArmature* armature = nullptr;
            blender::Map<blender::StringRef, int> bone_name_to_idx;
            if (mesh_obj != nullptr)
            {
                // Check if parent is an armature
                if (mesh_obj->parent != nullptr && mesh_obj->parent->type == OB_ARMATURE)
                {
                    armature = (bArmature*)mesh_obj->parent->data;
                }

                // Build bone name -> index map if we have an armature (using original names)
                if (armature != nullptr)
                {
                    int bone_idx                             = 0;
                    std::function<void(Bone*)> collect_bones = [&](Bone* bone)
                    {
                        bone_name_to_idx.add(bone->name, bone_idx++);
                        for (Bone* child : blender::ListBaseWrapper<Bone>(bone->childbase))
                        {
                            collect_bones(child);
                        }
                    };
                    for (Bone* bone : blender::ListBaseWrapper<Bone>(armature->bonebase))
                    {
                        collect_bones(bone);
                    }
                }
            }

            // Build vertex group index -> bone index map (vertex group names match bone names in Blender)
            blender::Map<int, int> vgroup_to_bone;
            if (armature != nullptr)
            {
                int vgroup_idx = 0;
                for (bDeformGroup* dg : blender::ListBaseWrapper<bDeformGroup>(mesh->vertex_group_names))
                {
                    const int* bone_idx = bone_name_to_idx.lookup_ptr(dg->name);
                    if (bone_idx)
                    {
                        vgroup_to_bone.add(vgroup_idx, *bone_idx);
                    }
                    vgroup_idx++;
                }
            }

            blender::Span<blender::float3> positions                = mesh->vert_positions();
            blender::Span<int> corner_verts                         = mesh->corner_verts();
            blender::Span<blender::int3> corner_tris                = mesh->corner_tris();
            blender::Span<int> corner_tri_faces                     = mesh->corner_tri_faces();
            blender::bke::AttributeAccessor attributes              = mesh->attributes();
            blender::VectorSet<blender::StringRefNull> uv_map_names = mesh->uv_map_names();
            blender::VArraySpan<blender::float2> uv_map0;
            blender::VArraySpan<blender::float2> uv_map1;
            blender::VArraySpan<blender::float2> uv_map2;
            if (uv_map_names.size() > 0) uv_map0 = *attributes.lookup<blender::float2>(uv_map_names[0], blender::bke::AttrDomain::Corner);
            if (uv_map_names.size() > 1) uv_map1 = *attributes.lookup<blender::float2>(uv_map_names[1], blender::bke::AttrDomain::Corner);
            if (uv_map_names.size() > 2) uv_map2 = *attributes.lookup<blender::float2>(uv_map_names[2], blender::bke::AttrDomain::Corner);

            // Get deform verts for skinning
            blender::Span<MDeformVert> deform_verts = mesh->deform_verts();
            bool has_skinning                       = !deform_verts.is_empty() && !vgroup_to_bone.is_empty();

            mesh_t mesh_data;
            mesh_data.name = mesh->id.name;

            // Group triangles by material index
            blender::Map<int, blender::Vector<int>> triangles_by_material;
            {
                const blender::VArray<int> material_indices_varray = *attributes.lookup_or_default<int>("material_index", blender::bke::AttrDomain::Face, 0);
                blender::VArraySpan<int> material_indices(material_indices_varray);

                for (int tri_idx = 0; tri_idx < corner_tris.size(); tri_idx++)
                {
                    int face_idx = corner_tri_faces[tri_idx];
                    int mat_idx  = material_indices[face_idx];
                    triangles_by_material.lookup_or_add_default(mat_idx).append(tri_idx);
                }
            }

            // Check for invalid material indices and skip entire mesh if found
            bool has_invalid_mat_idx = false;
            for (int mat_idx : triangles_by_material.keys())
            {
                if (mat_idx < 0 || mat_idx >= mesh->totcol || !mesh->mat[mat_idx])
                {
                    CLOG_INFO(&LOG, "Skipping mesh %s (face references invalid material index %d)", mesh->id.name, mat_idx);
                    has_invalid_mat_idx = true;
                    break;
                }
            }
            if (has_invalid_mat_idx)
            {
                continue;
            }

            // Create submeshes from grouped triangles
            for (auto tri_item : triangles_by_material.items())
            {
                int mat_idx                             = tri_item.key;
                const blender::Vector<int>& tri_indices = tri_item.value;
                submesh_t submesh;

                submesh.material_name = mesh->mat[mat_idx]->id.name;

                for (int tri_idx : tri_indices)
                {
                    blender::int3 tri   = corner_tris[tri_idx];
                    triangle_t triangle = {};
                    for (int i = 0; i < 3; i++)
                    {
                        int corner                      = tri[i];
                        int vert_idx                    = corner_verts[corner];

                        // Position
                        triangle.vertices[i].position.x = positions[vert_idx].x;
                        triangle.vertices[i].position.y = positions[vert_idx].y;
                        triangle.vertices[i].position.z = positions[vert_idx].z;

                        // UV
                        if (!uv_map0.is_empty() && corner < uv_map0.size())
                        {
                            triangle.vertices[i].uv0.x = uv_map0[corner].x;
                            triangle.vertices[i].uv0.y = uv_map0[corner].y;
                        }
                        if (!uv_map1.is_empty() && corner < uv_map1.size())
                        {
                            triangle.vertices[i].uv1.x = uv_map1[corner].x;
                            triangle.vertices[i].uv1.y = uv_map1[corner].y;
                        }
                        if (!uv_map2.is_empty() && corner < uv_map2.size())
                        {
                            triangle.vertices[i].uv2.x = uv_map2[corner].x;
                            triangle.vertices[i].uv2.y = uv_map2[corner].y;
                        }

                        // Joint indices and weights
                        if (has_skinning && vert_idx < deform_verts.size())
                        {
                            const MDeformVert& dvert = deform_verts[vert_idx];

                            // Collect all bone influences for this vertex
                            blender::Vector<std::pair<float, int>> influences;
                            for (int w = 0; w < dvert.totweight; w++)
                            {
                                const MDeformWeight& dw = dvert.dw[w];
                                const int* bone_idx     = vgroup_to_bone.lookup_ptr(dw.def_nr);
                                if (bone_idx)
                                {
                                    influences.append({dw.weight, *bone_idx});
                                }
                            }

                            // Sort by weight (descending) and take top 4
                            std::sort(influences.begin(), influences.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

                            // Fill in joint indices and weights (up to 4)
                            float total_weight = 0.0f;
                            for (int64_t j = 0; j < std::min(influences.size(), int64_t(4)); j++)
                            {
                                triangle.vertices[i].joint_indices[j]  = (uint8_t)influences[j].second;
                                triangle.vertices[i].joint_weights[j]  = influences[j].first;
                                total_weight                          += influences[j].first;
                            }

                            // Normalize weights
                            if (total_weight > 0.0f)
                            {
                                float inv_total = 1.0f / total_weight;
                                for (int j = 0; j < 4; j++)
                                {
                                    triangle.vertices[i].joint_weights[j] *= inv_total;
                                }
                            }
                        }
                    }
                    submesh.triangles.append(triangle);
                }
                mesh_data.submeshes.append(submesh);
            }

            data.meshes.add(mesh_data.name, mesh_data);
        }
    }

    // ===============================================================================================
    // ===============================================================================================
    // scenes
    // ===============================================================================================
    // ===============================================================================================
    {
        LISTBASE_FOREACH(Scene*, scene, &bmain->scenes)
        {
            scene_t scene_data;
            scene_data.name = scene->id.name;

            FOREACH_SCENE_OBJECT_BEGIN(scene, ob)
            {
                if (ob->data == nullptr) continue;

                std::optional<std::string> parent_name = ob->parent != nullptr ? (std::optional<std::string>)std::string(ob->parent->id.name) : std::nullopt;

                if (ob->type == OB_MESH)
                {
                    Mesh* mesh            = (Mesh*)ob->data;
                    std::string mesh_name = mesh->id.name;
                    if (!data.meshes.contains(mesh_name))
                    {
                        continue;
                    }

                    scene_elem_t elem = {};
                    elem.name         = ob->id.name;
                    elem.parent_name  = parent_name;
                    elem.pos.x        = ob->loc[0];
                    elem.pos.y        = ob->loc[1];
                    elem.pos.z        = ob->loc[2];
                    elem.euler_rot.x  = ob->rot[0];
                    elem.euler_rot.y  = ob->rot[1];
                    elem.euler_rot.z  = ob->rot[2];
                    elem.scale.x      = ob->scale[0];
                    elem.scale.y      = ob->scale[1];
                    elem.scale.z      = ob->scale[2];
                    elem.mesh_name    = mesh_name;

                    scene_data.elems.add(elem.name, elem);
                }
                else if (ob->type == OB_ARMATURE)
                {
                    bArmature* arm        = (bArmature*)ob->data;
                    std::string skel_name = arm->id.name;

                    scene_elem_t elem     = {};
                    elem.name             = ob->id.name;
                    elem.parent_name      = parent_name;
                    elem.pos.x            = ob->loc[0];
                    elem.pos.y            = ob->loc[1];
                    elem.pos.z            = ob->loc[2];
                    elem.euler_rot.x      = ob->rot[0];
                    elem.euler_rot.y      = ob->rot[1];
                    elem.euler_rot.z      = ob->rot[2];
                    elem.scale.x          = ob->scale[0];
                    elem.scale.y          = ob->scale[1];
                    elem.scale.z          = ob->scale[2];
                    elem.skel_name        = skel_name;

                    scene_data.elems.add(elem.name, elem);
                }
            }
            FOREACH_SCENE_OBJECT_END;

            data.scenes.append(scene_data);
        }
    }

    // ===============================================================================================
    // ===============================================================================================
    // images
    // ===============================================================================================
    // ===============================================================================================
    {
        LISTBASE_FOREACH(Image*, ima, &bmain->images)
        {
            if (!BKE_image_has_packedfile(ima))
            {
                continue;
            }

            for (ImagePackedFile* imapf : blender::ListBaseWrapper<ImagePackedFile>(ima->packedfiles))
            {
                if (imapf->packedfile == nullptr)
                {
                    continue;
                }

                int flag    = IB_byte_data | IB_metadata;
                ImBuf* ibuf = IMB_load_image_from_memory((const unsigned char*)imapf->packedfile->data, imapf->packedfile->size, flag, "<packed data>", nullptr, (char*)ima->colorspace_settings.name);

                if (ibuf == nullptr)
                {
                    continue;
                }

                // Ensure byte buffer exists (convert from float if needed)
                if (ibuf->byte_buffer.data == nullptr && ibuf->float_buffer.data != nullptr)
                {
                    if (!IMB_alloc_byte_pixels(ibuf, false))
                    {
                        IMB_freeImBuf(ibuf);
                        continue;
                    }
                    IMB_byte_from_float(ibuf);
                }

                if (ibuf->byte_buffer.data == nullptr)
                {
                    IMB_freeImBuf(ibuf);
                    continue;
                }

                size_t pixel_count = IMB_get_pixel_count(ibuf);
                size_t data_size   = pixel_count * 4;

                image_t image      = {};
                image.name         = ima->id.name;
                image.width        = ibuf->x;
                image.height       = ibuf->y;
                image.is_srgb      = IMB_colormanagement_space_is_srgb(ibuf->byte_buffer.colorspace);
                image.rgba_data.resize(data_size);
                memcpy(image.rgba_data.data(), ibuf->byte_buffer.data, data_size);

                IMB_freeImBuf(ibuf);
                data.images.add(image.name, image);
            }
        }
    }

    // ===============================================================================================
    // ===============================================================================================
    // skeletons
    // ===============================================================================================
    // ===============================================================================================
    {
        LISTBASE_FOREACH(bArmature*, arm, &bmain->armatures)
        {
            skeleton_t skel;
            skel.name = arm->id.name;

            // Build bone index map and collect bones in order (using original names)
            blender::Map<blender::StringRef, int> bone_name_to_index;
            blender::Vector<Bone*> bone_list;
            blender::Vector<blender::float4x4> inv_mats; // Store inverse matrices as float4x4

            // Recursive function to traverse bone hierarchy
            std::function<void(Bone*)> collect_bones = [&](Bone* bone)
            {
                int idx = (int)bone_list.size();
                bone_name_to_index.add(bone->name, idx);
                bone_list.append(bone);

                for (Bone* child : blender::ListBaseWrapper<Bone>(bone->childbase))
                {
                    collect_bones(child);
                }
            };

            // Start from root bones
            for (Bone* bone : blender::ListBaseWrapper<Bone>(arm->bonebase))
            {
                collect_bones(bone);
            }

            // Now process each bone
            for (size_t i = 0; i < bone_list.size(); i++)
            {
                Bone* bone = bone_list[i];
                bone_t bone_data;
                bone_data.name = bone->name; // Store original name, normalize during export

                // Parent index
                if (bone->parent)
                {
                    bone_data.parent_index = bone_name_to_index.lookup_default(bone->parent->name, -1);
                }
                else
                {
                    bone_data.parent_index = -1;
                }

                // matrix_local is the bone's rest pose transform in armature space (4x4)
                // We need:
                // - inverse_bind_matrix: inverse of matrix_local
                // - bind_local_matrix: local transform relative to parent

                // Compute inverse bind matrix using blender::math::invert()
                // bone->arm_mat is column-major, float4x4 is also column-major
                blender::float4x4 arm_mat;
                for (int c = 0; c < 4; c++)
                    for (int r = 0; r < 4; r++) arm_mat[c][r] = bone->arm_mat[c][r];

                bool invert_success;
                blender::float4x4 inv_mat = blender::math::invert(arm_mat, invert_success);

                // Store inverse matrix for later bind_local computation
                inv_mats.append(inv_mat);

                // Store inverse bind matrix (output in row-major, row-vector convention)
                // Transpose: output[r][c] = inv_mat[c][r]
                for (int r = 0; r < 4; r++)
                    for (int c = 0; c < 4; c++) bone_data.inverse_bind_matrix[r * 4 + c] = inv_mat[r][c];

                // Compute bind_local_matrix: local transform relative to parent
                // bind_local = parent_inverse @ bone_matrix_local
                // For root bones, bind_local = bone_matrix_local
                if (bone_data.parent_index == -1)
                {
                    // Root bone: bind_local = matrix_local
                    for (int r = 0; r < 4; r++)
                        for (int c = 0; c < 4; c++) bone_data.bind_local_matrix[r * 4 + c] = bone->arm_mat[r][c];
                }
                else
                {
                    // bind_local = parent_inv_mat @ arm_mat using float4x4 multiplication
                    const blender::float4x4& parent_inv = inv_mats[bone_data.parent_index];
                    blender::float4x4 bind_local        = parent_inv * arm_mat;

                    // Store in output format (transpose for row-vector convention)
                    for (int r = 0; r < 4; r++)
                        for (int c = 0; c < 4; c++) bone_data.bind_local_matrix[r * 4 + c] = bind_local[r][c];
                }

                skel.bones.append(bone_data);
            }

            data.skeletons.add(skel.name, skel);
            CLOG_INFO(&LOG, "Processed skeleton: %s (%ld bones)", arm->id.name, bone_list.size());
        }
    }

    // ===============================================================================================
    // ===============================================================================================
    // animations
    // ===============================================================================================
    // ===============================================================================================
    {
        // Euler to quaternion conversion using blender::math
        auto euler_to_quat = [](float x, float y, float z) -> std::array<float, 4>
        {
            blender::math::EulerXYZ euler(x, y, z);
            blender::math::Quaternion quat = blender::math::to_quaternion(euler);
            // Return in XYZW order (our convention)
            return {quat.x, quat.y, quat.z, quat.w};
        };

        // Helper lambda to process FCurves from an action into animation channels
        auto process_action_fcurves = [&](bAction* action, bArmature* arm, const blender::Map<blender::StringRef, int>& bone_name_to_index, const std::function<std::array<float, 4>(float, float, float)>& euler_to_quat_fn) -> std::optional<animation_t>
        {
            // Collect all FCurves from this action using the modern animrig API
            blender::Vector<FCurve*> all_fcurves;

            // Use the animrig C++ wrapper for the new layered animation system (Blender 5.0+ / Project Baklava)
            blender::animrig::Action& act = action->wrap();

            // Iterate through layers -> strips -> channelbags -> fcurves using modern API
            for (blender::animrig::Layer* layer : act.layers())
            {
                for (blender::animrig::Strip* strip : layer->strips())
                {
                    // Only process keyframe strips
                    if (strip->type() != blender::animrig::Strip::Type::Keyframe)
                    {
                        continue;
                    }

                    blender::animrig::StripKeyframeData& strip_data = strip->data<blender::animrig::StripKeyframeData>(act);

                    for (blender::animrig::Channelbag* channelbag : strip_data.channelbags())
                    {
                        for (FCurve* fcurve : channelbag->fcurves())
                        {
                            all_fcurves.append(fcurve);
                        }
                    }
                }
            }

            if (all_fcurves.is_empty())
            {
                return std::nullopt;
            }

            // Get final frame for normalization
            float final_frame = 1.0f;
            for (FCurve* fcurve : all_fcurves)
            {
                if (fcurve->bezt && fcurve->totvert > 0)
                {
                    float last_frame = fcurve->bezt[fcurve->totvert - 1].vec[1][0];
                    if (last_frame > final_frame) final_frame = last_frame;
                }
            }

            animation_t anim;
            anim.name          = action->id.name;
            anim.armature_name = arm->id.name;

            // Group FCurves by data_path (bone + transform type)
            blender::Map<std::string, blender::Map<int, FCurve*>> grouped_fcurves;
            for (FCurve* fcurve : all_fcurves)
            {
                if (fcurve->rna_path == nullptr) continue;
                grouped_fcurves.lookup_or_add_default(fcurve->rna_path).add(fcurve->array_index, fcurve);
            }

            // Process each grouped fcurve set
            // Parse pattern: pose.bones["bonename"].transform_type using StringRef (faster than std::regex)
            constexpr blender::StringRef prefix = "pose.bones[\"";

            for (auto item : grouped_fcurves.items())
            {
                const std::string& data_path           = item.key;
                blender::Map<int, FCurve*>& fcurve_map = item.value;
                blender::StringRef path_ref(data_path);

                // Check for prefix and extract bone name
                if (!path_ref.startswith(prefix))
                {
                    continue;
                }
                path_ref          = path_ref.drop_prefix(prefix.size());

                // Find closing quote-bracket
                int64_t end_quote = path_ref.find("\"].");
                if (end_quote == blender::StringRef::not_found)
                {
                    continue;
                }

                blender::StringRef bone_name = path_ref.substr(0, end_quote);
                std::string transform_type   = std::string(path_ref.drop_prefix(end_quote + 3));

                const int* bone_idx          = bone_name_to_index.lookup_ptr(bone_name);
                if (!bone_idx)
                {
                    continue;
                }

                anim_channel_t channel;
                channel.bone_index         = *bone_idx;
                channel.interpolation_type = anim_interpolation_type_t::LINEAR;

                bool is_euler              = false;
                if (transform_type == "location")
                {
                    channel.channel_type = anim_channel_type_t::LOCATION;
                }
                else if (transform_type == "rotation_quaternion")
                {
                    channel.channel_type = anim_channel_type_t::ROTATION;
                }
                else if (transform_type == "rotation_euler")
                {
                    channel.channel_type = anim_channel_type_t::ROTATION;
                    is_euler             = true;
                }
                else if (transform_type == "scale")
                {
                    channel.channel_type = anim_channel_type_t::SCALE;
                }
                else
                {
                    continue;
                }

                // Collect all unique keyframe times
                blender::Set<float> keyframe_times_set;
                for (auto fcurve_item : fcurve_map.items())
                {
                    FCurve* fcurve = fcurve_item.value;
                    if (fcurve->bezt == nullptr) continue;
                    for (unsigned int i = 0; i < fcurve->totvert; i++)
                    {
                        keyframe_times_set.add(fcurve->bezt[i].vec[1][0]);
                    }
                }

                // Sort keyframe times chronologically
                blender::Vector<float> keyframe_times(keyframe_times_set.begin(), keyframe_times_set.end());
                std::sort(keyframe_times.begin(), keyframe_times.end());

                // Build keyframes
                for (float frame : keyframe_times)
                {
                    float normalized_time = frame / final_frame;
                    channel.keyframe_times.append(normalized_time);

                    // Sample each component at this frame
                    std::array<float, 4> values = {0.0f, 0.0f, 0.0f, 0.0f};

                    // For quaternions: default w=1
                    if (channel.channel_type == anim_channel_type_t::ROTATION && !is_euler)
                    {
                        values[3] = 1.0f;
                    }
                    // For scale: default 1,1,1
                    if (channel.channel_type == anim_channel_type_t::SCALE)
                    {
                        values[0] = values[1] = values[2] = 1.0f;
                    }

                    for (auto fcurve_item : fcurve_map.items())
                    {
                        int arr_idx    = fcurve_item.key;
                        FCurve* fcurve = fcurve_item.value;
                        if (fcurve->bezt == nullptr) continue;

                        // Find keyframe at this time or interpolate
                        float value = 0.0f;
                        bool found  = false;
                        for (unsigned int i = 0; i < fcurve->totvert; i++)
                        {
                            if (std::abs(fcurve->bezt[i].vec[1][0] - frame) < 0.001f)
                            {
                                value = fcurve->bezt[i].vec[1][1];
                                found = true;
                                break;
                            }
                        }

                        if (!found && fcurve->totvert > 0)
                        {
                            // Use previous keyframe value
                            for (int i = (int)fcurve->totvert - 1; i >= 0; i--)
                            {
                                if (fcurve->bezt[i].vec[1][0] <= frame)
                                {
                                    value = fcurve->bezt[i].vec[1][1];
                                    break;
                                }
                            }
                        }

                        // Map array index to output position
                        // Blender quaternion: WXYZ, our output: XYZW
                        if (channel.channel_type == anim_channel_type_t::ROTATION && !is_euler)
                        {
                            // Quaternion reordering: Blender WXYZ -> XYZW
                            if (arr_idx == 0) values[3] = value;      // W -> index 3
                            else if (arr_idx == 1) values[0] = value; // X -> index 0
                            else if (arr_idx == 2) values[1] = value; // Y -> index 1
                            else if (arr_idx == 3) values[2] = value; // Z -> index 2
                        }
                        else if (arr_idx < 4)
                        {
                            values[arr_idx] = value;
                        }
                    }

                    // Convert Euler to quaternion if needed
                    if (is_euler)
                    {
                        values = euler_to_quat_fn(values[0], values[1], values[2]);
                    }

                    channel.keyframe_values.append(values);
                }

                if (!channel.keyframe_times.is_empty())
                {
                    anim.channels.append(channel);
                }
            }

            if (anim.channels.is_empty())
            {
                return std::nullopt;
            }

            return anim;
        };

        // Process each armature object with NLA animation data
        LISTBASE_FOREACH(Object*, ob, &bmain->objects)
        {
            if (ob->type != OB_ARMATURE || ob->adt == nullptr) continue;

            bArmature* arm = (bArmature*)ob->data;
            if (arm == nullptr) continue;

            // Build bone name to index map for this armature (using original names)
            blender::Map<blender::StringRef, int> bone_name_to_index;
            {
                int idx                            = 0;
                std::function<void(Bone*)> collect = [&](Bone* bone)
                {
                    bone_name_to_index.add(bone->name, idx++);
                    for (Bone* child : blender::ListBaseWrapper<Bone>(bone->childbase))
                    {
                        collect(child);
                    }
                };
                for (Bone* bone : blender::ListBaseWrapper<Bone>(arm->bonebase))
                {
                    collect(bone);
                }
            }

            // Process NLA tracks
            AnimData* adt = ob->adt;
            for (NlaTrack* track : blender::ListBaseWrapper<NlaTrack>(adt->nla_tracks))
            {
                for (NlaStrip* strip : blender::ListBaseWrapper<NlaStrip>(track->strips))
                {
                    bAction* action = strip->act;
                    if (action == nullptr) continue;

                    auto result = process_action_fcurves(action, arm, bone_name_to_index, euler_to_quat);
                    if (result.has_value())
                    {
                        data.animations.append(result.value());
                        CLOG_INFO(&LOG, "Processed animation: %s (%ld channels)", action->id.name, data.animations.last().channels.size());
                    }
                }
            }
        }
    }

    // ===============================================================================================
    // ===============================================================================================
    // dump data_t to log file
    // ===============================================================================================
    // ===============================================================================================
    {
        dbg_data(data);
    }

    // ===============================================================================================
    // ===============================================================================================
    // ===============================================================================================
    // ===============================================================================================
    arena_zh arena        = arena_init_z(8ULL * 1024ULL * 1024ULL * 1024ULL);

    // ===============================================================================================
    // ===============================================================================================
    // ===============================================================================================
    // ===============================================================================================
    auto normalize_string = [&](const std::string& str) -> const char*
    {
        std::string normalized = str;
        for (char& c : normalized)
        {
            if (c == ' ') c = '_';
        }
        char* result = (char*)arena_alloc_z(arena, normalized.size() + 1)->data;
        memcpy(result, normalized.c_str(), normalized.size() + 1);
        return result;
    };

    // ===============================================================================================
    // ===============================================================================================
    // ===============================================================================================
    // ===============================================================================================
    BBArchiveMesh* meshes = nullptr;
    int mesh_count        = 0;
    {
        mesh_count   = data.meshes.size();
        meshes       = (BBArchiveMesh*)arena_alloc_z(arena, sizeof(BBArchiveMesh) * mesh_count)->data;

        int mesh_idx = 0;
        for (auto mesh_item : data.meshes.items())
        {
            const mesh_t& mesh          = mesh_item.value;
            int submesh_count           = mesh.submeshes.size();
            BBArchiveSubmesh* submeshes = (BBArchiveSubmesh*)arena_alloc_z(arena, sizeof(BBArchiveSubmesh) * submesh_count)->data;

            for (size_t submesh_idx = 0; submesh_idx < mesh.submeshes.size(); submesh_idx++)
            {
                const submesh_t& src_submesh = mesh.submeshes[submesh_idx];
                int total_vertices           = src_submesh.triangles.size() * 3;

                // Allocate data arrays
                float* positions_data        = (float*)arena_alloc_z(arena, sizeof(float) * 3 * total_vertices)->data;
                float* uv0_data              = (float*)arena_alloc_z(arena, sizeof(float) * 2 * total_vertices)->data;
                float* uv1_data              = (float*)arena_alloc_z(arena, sizeof(float) * 2 * total_vertices)->data;
                float* uv2_data              = (float*)arena_alloc_z(arena, sizeof(float) * 2 * total_vertices)->data;
                uint8_t* joint_indices_data  = (uint8_t*)arena_alloc_z(arena, sizeof(uint8_t) * 4 * total_vertices)->data;
                float* joint_weights_data    = (float*)arena_alloc_z(arena, sizeof(float) * 4 * total_vertices)->data;

                // Check if any vertex has skinning data
                bool has_skinning            = false;

                // Fill data arrays from triangles
                int vertex_offset            = 0;
                for (const triangle_t& tri : src_submesh.triangles)
                {
                    for (int i = 0; i < 3; i++)
                    {
                        const vertex_t& vert                      = tri.vertices[i];

                        // Position
                        positions_data[vertex_offset * 3 + 0]     = vert.position.x;
                        positions_data[vertex_offset * 3 + 1]     = vert.position.y;
                        positions_data[vertex_offset * 3 + 2]     = vert.position.z;

                        // UV
                        uv0_data[vertex_offset * 2 + 0]           = vert.uv0.x;
                        uv0_data[vertex_offset * 2 + 1]           = vert.uv0.y;
                        uv1_data[vertex_offset * 2 + 0]           = vert.uv1.x;
                        uv1_data[vertex_offset * 2 + 1]           = vert.uv1.y;
                        uv2_data[vertex_offset * 2 + 0]           = vert.uv2.x;
                        uv2_data[vertex_offset * 2 + 1]           = vert.uv2.y;

                        // Joint indices and weights
                        joint_indices_data[vertex_offset * 4 + 0] = vert.joint_indices[0];
                        joint_indices_data[vertex_offset * 4 + 1] = vert.joint_indices[1];
                        joint_indices_data[vertex_offset * 4 + 2] = vert.joint_indices[2];
                        joint_indices_data[vertex_offset * 4 + 3] = vert.joint_indices[3];
                        joint_weights_data[vertex_offset * 4 + 0] = vert.joint_weights[0];
                        joint_weights_data[vertex_offset * 4 + 1] = vert.joint_weights[1];
                        joint_weights_data[vertex_offset * 4 + 2] = vert.joint_weights[2];
                        joint_weights_data[vertex_offset * 4 + 3] = vert.joint_weights[3];

                        // Check if this vertex has any non-zero weight
                        if (vert.joint_weights[0] > 0.0f) has_skinning = true;

                        vertex_offset++;
                    }
                }

                // Create submesh
                BBArchiveSubmesh& submesh = submeshes[submesh_idx];
                submesh.vertex_count      = total_vertices;
                submesh.positions         = positions_data;
                submesh.uv0               = uv0_data;
                submesh.uv1               = uv1_data;
                submesh.uv2               = uv2_data;
                submesh.joint_indices     = has_skinning ? joint_indices_data : nullptr;
                submesh.joint_weights     = has_skinning ? joint_weights_data : nullptr;
            }

            // Create mesh structure
            BBArchiveMesh* bb_mesh = &meshes[mesh_idx];
            bb_mesh->mesh_name     = normalize_string(mesh.name);
            bb_mesh->submesh_count = submesh_count;
            bb_mesh->submeshes     = submeshes;

            mesh_idx++;
        }

        mesh_count = mesh_idx;
    }

    // ===============================================================================================
    // ===============================================================================================
    // Extract scene data
    // ===============================================================================================
    // ===============================================================================================
    blender::Vector<BBArchiveScene> bb_scenes;
    {
        bb_scenes.resize(data.scenes.size());

        for (size_t scene_idx = 0; scene_idx < data.scenes.size(); scene_idx++)
        {
            const scene_t& scene   = data.scenes[scene_idx];

            size_t mesh_elem_count = 0;
            for (auto elem_item : scene.elems.items())
            {
                if (elem_item.value.mesh_name.has_value()) mesh_elem_count++;
            }

            BBArchiveSceneElem* elems = (BBArchiveSceneElem*)arena_alloc_z(arena, sizeof(BBArchiveSceneElem) * mesh_elem_count)->data;

            size_t elem_idx           = 0;
            for (auto elem_item : scene.elems.items())
            {
                const scene_elem_t& src_elem = elem_item.value;
                if (!src_elem.mesh_name.has_value()) continue;

                const mesh_t& mesh     = data.meshes.lookup(src_elem.mesh_name.value());

                int mat_count          = mesh.submeshes.size();
                const char** mat_names = (const char**)arena_alloc_z(arena, sizeof(const char*) * mat_count)->data;
                for (size_t i = 0; i < mesh.submeshes.size(); i++)
                {
                    mat_names[i] = normalize_string("mat_" + mesh.submeshes[i].material_name); /* temp */
                }

                BBArchiveSceneElem& elem = elems[elem_idx];
                elem.mesh_name           = normalize_string(src_elem.mesh_name.value());
                elem.pos[0]              = src_elem.pos.x;
                elem.pos[1]              = src_elem.pos.y;
                elem.pos[2]              = src_elem.pos.z;
                elem.rot[0]              = src_elem.euler_rot.x;
                elem.rot[1]              = src_elem.euler_rot.y;
                elem.rot[2]              = src_elem.euler_rot.z;
                elem.sca[0]              = src_elem.scale.x;
                elem.sca[1]              = src_elem.scale.y;
                elem.sca[2]              = src_elem.scale.z;
                elem.mat_count           = mat_count;
                elem.mat_names           = mat_names;

                elem_idx++;
            }

            bb_scenes[scene_idx].scene_name = normalize_string(scene.name);
            bb_scenes[scene_idx].elem_count = mesh_elem_count;
            bb_scenes[scene_idx].elems      = elems;
        }
    }

    // ===============================================================================================
    // ===============================================================================================
    // Extract images for bb_archive
    // ===============================================================================================
    // ===============================================================================================
    blender::Vector<BBArchiveImage> bb_images;
    {
        bb_images.resize(data.images.size());

        size_t image_idx = 0;
        for (auto image_item : data.images.items())
        {
            const image_t& image      = image_item.value;
            BBArchiveImage& bb_image  = bb_images[image_idx];
            bb_image.image_name       = normalize_string(image.name);
            bb_image.width            = image.width;
            bb_image.height           = image.height;
            bb_image.num_channels     = 4;
            bb_image.bits_per_channel = 8;
            bb_image.is_srgb          = image.is_srgb;
            bb_image.mip_levels       = 1;
            bb_image.blob             = image.rgba_data.data();
            bb_image.blob_size        = image.rgba_data.size();
            image_idx++;
        }
    }

    // ===============================================================================================
    // ===============================================================================================
    // Extract shader graphs
    // ===============================================================================================
    // ===============================================================================================
    blender::Vector<BBArchiveShaderGraph> bb_shaders;
    {
        // Convert socket_default_value_t to BBArchiveShaderField
        auto make_field = [&](bb_shader_input_field_type_e field_type, const socket_default_value_t& val) -> BBArchiveShaderField
        {
            BBArchiveShaderField field = {};
            field.field_type           = field_type;

            std::visit(
                [&](auto&& v)
                {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, socket_value_float_t>)
                    {
                        field.value_type = BB_SHADER_VALUE_TYPE_FLOAT;
                        field.float_val  = v.value;
                    }
                    else if constexpr (std::is_same_v<T, socket_value_int_t>)
                    {
                        field.value_type = BB_SHADER_VALUE_TYPE_INT;
                        field.int_val    = v.value;
                    }
                    else if constexpr (std::is_same_v<T, socket_value_bool_t>)
                    {
                        field.value_type = BB_SHADER_VALUE_TYPE_BOOL;
                        field.bool_val   = v.value ? 1 : 0;
                    }
                    else if constexpr (std::is_same_v<T, socket_value_vector_t>)
                    {
                        field.value_type  = BB_SHADER_VALUE_TYPE_FLOAT3;
                        field.vec3_val[0] = v.value.x;
                        field.vec3_val[1] = v.value.y;
                        field.vec3_val[2] = v.value.z;
                    }
                    else if constexpr (std::is_same_v<T, socket_value_rgba_t>)
                    {
                        field.value_type  = BB_SHADER_VALUE_TYPE_FLOAT4;
                        field.vec4_val[0] = v.value.x;
                        field.vec4_val[1] = v.value.y;
                        field.vec4_val[2] = v.value.z;
                        field.vec4_val[3] = v.value.w;
                    }
                    else if constexpr (std::is_same_v<T, socket_value_tex_t>)
                    {
                        field.value_type = BB_SHADER_VALUE_TYPE_TEX;
                        field.str_val    = normalize_string(v.value);
                    }
                    else if constexpr (std::is_same_v<T, socket_value_uv_map_t>)
                    {
                        field.value_type = BB_SHADER_VALUE_TYPE_UV_MAP;
                        field.str_val    = normalize_string(v.value);
                    }
                },
                val
            );

            return field;
        };

        bb_shaders.reserve(data.materials.size());

        for (auto mat_item : data.materials.items())
        {
            const material_t& mat = mat_item.value;
            // Build node name to index map
            blender::Map<std::string, uint32_t> node_name_to_idx;
            blender::Vector<std::pair<const shader_node_t*, bb_shader_node_type_e>> valid_nodes;

            for (const auto& node : mat.nodes)
            {
                auto it = node_type_map.find(node.idname);
                std::optional<bb_shader_node_type_e> node_type_opt;
                if (it != node_type_map.end()) node_type_opt = it->second;
                if (!node_type_opt.has_value())
                {
                    CLOG_FATAL(&LOG, "Unknown node type '%s' in material '%s'", node.idname.c_str(), mat.name.c_str());
                }
                node_name_to_idx.add(node.name, (uint32_t)valid_nodes.size());
                valid_nodes.append({&node, node_type_opt.value()});
            }

            if (valid_nodes.is_empty())
            {
                CLOG_FATAL(&LOG, "No valid nodes in material '%s'", mat.name.c_str());
            }

            // Build nodes
            BBArchiveShaderNode* bb_nodes = (BBArchiveShaderNode*)arena_alloc_z(arena, sizeof(BBArchiveShaderNode) * valid_nodes.size())->data;

            for (size_t i = 0; i < valid_nodes.size(); i++)
            {
                const shader_node_t* node    = valid_nodes[i].first;
                bb_shader_node_type_e n_type = valid_nodes[i].second;

                // Count fields (inputs with default values + props)
                blender::Vector<BBArchiveShaderField> fields;

                // Add input socket default values
                for (const auto& input : node->inputs)
                {
                    if (!input.default_value.has_value()) continue;
                    auto field = lookup_field(input_field_map, n_type, input.identifier);
                    fields.append(make_field(field, input.default_value.value()));
                }

                // For Value and RGB nodes, the constant value is stored in the output socket
                // We treat it as an input field since it's a value that defines the node's output
                if (n_type == BB_SHADER_NODE_TYPE_VALUE || n_type == BB_SHADER_NODE_TYPE_RGB)
                {
                    for (const auto& output : node->outputs)
                    {
                        if (!output.default_value.has_value()) continue;
                        auto field = lookup_field(input_field_map, n_type, output.identifier);
                        fields.append(make_field(field, output.default_value.value()));
                    }
                }

                // Add props
                for (const auto& prop : node->props)
                {
                    auto field = lookup_field(prop_field_map, n_type, prop.identifier);
                    fields.append(make_field(field, prop.value));
                }

                // Allocate and copy fields
                BBArchiveShaderField* bb_fields = nullptr;
                if (!fields.is_empty())
                {
                    bb_fields = (BBArchiveShaderField*)arena_alloc_z(arena, sizeof(BBArchiveShaderField) * fields.size())->data;
                    memcpy(bb_fields, fields.data(), sizeof(BBArchiveShaderField) * fields.size());
                }

                bb_nodes[i].node_type   = n_type;
                bb_nodes[i].field_count = fields.size();
                bb_nodes[i].fields      = bb_fields;
            }

            // Build links
            blender::Vector<BBArchiveShaderLink> links;
            for (const auto& link : mat.links)
            {
                const uint32_t* src_idx_ptr = node_name_to_idx.lookup_ptr(link.from_node);
                const uint32_t* dst_idx_ptr = node_name_to_idx.lookup_ptr(link.to_node);
                if (!src_idx_ptr)
                {
                    CLOG_FATAL(&LOG, "Link source node '%s' not found in material '%s'", link.from_node.c_str(), mat.name.c_str());
                }
                if (!dst_idx_ptr)
                {
                    CLOG_FATAL(&LOG, "Link destination node '%s' not found in material '%s'", link.to_node.c_str(), mat.name.c_str());
                }

                uint32_t src_idx               = *src_idx_ptr;
                uint32_t dst_idx               = *dst_idx_ptr;
                bb_shader_node_type_e src_type = valid_nodes[src_idx].second;
                bb_shader_node_type_e dst_type = valid_nodes[dst_idx].second;

                auto from_field                = lookup_field(output_field_map, src_type, link.from_socket);
                auto to_field                  = lookup_field(input_field_map, dst_type, link.to_socket);

                BBArchiveShaderLink bb_link;
                bb_link.src_idx    = src_idx;
                bb_link.dst_idx    = dst_idx;
                bb_link.from_field = from_field;
                bb_link.to_field   = to_field;
                links.append(bb_link);
            }

            // Allocate and copy links
            BBArchiveShaderLink* bb_links = nullptr;
            if (!links.is_empty())
            {
                bb_links = (BBArchiveShaderLink*)arena_alloc_z(arena, sizeof(BBArchiveShaderLink) * links.size())->data;
                memcpy(bb_links, links.data(), sizeof(BBArchiveShaderLink) * links.size());
            }

            // Collect mat_data: all string fields (TEX and UV_MAP types)
            blender::Vector<BBArchiveShaderMatData> mat_data_vec;
            for (uint32_t node_idx = 0; node_idx < valid_nodes.size(); node_idx++)
            {
                const BBArchiveShaderNode& node = bb_nodes[node_idx];
                for (uint32_t field_idx = 0; field_idx < node.field_count; field_idx++)
                {
                    const BBArchiveShaderField& field = node.fields[field_idx];
                    if (field.value_type == BB_SHADER_VALUE_TYPE_TEX || field.value_type == BB_SHADER_VALUE_TYPE_UV_MAP)
                    {
                        BBArchiveShaderMatData mat_data_entry;
                        mat_data_entry.node_idx = node_idx;
                        mat_data_entry.field    = field;
                        mat_data_vec.append(mat_data_entry);
                    }
                }
            }

            // Allocate and copy mat_data
            BBArchiveShaderMatData* bb_mat_data = nullptr;
            if (!mat_data_vec.is_empty())
            {
                bb_mat_data = (BBArchiveShaderMatData*)arena_alloc_z(arena, sizeof(BBArchiveShaderMatData) * mat_data_vec.size())->data;
                memcpy(bb_mat_data, mat_data_vec.data(), sizeof(BBArchiveShaderMatData) * mat_data_vec.size());
            }

            BBArchiveShaderGraph shader_graph;
            shader_graph.shader_name    = normalize_string(mat.name);
            shader_graph.node_count     = valid_nodes.size();
            shader_graph.nodes          = bb_nodes;
            shader_graph.link_count     = links.size();
            shader_graph.links          = bb_links;
            shader_graph.mat_data_count = mat_data_vec.size();
            shader_graph.mat_data       = bb_mat_data;

            bb_shaders.append(shader_graph);
        }

        CLOG_INFO(&LOG, "Exported %ld shader graphs", bb_shaders.size());
    }

    // ===============================================================================================
    // ===============================================================================================
    // Extract skeletons for bb_archive
    // ===============================================================================================
    // ===============================================================================================
    blender::Vector<BBArchiveSkel> bb_skeletons;
    {
        bb_skeletons.reserve(data.skeletons.size());

        for (auto skel_item : data.skeletons.items())
        {
            const skeleton_t& skel  = skel_item.value;
            size_t bone_count       = skel.bones.size();

            // Allocate bone names array
            const char** bone_names = (const char**)arena_alloc_z(arena, sizeof(const char*) * bone_count)->data;
            for (size_t i = 0; i < bone_count; i++)
            {
                bone_names[i] = normalize_string(skel.bones[i].name);
            }

            // Allocate parent indices array
            int32_t* parent_indices = (int32_t*)arena_alloc_z(arena, sizeof(int32_t) * bone_count)->data;
            for (size_t i = 0; i < bone_count; i++)
            {
                parent_indices[i] = skel.bones[i].parent_index;
            }

            // Allocate bind local matrices (16 floats per bone)
            float* bind_local_mats = (float*)arena_alloc_z(arena, sizeof(float) * 16 * bone_count)->data;
            for (size_t i = 0; i < bone_count; i++)
            {
                for (int j = 0; j < 16; j++)
                {
                    bind_local_mats[i * 16 + j] = skel.bones[i].bind_local_matrix[j];
                }
            }

            // Allocate inverse bind matrices (16 floats per bone)
            float* inv_bind_mats = (float*)arena_alloc_z(arena, sizeof(float) * 16 * bone_count)->data;
            for (size_t i = 0; i < bone_count; i++)
            {
                for (int j = 0; j < 16; j++)
                {
                    inv_bind_mats[i * 16 + j] = skel.bones[i].inverse_bind_matrix[j];
                }
            }

            BBArchiveSkel bb_skel;
            bb_skel.skeleton_name         = normalize_string(skel.name);
            bb_skel.bone_count            = bone_count;
            bb_skel.bone_names            = bone_names;
            bb_skel.parent_bone_indices   = parent_indices;
            bb_skel.bind_local_matrices   = bind_local_mats;
            bb_skel.inverse_bind_matrices = inv_bind_mats;

            bb_skeletons.append(bb_skel);
        }

        CLOG_INFO(&LOG, "Exported %ld skeletons", bb_skeletons.size());
    }

    // ===============================================================================================
    // ===============================================================================================
    // Extract animations for bb_archive
    // ===============================================================================================
    // ===============================================================================================
    blender::Vector<BBArchiveAnim> bb_animations;
    {
        bb_animations.reserve(data.animations.size());

        for (const auto& anim : data.animations)
        {
            size_t channel_count = anim.channels.size();
            if (channel_count == 0) continue;

            // Allocate per-channel arrays
            uint32_t* bone_indices        = (uint32_t*)arena_alloc_z(arena, sizeof(uint32_t) * channel_count)->data;
            uint32_t* channel_types       = (uint32_t*)arena_alloc_z(arena, sizeof(uint32_t) * channel_count)->data;
            uint32_t* interpolation_types = (uint32_t*)arena_alloc_z(arena, sizeof(uint32_t) * channel_count)->data;
            uint32_t* keyframe_counts     = (uint32_t*)arena_alloc_z(arena, sizeof(uint32_t) * channel_count)->data;

            // Calculate total keyframe count
            size_t total_keyframes        = 0;
            for (size_t i = 0; i < channel_count; i++)
            {
                total_keyframes += anim.channels[i].keyframe_times.size();
            }

            // Allocate keyframe arrays
            float* keyframe_times  = (float*)arena_alloc_z(arena, sizeof(float) * total_keyframes)->data;
            float* keyframe_values = (float*)arena_alloc_z(arena, sizeof(float) * total_keyframes * 4)->data;

            // Fill arrays
            size_t time_offset     = 0;
            size_t value_offset    = 0;
            for (size_t i = 0; i < channel_count; i++)
            {
                const anim_channel_t& channel = anim.channels[i];

                bone_indices[i]               = channel.bone_index;
                channel_types[i]              = static_cast<uint32_t>(channel.channel_type);
                interpolation_types[i]        = static_cast<uint32_t>(channel.interpolation_type);
                keyframe_counts[i]            = channel.keyframe_times.size();

                for (size_t k = 0; k < channel.keyframe_times.size(); k++)
                {
                    keyframe_times[time_offset++]   = channel.keyframe_times[k];
                    keyframe_values[value_offset++] = channel.keyframe_values[k][0];
                    keyframe_values[value_offset++] = channel.keyframe_values[k][1];
                    keyframe_values[value_offset++] = channel.keyframe_values[k][2];
                    keyframe_values[value_offset++] = channel.keyframe_values[k][3];
                }
            }

            BBArchiveAnim bb_anim;
            bb_anim.animation_name      = normalize_string(anim.name);
            bb_anim.channel_count       = channel_count;
            bb_anim.bone_indices        = bone_indices;
            bb_anim.channel_types       = channel_types;
            bb_anim.interpolation_types = interpolation_types;
            bb_anim.keyframe_counts     = keyframe_counts;
            bb_anim.keyframe_times      = keyframe_times;
            bb_anim.keyframe_values     = keyframe_values;

            bb_animations.append(bb_anim);
        }

        CLOG_INFO(&LOG, "Exported %ld animations", bb_animations.size());
    }

    // ===============================================================================================
    // ===============================================================================================
    // Write to bb_archive
    // ===============================================================================================
    // ===============================================================================================
    {
        BBArchiveInfo info      = {0};
        info.mesh_count         = mesh_count;
        info.image_count        = bb_images.size();
        info.skeleton_count     = bb_skeletons.size();
        info.animation_count    = bb_animations.size();
        info.shader_graph_count = bb_shaders.size();
        info.scene_count        = bb_scenes.size();
        info.meshes             = meshes;
        info.images             = bb_images.data();
        info.skeletons          = bb_skeletons.is_empty() ? nullptr : bb_skeletons.data();
        info.animations         = bb_animations.is_empty() ? nullptr : bb_animations.data();
        info.shader_graphs      = bb_shaders.data();
        info.scenes             = bb_scenes.data();

        bb_archive_write(&info, bb_archive_output_dir);
        CLOG_INFO(&LOG, "bb_archive_write completed");
    }

    arena_destroy_z(arena);
}
