#pragma once

#include <stdint.h>
#include <zp_c/arena.h>

#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C extern
#endif

// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
typedef enum
{
    BB_SHADER_NODE_TYPE_INPUT               = 0,
    BB_SHADER_NODE_TYPE_OUTPUT              = 1,
    BB_SHADER_NODE_TYPE_UV_MAP              = 2,
    BB_SHADER_NODE_TYPE_MATH                = 3,
    BB_SHADER_NODE_TYPE_VECTOR_MATH         = 4,
    BB_SHADER_NODE_TYPE_MIX                 = 5,
    BB_SHADER_NODE_TYPE_SEPARATE_COLOR      = 6,
    BB_SHADER_NODE_TYPE_COMBINE_COLOR       = 7,
    BB_SHADER_NODE_TYPE_TEX_NOISE           = 8,
    BB_SHADER_NODE_TYPE_TEX_IMAGE           = 9,
    BB_SHADER_NODE_TYPE_VAL_TO_RGB          = 10,
    BB_SHADER_NODE_TYPE_AMBIENT_OCCLUSION   = 11,
    BB_SHADER_NODE_TYPE_BUMP                = 12,
    BB_SHADER_NODE_TYPE_CLAMP               = 13,
    BB_SHADER_NODE_TYPE_DISPLACEMENT        = 14,
    BB_SHADER_NODE_TYPE_MAP_RANGE           = 15,
    BB_SHADER_NODE_TYPE_MAPPING             = 16,
    BB_SHADER_NODE_TYPE_NORMAL_MAP          = 17,
    BB_SHADER_NODE_TYPE_TANGENT             = 18,
    BB_SHADER_NODE_TYPE_TEX_BRICK           = 19,
    BB_SHADER_NODE_TYPE_TEX_ENVIRONMENT     = 20,
    BB_SHADER_NODE_TYPE_TEX_GABOR           = 21,
    BB_SHADER_NODE_TYPE_TEX_GRADIENT        = 22,
    BB_SHADER_NODE_TYPE_TEX_MAGIC           = 23,
    BB_SHADER_NODE_TYPE_TEX_VORONOI         = 24,
    BB_SHADER_NODE_TYPE_TEX_WAVE            = 25,
    BB_SHADER_NODE_TYPE_TEX_WHITE_NOISE     = 26,
    BB_SHADER_NODE_TYPE_VECTOR_DISPLACEMENT = 27,
    BB_SHADER_NODE_TYPE_VECTOR_ROTATE       = 28,
    BB_SHADER_NODE_TYPE_VECTOR_TRANSFORM    = 29,
    BB_SHADER_NODE_TYPE_VALUE               = 30,
    BB_SHADER_NODE_TYPE_GAMMA               = 31,
    BB_SHADER_NODE_TYPE_COMBINE_XYZ         = 32,
    BB_SHADER_NODE_TYPE_SEPARATE_XYZ        = 33,
    BB_SHADER_NODE_TYPE_RGB                 = 34,
    BB_SHADER_NODE_TYPE_BRIGHT_CONTRAST     = 35,
    BB_SHADER_NODE_TYPE_RGB_TO_BW           = 36,
    BB_SHADER_NODE_TYPE_HUE_SATURATION      = 37,
    BB_SHADER_NODE_TYPE_INVERT              = 38,
    BB_SHADER_NODE_TYPE_FRESNEL             = 39,
} bb_shader_node_type_e;

typedef enum
{
    BB_SHADER_INPUT_FIELD_OUTPUT_BASE_COLOUR               = 0,
    BB_SHADER_INPUT_FIELD_OUTPUT_EMISSION                  = 1,
    BB_SHADER_INPUT_FIELD_OUTPUT_EMISSION_STRENGTH         = 2,
    BB_SHADER_INPUT_FIELD_OUTPUT_ROUGHNESS                 = 3,
    BB_SHADER_INPUT_FIELD_OUTPUT_METALLIC                  = 4,
    BB_SHADER_INPUT_FIELD_OUTPUT_SPECULAR                  = 5,
    BB_SHADER_INPUT_FIELD_OUTPUT_NORMAL                    = 6,
    BB_SHADER_INPUT_FIELD_OUTPUT_ALPHA                     = 7,
    BB_SHADER_INPUT_FIELD_UV_MAP_UV                        = 8,
    BB_SHADER_INPUT_FIELD_MATH_VALUE                       = 9,
    BB_SHADER_INPUT_FIELD_MATH_VALUE_001                   = 10,
    BB_SHADER_INPUT_FIELD_MATH_VALUE_002                   = 11,
    BB_SHADER_INPUT_FIELD_VECTOR_MATH_VECTOR               = 12,
    BB_SHADER_INPUT_FIELD_VECTOR_MATH_VECTOR_001           = 13,
    BB_SHADER_INPUT_FIELD_VECTOR_MATH_VECTOR_002           = 14,
    BB_SHADER_INPUT_FIELD_VECTOR_MATH_SCALE                = 15,
    BB_SHADER_INPUT_FIELD_MIX_FACTOR_FLOAT                 = 16,
    BB_SHADER_INPUT_FIELD_MIX_FACTOR_VECTOR                = 17,
    BB_SHADER_INPUT_FIELD_MIX_A_FLOAT                      = 18,
    BB_SHADER_INPUT_FIELD_MIX_B_FLOAT                      = 19,
    BB_SHADER_INPUT_FIELD_MIX_A_VECTOR                     = 20,
    BB_SHADER_INPUT_FIELD_MIX_B_VECTOR                     = 21,
    BB_SHADER_INPUT_FIELD_MIX_A_COLOR                      = 22,
    BB_SHADER_INPUT_FIELD_MIX_B_COLOR                      = 23,
    BB_SHADER_INPUT_FIELD_MIX_A_ROTATION                   = 24,
    BB_SHADER_INPUT_FIELD_MIX_B_ROTATION                   = 25,
    BB_SHADER_INPUT_FIELD_SEPARATE_COLOR_COLOR             = 26,
    BB_SHADER_INPUT_FIELD_COMBINE_COLOR_RED                = 27,
    BB_SHADER_INPUT_FIELD_COMBINE_COLOR_GREEN              = 28,
    BB_SHADER_INPUT_FIELD_COMBINE_COLOR_BLUE               = 29,
    BB_SHADER_INPUT_FIELD_COMBINE_XYZ_X                    = 30,
    BB_SHADER_INPUT_FIELD_COMBINE_XYZ_Y                    = 31,
    BB_SHADER_INPUT_FIELD_COMBINE_XYZ_Z                    = 32,
    BB_SHADER_INPUT_FIELD_SEPARATE_XYZ_VECTOR              = 33,
    BB_SHADER_INPUT_FIELD_RGB_COLOR                        = 34,
    BB_SHADER_INPUT_FIELD_BRIGHT_CONTRAST_COLOR            = 35,
    BB_SHADER_INPUT_FIELD_BRIGHT_CONTRAST_BRIGHT           = 36,
    BB_SHADER_INPUT_FIELD_BRIGHT_CONTRAST_CONTRAST         = 37,
    BB_SHADER_INPUT_FIELD_RGB_TO_BW_COLOR                  = 38,
    BB_SHADER_INPUT_FIELD_HUE_SATURATION_HUE               = 39,
    BB_SHADER_INPUT_FIELD_HUE_SATURATION_SATURATION        = 40,
    BB_SHADER_INPUT_FIELD_HUE_SATURATION_VALUE             = 41,
    BB_SHADER_INPUT_FIELD_HUE_SATURATION_FAC               = 42,
    BB_SHADER_INPUT_FIELD_HUE_SATURATION_COLOR             = 43,
    BB_SHADER_INPUT_FIELD_INVERT_FAC                       = 44,
    BB_SHADER_INPUT_FIELD_INVERT_COLOR                     = 45,
    BB_SHADER_INPUT_FIELD_FRESNEL_IOR                      = 46,
    BB_SHADER_INPUT_FIELD_FRESNEL_NORMAL                   = 47,
    BB_SHADER_INPUT_FIELD_TEX_NOISE_VECTOR                 = 48,
    BB_SHADER_INPUT_FIELD_TEX_NOISE_W                      = 49,
    BB_SHADER_INPUT_FIELD_TEX_NOISE_SCALE                  = 50,
    BB_SHADER_INPUT_FIELD_TEX_NOISE_DETAIL                 = 51,
    BB_SHADER_INPUT_FIELD_TEX_NOISE_ROUGHNESS              = 52,
    BB_SHADER_INPUT_FIELD_TEX_NOISE_LACUNARITY             = 53,
    BB_SHADER_INPUT_FIELD_TEX_NOISE_OFFSET                 = 54,
    BB_SHADER_INPUT_FIELD_TEX_NOISE_GAIN                   = 55,
    BB_SHADER_INPUT_FIELD_TEX_NOISE_DISTORTION             = 56,
    BB_SHADER_INPUT_FIELD_TEX_IMAGE_VECTOR                 = 57,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_FAC                   = 58,
    BB_SHADER_INPUT_FIELD_AMBIENT_OCCLUSION_COLOR          = 59,
    BB_SHADER_INPUT_FIELD_AMBIENT_OCCLUSION_DISTANCE       = 60,
    BB_SHADER_INPUT_FIELD_AMBIENT_OCCLUSION_NORMAL         = 61,
    BB_SHADER_INPUT_FIELD_BUMP_STRENGTH                    = 62,
    BB_SHADER_INPUT_FIELD_BUMP_DISTANCE                    = 63,
    BB_SHADER_INPUT_FIELD_BUMP_FILTER_WIDTH                = 64,
    BB_SHADER_INPUT_FIELD_BUMP_HEIGHT                      = 65,
    BB_SHADER_INPUT_FIELD_BUMP_NORMAL                      = 66,
    BB_SHADER_INPUT_FIELD_CLAMP_VALUE                      = 67,
    BB_SHADER_INPUT_FIELD_CLAMP_MIN                        = 68,
    BB_SHADER_INPUT_FIELD_CLAMP_MAX                        = 69,
    BB_SHADER_INPUT_FIELD_DISPLACEMENT_HEIGHT              = 70,
    BB_SHADER_INPUT_FIELD_DISPLACEMENT_MIDLEVEL            = 71,
    BB_SHADER_INPUT_FIELD_DISPLACEMENT_SCALE               = 72,
    BB_SHADER_INPUT_FIELD_DISPLACEMENT_NORMAL              = 73,
    BB_SHADER_INPUT_FIELD_MAP_RANGE_VALUE                  = 74,
    BB_SHADER_INPUT_FIELD_MAP_RANGE_FROM_MIN               = 75,
    BB_SHADER_INPUT_FIELD_MAP_RANGE_FROM_MAX               = 76,
    BB_SHADER_INPUT_FIELD_MAP_RANGE_TO_MIN                 = 77,
    BB_SHADER_INPUT_FIELD_MAP_RANGE_TO_MAX                 = 78,
    BB_SHADER_INPUT_FIELD_MAP_RANGE_STEPS                  = 79,
    BB_SHADER_INPUT_FIELD_MAP_RANGE_VECTOR                 = 80,
    BB_SHADER_INPUT_FIELD_MAP_RANGE_FROM_MIN_FLOAT3        = 81,
    BB_SHADER_INPUT_FIELD_MAP_RANGE_FROM_MAX_FLOAT3        = 82,
    BB_SHADER_INPUT_FIELD_MAP_RANGE_TO_MIN_FLOAT3          = 83,
    BB_SHADER_INPUT_FIELD_MAP_RANGE_TO_MAX_FLOAT3          = 84,
    BB_SHADER_INPUT_FIELD_MAP_RANGE_STEPS_FLOAT3           = 85,
    BB_SHADER_INPUT_FIELD_MAPPING_VECTOR                   = 86,
    BB_SHADER_INPUT_FIELD_MAPPING_LOCATION                 = 87,
    BB_SHADER_INPUT_FIELD_MAPPING_ROTATION                 = 88,
    BB_SHADER_INPUT_FIELD_MAPPING_SCALE                    = 89,
    BB_SHADER_INPUT_FIELD_NORMAL_MAP_STRENGTH              = 90,
    BB_SHADER_INPUT_FIELD_NORMAL_MAP_COLOR                 = 91,
    BB_SHADER_INPUT_FIELD_TEX_BRICK_VECTOR                 = 92,
    BB_SHADER_INPUT_FIELD_TEX_BRICK_COLOR1                 = 93,
    BB_SHADER_INPUT_FIELD_TEX_BRICK_COLOR2                 = 94,
    BB_SHADER_INPUT_FIELD_TEX_BRICK_MORTAR                 = 95,
    BB_SHADER_INPUT_FIELD_TEX_BRICK_SCALE                  = 96,
    BB_SHADER_INPUT_FIELD_TEX_BRICK_MORTAR_SIZE            = 97,
    BB_SHADER_INPUT_FIELD_TEX_BRICK_MORTAR_SMOOTH          = 98,
    BB_SHADER_INPUT_FIELD_TEX_BRICK_BIAS                   = 99,
    BB_SHADER_INPUT_FIELD_TEX_BRICK_BRICK_WIDTH            = 100,
    BB_SHADER_INPUT_FIELD_TEX_BRICK_ROW_HEIGHT             = 101,
    BB_SHADER_INPUT_FIELD_TEX_ENVIRONMENT_VECTOR           = 102,
    BB_SHADER_INPUT_FIELD_TEX_GABOR_VECTOR                 = 103,
    BB_SHADER_INPUT_FIELD_TEX_GABOR_SCALE                  = 104,
    BB_SHADER_INPUT_FIELD_TEX_GABOR_FREQUENCY              = 105,
    BB_SHADER_INPUT_FIELD_TEX_GABOR_ANISOTROPY             = 106,
    BB_SHADER_INPUT_FIELD_TEX_GABOR_ORIENTATION_2D         = 107,
    BB_SHADER_INPUT_FIELD_TEX_GABOR_ORIENTATION_3D         = 108,
    BB_SHADER_INPUT_FIELD_TEX_GRADIENT_VECTOR              = 109,
    BB_SHADER_INPUT_FIELD_TEX_MAGIC_VECTOR                 = 110,
    BB_SHADER_INPUT_FIELD_TEX_MAGIC_SCALE                  = 111,
    BB_SHADER_INPUT_FIELD_TEX_MAGIC_DISTORTION             = 112,
    BB_SHADER_INPUT_FIELD_TEX_VORONOI_VECTOR               = 113,
    BB_SHADER_INPUT_FIELD_TEX_VORONOI_W                    = 114,
    BB_SHADER_INPUT_FIELD_TEX_VORONOI_SCALE                = 115,
    BB_SHADER_INPUT_FIELD_TEX_VORONOI_DETAIL               = 116,
    BB_SHADER_INPUT_FIELD_TEX_VORONOI_ROUGHNESS            = 117,
    BB_SHADER_INPUT_FIELD_TEX_VORONOI_LACUNARITY           = 118,
    BB_SHADER_INPUT_FIELD_TEX_VORONOI_SMOOTHNESS           = 119,
    BB_SHADER_INPUT_FIELD_TEX_VORONOI_EXPONENT             = 120,
    BB_SHADER_INPUT_FIELD_TEX_VORONOI_RANDOMNESS           = 121,
    BB_SHADER_INPUT_FIELD_TEX_WAVE_VECTOR                  = 122,
    BB_SHADER_INPUT_FIELD_TEX_WAVE_SCALE                   = 123,
    BB_SHADER_INPUT_FIELD_TEX_WAVE_DISTORTION              = 124,
    BB_SHADER_INPUT_FIELD_TEX_WAVE_DETAIL                  = 125,
    BB_SHADER_INPUT_FIELD_TEX_WAVE_DETAIL_SCALE            = 126,
    BB_SHADER_INPUT_FIELD_TEX_WAVE_DETAIL_ROUGHNESS        = 127,
    BB_SHADER_INPUT_FIELD_TEX_WAVE_PHASE_OFFSET            = 128,
    BB_SHADER_INPUT_FIELD_TEX_WHITE_NOISE_VECTOR           = 129,
    BB_SHADER_INPUT_FIELD_TEX_WHITE_NOISE_W                = 130,
    BB_SHADER_INPUT_FIELD_VECTOR_DISPLACEMENT_VECTOR       = 131,
    BB_SHADER_INPUT_FIELD_VECTOR_DISPLACEMENT_MIDLEVEL     = 132,
    BB_SHADER_INPUT_FIELD_VECTOR_DISPLACEMENT_SCALE        = 133,
    BB_SHADER_INPUT_FIELD_VECTOR_ROTATE_VECTOR             = 134,
    BB_SHADER_INPUT_FIELD_VECTOR_ROTATE_CENTER             = 135,
    BB_SHADER_INPUT_FIELD_VECTOR_ROTATE_AXIS               = 136,
    BB_SHADER_INPUT_FIELD_VECTOR_ROTATE_ANGLE              = 137,
    BB_SHADER_INPUT_FIELD_VECTOR_ROTATE_ROTATION           = 138,
    BB_SHADER_INPUT_FIELD_VECTOR_TRANSFORM_VECTOR          = 139,
    BB_SHADER_INPUT_FIELD_GAMMA_COLOR                      = 140,
    BB_SHADER_INPUT_FIELD_GAMMA_GAMMA                      = 141,
    BB_SHADER_INPUT_FIELD_MATH_OPERATION                   = 142,
    BB_SHADER_INPUT_FIELD_MATH_USE_CLAMP                   = 143,
    BB_SHADER_INPUT_FIELD_VECTOR_MATH_OPERATION            = 144,
    BB_SHADER_INPUT_FIELD_MIX_BLEND_TYPE                   = 145,
    BB_SHADER_INPUT_FIELD_MIX_CLAMP_FACTOR                 = 146,
    BB_SHADER_INPUT_FIELD_MIX_CLAMP_RESULT                 = 147,
    BB_SHADER_INPUT_FIELD_MIX_DATA_TYPE                    = 148,
    BB_SHADER_INPUT_FIELD_MIX_FACTOR_MODE                  = 149,
    BB_SHADER_INPUT_FIELD_SEPARATE_COLOR_MODE              = 150,
    BB_SHADER_INPUT_FIELD_COMBINE_COLOR_MODE               = 151,
    BB_SHADER_INPUT_FIELD_TEX_NOISE_NOISE_DIMENSIONS       = 152,
    BB_SHADER_INPUT_FIELD_TEX_NOISE_NOISE_TYPE             = 153,
    BB_SHADER_INPUT_FIELD_TEX_NOISE_NORMALIZE              = 154,
    BB_SHADER_INPUT_FIELD_TEX_IMAGE_INTERPOLATION          = 155,
    BB_SHADER_INPUT_FIELD_TEX_IMAGE_PROJECTION             = 156,
    BB_SHADER_INPUT_FIELD_TEX_IMAGE_EXTENSION              = 157,
    BB_SHADER_INPUT_FIELD_TEX_IMAGE_IMAGE                  = 158,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_INTERPOLATION         = 159,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_ELEMENT_COUNT         = 160,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_0            = 161,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_1            = 162,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_2            = 163,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_3            = 164,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_4            = 165,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_5            = 166,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_6            = 167,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_7            = 168,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_0               = 169,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_1               = 170,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_2               = 171,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_3               = 172,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_4               = 173,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_5               = 174,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_6               = 175,
    BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_7               = 176,
    BB_SHADER_INPUT_FIELD_AMBIENT_OCCLUSION_SAMPLES        = 177,
    BB_SHADER_INPUT_FIELD_AMBIENT_OCCLUSION_INSIDE         = 178,
    BB_SHADER_INPUT_FIELD_AMBIENT_OCCLUSION_ONLY_LOCAL     = 179,
    BB_SHADER_INPUT_FIELD_BUMP_INVERT                      = 180,
    BB_SHADER_INPUT_FIELD_CLAMP_CLAMP_TYPE                 = 181,
    BB_SHADER_INPUT_FIELD_DISPLACEMENT_SPACE               = 182,
    BB_SHADER_INPUT_FIELD_MAP_RANGE_DATA_TYPE              = 183,
    BB_SHADER_INPUT_FIELD_MAP_RANGE_INTERPOLATION_TYPE     = 184,
    BB_SHADER_INPUT_FIELD_MAP_RANGE_CLAMP                  = 185,
    BB_SHADER_INPUT_FIELD_MAPPING_VECTOR_TYPE              = 186,
    BB_SHADER_INPUT_FIELD_NORMAL_MAP_SPACE                 = 187,
    BB_SHADER_INPUT_FIELD_NORMAL_MAP_UV_MAP                = 188,
    BB_SHADER_INPUT_FIELD_TANGENT_DIRECTION_TYPE           = 189,
    BB_SHADER_INPUT_FIELD_TANGENT_AXIS                     = 190,
    BB_SHADER_INPUT_FIELD_TANGENT_UV_MAP                   = 191,
    BB_SHADER_INPUT_FIELD_TEX_BRICK_OFFSET                 = 192,
    BB_SHADER_INPUT_FIELD_TEX_BRICK_OFFSET_FREQUENCY       = 193,
    BB_SHADER_INPUT_FIELD_TEX_BRICK_SQUASH                 = 194,
    BB_SHADER_INPUT_FIELD_TEX_BRICK_SQUASH_FREQUENCY       = 195,
    BB_SHADER_INPUT_FIELD_TEX_ENVIRONMENT_INTERPOLATION    = 196,
    BB_SHADER_INPUT_FIELD_TEX_ENVIRONMENT_PROJECTION       = 197,
    BB_SHADER_INPUT_FIELD_TEX_ENVIRONMENT_IMAGE            = 198,
    BB_SHADER_INPUT_FIELD_TEX_GABOR_GABOR_TYPE             = 199,
    BB_SHADER_INPUT_FIELD_TEX_GRADIENT_GRADIENT_TYPE       = 200,
    BB_SHADER_INPUT_FIELD_TEX_MAGIC_TURBULENCE_DEPTH       = 201,
    BB_SHADER_INPUT_FIELD_TEX_VORONOI_VORONOI_DIMENSIONS   = 202,
    BB_SHADER_INPUT_FIELD_TEX_VORONOI_FEATURE              = 203,
    BB_SHADER_INPUT_FIELD_TEX_VORONOI_DISTANCE             = 204,
    BB_SHADER_INPUT_FIELD_TEX_WAVE_WAVE_TYPE               = 205,
    BB_SHADER_INPUT_FIELD_TEX_WAVE_BANDS_DIRECTION         = 206,
    BB_SHADER_INPUT_FIELD_TEX_WAVE_RINGS_DIRECTION         = 207,
    BB_SHADER_INPUT_FIELD_TEX_WAVE_WAVE_PROFILE            = 208,
    BB_SHADER_INPUT_FIELD_TEX_WHITE_NOISE_NOISE_DIMENSIONS = 209,
    BB_SHADER_INPUT_FIELD_VECTOR_DISPLACEMENT_SPACE        = 210,
    BB_SHADER_INPUT_FIELD_VECTOR_ROTATE_ROTATION_TYPE      = 211,
    BB_SHADER_INPUT_FIELD_VECTOR_ROTATE_INVERT             = 212,
    BB_SHADER_INPUT_FIELD_VECTOR_TRANSFORM_VECTOR_TYPE     = 213,
    BB_SHADER_INPUT_FIELD_VECTOR_TRANSFORM_CONVERT_FROM    = 214,
    BB_SHADER_INPUT_FIELD_VECTOR_TRANSFORM_CONVERT_TO      = 215,
    BB_SHADER_INPUT_FIELD_VALUE_VALUE                      = 216,
} bb_shader_input_field_type_e;

typedef enum
{
    BB_SHADER_OUTPUT_FIELD_INPUT_POSITION                   = 0,
    BB_SHADER_OUTPUT_FIELD_INPUT_GENERATED                  = 1,
    BB_SHADER_OUTPUT_FIELD_INPUT_NORMAL                     = 2,
    BB_SHADER_OUTPUT_FIELD_INPUT_TANGENT                    = 3,
    BB_SHADER_OUTPUT_FIELD_INPUT_GEO_NORMAL                 = 4,
    BB_SHADER_OUTPUT_FIELD_INPUT_UV0                        = 5,
    BB_SHADER_OUTPUT_FIELD_INPUT_UV1                        = 6,
    BB_SHADER_OUTPUT_FIELD_INPUT_UV2                        = 7,
    BB_SHADER_OUTPUT_FIELD_INPUT_UV3                        = 8,
    BB_SHADER_OUTPUT_FIELD_INPUT_UV4                        = 9,
    BB_SHADER_OUTPUT_FIELD_INPUT_OBJECT                     = 10,
    BB_SHADER_OUTPUT_FIELD_INPUT_LOCATION                   = 11,
    BB_SHADER_OUTPUT_FIELD_UV_MAP_UV                        = 12,
    BB_SHADER_OUTPUT_FIELD_MATH_VALUE                       = 13,
    BB_SHADER_OUTPUT_FIELD_VECTOR_MATH_VECTOR               = 14,
    BB_SHADER_OUTPUT_FIELD_VECTOR_MATH_VALUE                = 15,
    BB_SHADER_OUTPUT_FIELD_MIX_RESULT_FLOAT                 = 16,
    BB_SHADER_OUTPUT_FIELD_MIX_RESULT_VECTOR                = 17,
    BB_SHADER_OUTPUT_FIELD_MIX_RESULT_COLOR                 = 18,
    BB_SHADER_OUTPUT_FIELD_MIX_RESULT_ROTATION              = 19,
    BB_SHADER_OUTPUT_FIELD_SEPARATE_COLOR_RED               = 20,
    BB_SHADER_OUTPUT_FIELD_SEPARATE_COLOR_GREEN             = 21,
    BB_SHADER_OUTPUT_FIELD_SEPARATE_COLOR_BLUE              = 22,
    BB_SHADER_OUTPUT_FIELD_COMBINE_COLOR_COLOR              = 23,
    BB_SHADER_OUTPUT_FIELD_COMBINE_XYZ_VECTOR               = 24,
    BB_SHADER_OUTPUT_FIELD_SEPARATE_XYZ_X                   = 25,
    BB_SHADER_OUTPUT_FIELD_SEPARATE_XYZ_Y                   = 26,
    BB_SHADER_OUTPUT_FIELD_SEPARATE_XYZ_Z                   = 27,
    BB_SHADER_OUTPUT_FIELD_RGB_COLOR                        = 28,
    BB_SHADER_OUTPUT_FIELD_BRIGHT_CONTRAST_COLOR            = 29,
    BB_SHADER_OUTPUT_FIELD_RGB_TO_BW_VAL                    = 30,
    BB_SHADER_OUTPUT_FIELD_HUE_SATURATION_COLOR             = 31,
    BB_SHADER_OUTPUT_FIELD_INVERT_COLOR                     = 32,
    BB_SHADER_OUTPUT_FIELD_FRESNEL_FAC                      = 33,
    BB_SHADER_OUTPUT_FIELD_TEX_NOISE_FAC                    = 34,
    BB_SHADER_OUTPUT_FIELD_TEX_NOISE_COLOR                  = 35,
    BB_SHADER_OUTPUT_FIELD_TEX_IMAGE_COLOR                  = 36,
    BB_SHADER_OUTPUT_FIELD_TEX_IMAGE_ALPHA                  = 37,
    BB_SHADER_OUTPUT_FIELD_VAL_TO_RGB_COLOR                 = 38,
    BB_SHADER_OUTPUT_FIELD_VAL_TO_RGB_ALPHA                 = 39,
    BB_SHADER_OUTPUT_FIELD_AMBIENT_OCCLUSION_COLOR          = 40,
    BB_SHADER_OUTPUT_FIELD_AMBIENT_OCCLUSION_AO             = 41,
    BB_SHADER_OUTPUT_FIELD_BUMP_NORMAL                      = 42,
    BB_SHADER_OUTPUT_FIELD_CLAMP_RESULT                     = 43,
    BB_SHADER_OUTPUT_FIELD_DISPLACEMENT_DISPLACEMENT        = 44,
    BB_SHADER_OUTPUT_FIELD_MAP_RANGE_RESULT                 = 45,
    BB_SHADER_OUTPUT_FIELD_MAP_RANGE_VECTOR                 = 46,
    BB_SHADER_OUTPUT_FIELD_MAPPING_VECTOR                   = 47,
    BB_SHADER_OUTPUT_FIELD_NORMAL_MAP_NORMAL                = 48,
    BB_SHADER_OUTPUT_FIELD_TANGENT_TANGENT                  = 49,
    BB_SHADER_OUTPUT_FIELD_TEX_BRICK_COLOR                  = 50,
    BB_SHADER_OUTPUT_FIELD_TEX_BRICK_FAC                    = 51,
    BB_SHADER_OUTPUT_FIELD_TEX_ENVIRONMENT_COLOR            = 52,
    BB_SHADER_OUTPUT_FIELD_TEX_GABOR_VALUE                  = 53,
    BB_SHADER_OUTPUT_FIELD_TEX_GABOR_PHASE                  = 54,
    BB_SHADER_OUTPUT_FIELD_TEX_GABOR_INTENSITY              = 55,
    BB_SHADER_OUTPUT_FIELD_TEX_GRADIENT_COLOR               = 56,
    BB_SHADER_OUTPUT_FIELD_TEX_GRADIENT_FAC                 = 57,
    BB_SHADER_OUTPUT_FIELD_TEX_MAGIC_COLOR                  = 58,
    BB_SHADER_OUTPUT_FIELD_TEX_MAGIC_FAC                    = 59,
    BB_SHADER_OUTPUT_FIELD_TEX_VORONOI_DISTANCE             = 60,
    BB_SHADER_OUTPUT_FIELD_TEX_VORONOI_COLOR                = 61,
    BB_SHADER_OUTPUT_FIELD_TEX_VORONOI_POSITION             = 62,
    BB_SHADER_OUTPUT_FIELD_TEX_VORONOI_W                    = 63,
    BB_SHADER_OUTPUT_FIELD_TEX_VORONOI_RADIUS               = 64,
    BB_SHADER_OUTPUT_FIELD_TEX_WAVE_COLOR                   = 65,
    BB_SHADER_OUTPUT_FIELD_TEX_WAVE_FAC                     = 66,
    BB_SHADER_OUTPUT_FIELD_TEX_WHITE_NOISE_VALUE            = 67,
    BB_SHADER_OUTPUT_FIELD_TEX_WHITE_NOISE_COLOR            = 68,
    BB_SHADER_OUTPUT_FIELD_VECTOR_DISPLACEMENT_DISPLACEMENT = 69,
    BB_SHADER_OUTPUT_FIELD_VECTOR_ROTATE_VECTOR             = 70,
    BB_SHADER_OUTPUT_FIELD_VECTOR_TRANSFORM_VECTOR          = 71,
    BB_SHADER_OUTPUT_FIELD_VALUE_VALUE                      = 72,
    BB_SHADER_OUTPUT_FIELD_GAMMA_COLOR                      = 73,
} bb_shader_output_field_type_e;

typedef enum
{
    BB_SHADER_VALUE_TYPE_BOOL   = 1,
    BB_SHADER_VALUE_TYPE_INT    = 2,
    BB_SHADER_VALUE_TYPE_FLOAT  = 3,
    BB_SHADER_VALUE_TYPE_UV_MAP = 4,
    BB_SHADER_VALUE_TYPE_TEX    = 5,
    BB_SHADER_VALUE_TYPE_FLOAT2 = 6,
    BB_SHADER_VALUE_TYPE_FLOAT3 = 7,
    BB_SHADER_VALUE_TYPE_FLOAT4 = 8,
    BB_SHADER_VALUE_TYPE_COL    = 9,
} bb_shader_value_type_e;

typedef struct
{
    bb_shader_input_field_type_e field_type;
    bb_shader_value_type_e value_type;
    union
    {
        int32_t bool_val;
        int32_t int_val;
        float float_val;
        const char* str_val;
        float vec2_val[2];
        float vec3_val[3];
        float vec4_val[4];
    };
} BBArchiveShaderField;

typedef struct
{
    bb_shader_node_type_e node_type;
    uint32_t field_count;
    const BBArchiveShaderField* fields;
} BBArchiveShaderNode;

typedef struct
{
    uint32_t src_idx;
    uint32_t dst_idx;
    bb_shader_output_field_type_e from_field;
    bb_shader_input_field_type_e to_field;
} BBArchiveShaderLink;

typedef struct
{
    uint32_t node_idx;
    BBArchiveShaderField field;
} BBArchiveShaderMatData;

typedef struct
{
    const char* shader_name;
    uint32_t node_count;
    const BBArchiveShaderNode* nodes;
    uint32_t link_count;
    const BBArchiveShaderLink* links;
    uint32_t mat_data_count;
    const BBArchiveShaderMatData* mat_data;
} BBArchiveShaderGraph;

// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
typedef struct
{
    uint32_t vertex_count;
    const float* positions;
    const float* uv0;
    const float* uv1;
    const float* uv2;
    const uint8_t* joint_indices;
    const float* joint_weights;
} BBArchiveSubmesh;

typedef struct
{
    const char* mesh_name;
    uint32_t submesh_count;
    const BBArchiveSubmesh* submeshes;
} BBArchiveMesh;

typedef struct
{
    const char* image_name;
    uint32_t width;
    uint32_t height;
    uint32_t num_channels;
    uint32_t bits_per_channel;
    bool is_srgb;
    uint32_t mip_levels;
    const uint8_t* blob;
    uint64_t blob_size;
} BBArchiveImage;

typedef struct
{
    const char* skeleton_name;
    uint32_t bone_count;
    const char* const* bone_names;
    const int32_t* parent_bone_indices;
    const float* bind_local_matrices;
    const float* inverse_bind_matrices;
} BBArchiveSkel;

typedef struct
{
    const char* animation_name;
    uint32_t channel_count;
    const uint32_t* bone_indices;
    const uint32_t* channel_types;
    const uint32_t* interpolation_types;
    const uint32_t* keyframe_counts;
    const float* keyframe_times;
    const float* keyframe_values;
} BBArchiveAnim;

typedef struct
{
    const char* mesh_name;
    float pos[3];
    float rot[3];
    float sca[3];
    uint32_t mat_count;
    const char* const* mat_names;
} BBArchiveSceneElem;

typedef struct
{
    const char* scene_name;
    uint32_t elem_count;
    const BBArchiveSceneElem* elems;
} BBArchiveScene;

typedef struct
{
    uint32_t mesh_count;
    uint32_t image_count;
    uint32_t skeleton_count;
    uint32_t animation_count;
    uint32_t shader_graph_count;
    uint32_t scene_count;
    const BBArchiveMesh* meshes;
    const BBArchiveImage* images;
    const BBArchiveSkel* skeletons;
    const BBArchiveAnim* animations;
    const BBArchiveShaderGraph* shader_graphs;
    const BBArchiveScene* scenes;
} BBArchiveInfo;

// =========================================================================================================================================
// =========================================================================================================================================
// bb_archive_write: Process all meshes and images and write to directory-based output format.
// This function is additive: if the archive directory already exists, it will contribute to it
// seamlessly, overwriting existing assets with new updates if they already exist.
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C void bb_archive_write(const BBArchiveInfo* info, const char* output_dir);

// =========================================================================================================================================
// =========================================================================================================================================
// bb_archive_read: Validate and read an existing bb_archive directory structure.
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C void bb_archive_read(arena_zh arena, const char* archive_dir, BBArchiveInfo* info);
