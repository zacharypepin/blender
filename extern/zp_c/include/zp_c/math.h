#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C extern
#endif

// =========================================================================================================================================
// =========================================================================================================================================
// Core Conventions - VULKAN CONVENTION (NOT OpenGL)
// =========================================================================================================================================
// =========================================================================================================================================
//
// This math library follows Vulkan conventions:
//   - Row-major matrix storage
//   - Row vectors (v * M, NOT M * v)
//   - Projection matrices map z to [0, 1] (Vulkan), NOT [-1, 1] (OpenGL)
//
// Coordinate system:
//   - Right-handed coordinate system
//   - Z-axis points UP
//   - Y-axis points FORWARD
//   - X-axis points RIGHT
//   - Follows the right-hand rule: X × Y = Z
//
// Camera/View conventions:
//   - In view space, the camera looks along -Z
//   - This means the camera's forward direction is -Z in view space
//
// Vector representation:
//   - Treat vectors as row vectors: 2D: [x, y], 3D: [x, y, z], 4D: [x, y, z, w]
//   - When multiplying by matrices, vectors are always on the left: v' = v * M
//   - Do not use column-vector forms like M * v
//
// Matrix representation:
//   - Matrices are row-major in both:
//     * Conceptual math: first index = row, second index = column
//     * Memory layout: rows stored contiguously
//   - Indexing rule: M[i][j] = element at row i, column j
//   - Memory address formula (for a C-style T* data backing array):
//     data[i * numCols + j] corresponds to M[i][j]
//
// Matrix-vector multiplication:
//   - For v = 1×N, M = N×N: v' = v * M
//   - Component-wise: v'[j] = sum_over_k( v[k] * M[k][j] )
//   - Implement mul(v, M) / operator*(Vector, Matrix) but not Matrix * Vector
//
// Matrix-matrix multiplication:
//   - Use standard row-major rules, consistent with v' = v * M
//   - If A and B are N×N: C = A * B
//   - Applying transforms: v' = v * A * B
//   - This means: apply A first, then B
//   - So left-to-right read order equals execution order of transforms
//
// Transform Matrices (4×4):
//   - Assume 3D with homogeneous row vectors [x, y, z, 1] and row-major 4×4 matrices
//
//   Translation:
//     - Translation components go in the last row, first 3 columns:
//       M[3][0] = tx, M[3][1] = ty, M[3][2] = tz
//     - The last row is: [tx, ty, tz, 1] for a pure translation
//
//   Scaling:
//     - Diagonal elements for uniform/non-uniform scale:
//       M[0][0] = sx, M[1][1] = sy, M[2][2] = sz, M[3][3] = 1
//
//   Rotation:
//     - Use standard 3×3 rotation block in the upper-left
//     - Rows represent basis vectors in world space under row-vector convention
//     - Example: rotation around Z axis by θ:
//       M[0][0] = cosθ, M[0][1] = sinθ, M[0][2] = 0
//       M[1][0] = -sinθ, M[1][1] = cosθ, M[1][2] = 0
//       M[2][0] = 0, M[2][1] = 0, M[2][2] = 1
//       M[3][3] = 1
//
//   Projection matrices:
//     - Perspective and orthographic projection matrices map z from [near, far] to [0, 1]
//     - This is Vulkan convention (NOT OpenGL which maps to [-1, 1])
//
//   Transform application order:
//     - For transforms T, R, S:
//       To scale, then rotate, then translate a vector v: v' = v * S * R * T
//     - Ensure documentation clearly states: left-to-right multiplication order = order of application

// =========================================================================================================================================
// =========================================================================================================================================
// Basic vector types
// =========================================================================================================================================
// =========================================================================================================================================
typedef struct
{
    float x;
    float y;
} vec2_zt;

typedef struct
{
    float x;
    float y;
    float z;
} vec3_zt;

typedef struct
{
    float x;
    float y;
    float z;
    float w;
} vec4_zt;

typedef struct
{
    int x;
    int y;
} ivec2_zt;

typedef struct
{
    int x;
    int y;
    int z;
} ivec3_zt;

typedef struct
{
    int x;
    int y;
    int z;
    int w;
} ivec4_zt;

// =========================================================================================================================================
// =========================================================================================================================================
// Quaternion type
// =========================================================================================================================================
// =========================================================================================================================================
typedef struct
{
    float x;
    float y;
    float z;
    float w;
} quat_zt;

// =========================================================================================================================================
// =========================================================================================================================================
// Matrix types
// =========================================================================================================================================
// =========================================================================================================================================
// Matrices are stored in row-major format:
//   - f[i][j] = element at row i, column j
//   - Rows are stored contiguously in memory
//   - For mat4_zt: row 0 = [f[0][0], f[0][1], f[0][2], f[0][3]]
//                   row 1 = [f[1][0], f[1][1], f[1][2], f[1][3]]
//                   etc.
typedef struct
{
    float f[3][3];
} mat3_zt;

typedef struct
{
    float f[4][4];
} mat4_zt;

// =========================================================================================================================================
// =========================================================================================================================================
// Bounding box types
// =========================================================================================================================================
// =========================================================================================================================================
typedef struct
{
    vec2_zt min;
    vec2_zt max;
} bb2_zt;

typedef struct
{
    vec3_zt min;
    vec3_zt max;
} bb3_zt;

typedef struct
{
    vec4_zt min;
    vec4_zt max;
} bb4_zt;

// =========================================================================================================================================
// =========================================================================================================================================
// Rectangle type
// =========================================================================================================================================
// =========================================================================================================================================
typedef struct
{
    vec2_zt xy;
    vec2_zt wh;
} rect_zt;

// =========================================================================================================================================
// =========================================================================================================================================
// Spherical coordinates
// =========================================================================================================================================
// =========================================================================================================================================
typedef struct
{
    double radius;
    double azimuth;
    double polar;
} spherical_coords_zt;

// =========================================================================================================================================
// =========================================================================================================================================
// Intersection result
// =========================================================================================================================================
// =========================================================================================================================================
typedef struct
{
    bool hit;
    float distance;
} intersection_result_zt;

// =========================================================================================================================================
// =========================================================================================================================================
// Orbit camera types
// =========================================================================================================================================
// =========================================================================================================================================
typedef struct
{
    vec3_zt start_pos;
    vec3_zt start_target_pos;
    float start_fov;
    float aspect_ratio;
} orbit_camera_config_zt;

typedef struct
{
    vec3_zt position;
    vec3_zt target_pos;
    float vert_fov_deg;
    float aspect_ratio;
} orbit_camera_zt;

// =========================================================================================================================================
// =========================================================================================================================================
// Constants
// =========================================================================================================================================
// =========================================================================================================================================
#define MATH_RIGHT   ((vec3_zt){1.0f, 0.0f, 0.0f})
#define MATH_FORWARD ((vec3_zt){0.0f, 1.0f, 0.0f})
#define MATH_UP      ((vec3_zt){0.0f, 0.0f, 1.0f})

// =========================================================================================================================================
// =========================================================================================================================================
// vec2 operations
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C vec2_zt vec2_add_z(vec2_zt a, vec2_zt b);
EXTERN_C vec2_zt vec2_sub_z(vec2_zt a, vec2_zt b);
EXTERN_C vec2_zt vec2_mul_z(vec2_zt a, vec2_zt b);
EXTERN_C vec2_zt vec2_div_z(vec2_zt a, vec2_zt b);
EXTERN_C vec2_zt vec2_mul_scalar_z(vec2_zt v, float s);
EXTERN_C vec2_zt vec2_div_scalar_z(vec2_zt v, float s);
EXTERN_C vec2_zt vec2_neg_z(vec2_zt v);
EXTERN_C bool vec2_eq_z(vec2_zt a, vec2_zt b);
EXTERN_C float vec2_dot_z(vec2_zt a, vec2_zt b);
EXTERN_C float vec2_cross_z(vec2_zt a, vec2_zt b);
EXTERN_C float vec2_length2_z(vec2_zt v);
EXTERN_C float vec2_length_z(vec2_zt v);
EXTERN_C vec2_zt vec2_normalize_z(vec2_zt v);
EXTERN_C float vec2_distance_z(vec2_zt a, vec2_zt b);
EXTERN_C vec2_zt vec2_lerp_z(vec2_zt a, vec2_zt b, float t);
EXTERN_C vec2_zt vec2_min_z(vec2_zt a, vec2_zt b);
EXTERN_C vec2_zt vec2_max_z(vec2_zt a, vec2_zt b);
EXTERN_C vec2_zt vec2_clamp_z(vec2_zt v, vec2_zt lo, vec2_zt hi);

// =========================================================================================================================================
// =========================================================================================================================================
// vec3 operations
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C vec3_zt vec3_add_z(vec3_zt a, vec3_zt b);
EXTERN_C vec3_zt vec3_sub_z(vec3_zt a, vec3_zt b);
EXTERN_C vec3_zt vec3_mul_z(vec3_zt a, vec3_zt b);
EXTERN_C vec3_zt vec3_div_z(vec3_zt a, vec3_zt b);
EXTERN_C vec3_zt vec3_mul_scalar_z(vec3_zt v, float s);
EXTERN_C vec3_zt vec3_div_scalar_z(vec3_zt v, float s);
EXTERN_C vec3_zt vec3_neg_z(vec3_zt v);
EXTERN_C bool vec3_eq_z(vec3_zt a, vec3_zt b);
EXTERN_C float vec3_dot_z(vec3_zt a, vec3_zt b);
EXTERN_C vec3_zt vec3_cross_z(vec3_zt a, vec3_zt b);
EXTERN_C float vec3_length2_z(vec3_zt v);
EXTERN_C float vec3_length_z(vec3_zt v);
EXTERN_C vec3_zt vec3_normalize_z(vec3_zt v);
EXTERN_C float vec3_distance_z(vec3_zt a, vec3_zt b);
EXTERN_C vec3_zt vec3_lerp_z(vec3_zt a, vec3_zt b, float t);
EXTERN_C vec3_zt vec3_min_z(vec3_zt a, vec3_zt b);
EXTERN_C vec3_zt vec3_max_z(vec3_zt a, vec3_zt b);
EXTERN_C vec3_zt vec3_clamp_z(vec3_zt v, vec3_zt lo, vec3_zt hi);

// =========================================================================================================================================
// =========================================================================================================================================
// vec4 operations
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C vec4_zt vec4_add_z(vec4_zt a, vec4_zt b);
EXTERN_C vec4_zt vec4_sub_z(vec4_zt a, vec4_zt b);
EXTERN_C vec4_zt vec4_mul_z(vec4_zt a, vec4_zt b);
EXTERN_C vec4_zt vec4_div_z(vec4_zt a, vec4_zt b);
EXTERN_C vec4_zt vec4_mul_scalar_z(vec4_zt v, float s);
EXTERN_C vec4_zt vec4_div_scalar_z(vec4_zt v, float s);
EXTERN_C vec4_zt vec4_neg_z(vec4_zt v);
EXTERN_C bool vec4_eq_z(vec4_zt a, vec4_zt b);
EXTERN_C float vec4_dot_z(vec4_zt a, vec4_zt b);
EXTERN_C float vec4_length2_z(vec4_zt v);
EXTERN_C float vec4_length_z(vec4_zt v);
EXTERN_C vec4_zt vec4_normalize_z(vec4_zt v);
EXTERN_C float vec4_distance_z(vec4_zt a, vec4_zt b);
EXTERN_C vec4_zt vec4_lerp_z(vec4_zt a, vec4_zt b, float t);
EXTERN_C vec4_zt vec4_min_z(vec4_zt a, vec4_zt b);
EXTERN_C vec4_zt vec4_max_z(vec4_zt a, vec4_zt b);
EXTERN_C vec4_zt vec4_clamp_z(vec4_zt v, vec4_zt lo, vec4_zt hi);

// =========================================================================================================================================
// =========================================================================================================================================
// ivec2 operations
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C ivec2_zt ivec2_add_z(ivec2_zt a, ivec2_zt b);
EXTERN_C ivec2_zt ivec2_sub_z(ivec2_zt a, ivec2_zt b);
EXTERN_C ivec2_zt ivec2_mul_z(ivec2_zt a, ivec2_zt b);
EXTERN_C ivec2_zt ivec2_div_z(ivec2_zt a, ivec2_zt b);
EXTERN_C ivec2_zt ivec2_mul_scalar_z(ivec2_zt v, int s);
EXTERN_C ivec2_zt ivec2_div_scalar_z(ivec2_zt v, int s);
EXTERN_C ivec2_zt ivec2_neg_z(ivec2_zt v);
EXTERN_C bool ivec2_eq_z(ivec2_zt a, ivec2_zt b);

// =========================================================================================================================================
// =========================================================================================================================================
// ivec3 operations
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C ivec3_zt ivec3_add_z(ivec3_zt a, ivec3_zt b);
EXTERN_C ivec3_zt ivec3_sub_z(ivec3_zt a, ivec3_zt b);
EXTERN_C ivec3_zt ivec3_mul_z(ivec3_zt a, ivec3_zt b);
EXTERN_C ivec3_zt ivec3_div_z(ivec3_zt a, ivec3_zt b);
EXTERN_C ivec3_zt ivec3_mul_scalar_z(ivec3_zt v, int s);
EXTERN_C ivec3_zt ivec3_div_scalar_z(ivec3_zt v, int s);
EXTERN_C ivec3_zt ivec3_neg_z(ivec3_zt v);
EXTERN_C bool ivec3_eq_z(ivec3_zt a, ivec3_zt b);

// =========================================================================================================================================
// =========================================================================================================================================
// ivec4 operations
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C ivec4_zt ivec4_add_z(ivec4_zt a, ivec4_zt b);
EXTERN_C ivec4_zt ivec4_sub_z(ivec4_zt a, ivec4_zt b);
EXTERN_C ivec4_zt ivec4_mul_z(ivec4_zt a, ivec4_zt b);
EXTERN_C ivec4_zt ivec4_div_z(ivec4_zt a, ivec4_zt b);
EXTERN_C ivec4_zt ivec4_mul_scalar_z(ivec4_zt v, int s);
EXTERN_C ivec4_zt ivec4_div_scalar_z(ivec4_zt v, int s);
EXTERN_C ivec4_zt ivec4_neg_z(ivec4_zt v);
EXTERN_C bool ivec4_eq_z(ivec4_zt a, ivec4_zt b);

// =========================================================================================================================================
// =========================================================================================================================================
// quat operations
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C quat_zt quat_add_z(quat_zt a, quat_zt b);
EXTERN_C quat_zt quat_sub_z(quat_zt a, quat_zt b);
EXTERN_C quat_zt quat_mul_z(quat_zt a, quat_zt b);
EXTERN_C quat_zt quat_mul_scalar_z(quat_zt q, float s);
EXTERN_C quat_zt quat_div_scalar_z(quat_zt q, float s);
EXTERN_C quat_zt quat_neg_z(quat_zt q);
EXTERN_C bool quat_eq_z(quat_zt a, quat_zt b);
EXTERN_C quat_zt quat_normalize_z(quat_zt q);
EXTERN_C quat_zt quat_rotation_to_face_z(vec3_zt dir_to_face);
EXTERN_C quat_zt quat_slerp_z(quat_zt q0, quat_zt q1, float f);

// =========================================================================================================================================
// =========================================================================================================================================
// mat3 operations
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C mat3_zt mat3_identity_z(void);
EXTERN_C mat3_zt mat3_from_rows_z(vec3_zt r0, vec3_zt r1, vec3_zt r2);
EXTERN_C mat3_zt mat3_mul_z(mat3_zt a, mat3_zt b);
EXTERN_C vec3_zt mat3_mul_vec3_z(vec3_zt v, mat3_zt m);
EXTERN_C mat3_zt mat3_add_z(mat3_zt a, mat3_zt b);
EXTERN_C mat3_zt mat3_sub_z(mat3_zt a, mat3_zt b);
EXTERN_C mat3_zt mat3_mul_scalar_z(mat3_zt m, float s);
EXTERN_C mat3_zt mat3_div_scalar_z(mat3_zt m, float s);
EXTERN_C bool mat3_eq_z(mat3_zt a, mat3_zt b);
EXTERN_C mat3_zt mat3_transpose_z(mat3_zt m);
EXTERN_C mat3_zt mat3_inverse_z(mat3_zt m);

// =========================================================================================================================================
// =========================================================================================================================================
// mat4 operations
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C mat4_zt mat4_identity_z(void);
EXTERN_C mat4_zt mat4_from_rows_z(vec4_zt r0, vec4_zt r1, vec4_zt r2, vec4_zt r3);
EXTERN_C mat4_zt mat4_mul_z(mat4_zt a, mat4_zt b);
EXTERN_C vec4_zt mat4_mul_vec4_z(vec4_zt v, mat4_zt m);
EXTERN_C mat4_zt mat4_add_z(mat4_zt a, mat4_zt b);
EXTERN_C mat4_zt mat4_sub_z(mat4_zt a, mat4_zt b);
EXTERN_C mat4_zt mat4_mul_scalar_z(mat4_zt m, float s);
EXTERN_C mat4_zt mat4_div_scalar_z(mat4_zt m, float s);
EXTERN_C bool mat4_eq_z(mat4_zt a, mat4_zt b);
EXTERN_C mat4_zt mat4_transpose_z(mat4_zt m);
EXTERN_C float mat4_determinant_z(mat4_zt m);
EXTERN_C mat4_zt mat4_inverse_z(mat4_zt m);

// =========================================================================================================================================
// =========================================================================================================================================
// Matrix builders
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C mat4_zt mat4_translate_z(vec3_zt t);
EXTERN_C mat4_zt mat4_scale_z(vec3_zt s);
EXTERN_C mat4_zt mat4_rotate_x_z(float radians);
EXTERN_C mat4_zt mat4_rotate_y_z(float radians);
EXTERN_C mat4_zt mat4_rotate_z_z(float radians);
EXTERN_C mat4_zt mat4_rotate_axis_angle_z(vec3_zt axis, float radians);
EXTERN_C mat4_zt mat4_look_at_z(vec3_zt eye, vec3_zt center, vec3_zt up);
EXTERN_C mat4_zt mat4_perspective_z(float fovy_radians, float aspect, float z_near, float z_far);
EXTERN_C mat4_zt mat4_ortho_z(float left, float right, float bottom, float top, float z_near, float z_far);

// =========================================================================================================================================
// =========================================================================================================================================
// Transform helpers
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C mat4_zt mat4_get_transform_z(vec3_zt translate, quat_zt rot, vec3_zt sca);
EXTERN_C mat4_zt mat4_get_transform_euler_z(vec3_zt translate, vec3_zt euler_rot, vec3_zt sca);
EXTERN_C mat3_zt mat4_model_to_nrm_z(mat4_zt model_mat);
EXTERN_C ivec2_zt vec2_to_ivec2_z(vec2_zt vec);

// =========================================================================================================================================
// =========================================================================================================================================
// Spherical coordinates
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C spherical_coords_zt math_cartesian_to_polar_z(vec3_zt cartesian);
EXTERN_C vec3_zt math_polar_to_cartesian_z(spherical_coords_zt polar);
EXTERN_C bool math_are_parallel_z(vec3_zt v1, vec3_zt v2);

// =========================================================================================================================================
// =========================================================================================================================================
// Orbit camera
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C void orbit_camera_init_z(orbit_camera_zt* p_camera, orbit_camera_config_zt config);
EXTERN_C mat4_zt orbit_camera_get_model_mat_z(orbit_camera_zt* p_camera);
EXTERN_C float orbit_camera_get_near_plane_height_z(orbit_camera_zt* p_camera, float near_plane_dist);
EXTERN_C float orbit_camera_get_near_plane_width_z(orbit_camera_zt* p_camera, float near_plane_height);
EXTERN_C vec3_zt orbit_camera_get_forward_axis_z(orbit_camera_zt* p_camera);
EXTERN_C vec3_zt orbit_camera_get_right_axis_z(orbit_camera_zt* p_camera);
EXTERN_C vec3_zt orbit_camera_get_up_axis_z(orbit_camera_zt* p_camera);
EXTERN_C mat4_zt orbit_camera_get_view_mat_z(orbit_camera_zt* p_camera);
EXTERN_C mat4_zt orbit_camera_get_proj_mat_z(orbit_camera_zt* p_camera, float near_dist, float far_dist);
EXTERN_C vec3_zt orbit_camera_screen_point_to_near_world_z(orbit_camera_zt* p_camera, vec2_zt nrm_point, float near_dist, float far_dist);
EXTERN_C vec3_zt orbit_camera_screen_point_to_ray_dir_z(orbit_camera_zt* p_camera, vec2_zt nrm_point, float near_dist, float far_dist);
EXTERN_C void orbit_camera_rotate_z(orbit_camera_zt* p_camera, vec2_zt delta, float speed);
EXTERN_C void orbit_camera_pan_z(orbit_camera_zt* p_camera, vec2_zt delta, float speed);
EXTERN_C void orbit_camera_zoom_z(orbit_camera_zt* p_camera, float delta, float speed);

// =========================================================================================================================================
// =========================================================================================================================================
// Intersection
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C intersection_result_zt intersection_result_make_z(float dist);
EXTERN_C intersection_result_zt intersection_result_miss_z(void);
EXTERN_C bool intersection_result_lt_z(intersection_result_zt a, intersection_result_zt b);
EXTERN_C intersection_result_zt math_check_ray_aabb_intersection_z(vec3_zt ray_position, vec3_zt ray_direction, vec3_zt bb_min, vec3_zt bb_max);

// =========================================================================================================================================
// =========================================================================================================================================
// Utility functions
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C float math_proportion_z(uint64_t i0, uint64_t i1);

// =========================================================================================================================================
// =========================================================================================================================================
// Splines
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C uint32_t splines_sample_bezier_tris_z(vec2_zt screen_size, const vec2_zt cp[4], int segments, float width_pixels, vec2_zt* out_triangles, uint32_t out_capacity);

// =========================================================================================================================================
// =========================================================================================================================================
// Normal and tangent calculation
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C void calc_nrms_z(const float* positions, uint32_t vertex_count, float* normals);
EXTERN_C void calc_tans_z(const float* positions, const float* uvs, uint32_t vertex_count, float* tangents);

// =========================================================================================================================================
// =========================================================================================================================================
// String conversion (writes to provided buffer, returns pointer to buffer)
// =========================================================================================================================================
// =========================================================================================================================================
EXTERN_C const char* vec2_to_string_z(vec2_zt v, char* buf, size_t buf_size);
EXTERN_C const char* vec3_to_string_z(vec3_zt v, char* buf, size_t buf_size);
EXTERN_C const char* vec4_to_string_z(vec4_zt v, char* buf, size_t buf_size);
EXTERN_C const char* ivec2_to_string_z(ivec2_zt v, char* buf, size_t buf_size);
EXTERN_C const char* ivec3_to_string_z(ivec3_zt v, char* buf, size_t buf_size);
EXTERN_C const char* ivec4_to_string_z(ivec4_zt v, char* buf, size_t buf_size);
EXTERN_C const char* quat_to_string_z(quat_zt q, char* buf, size_t buf_size);
EXTERN_C const char* mat3_to_string_z(mat3_zt m, char* buf, size_t buf_size);
EXTERN_C const char* mat4_to_string_z(mat4_zt m, char* buf, size_t buf_size);
EXTERN_C const char* bb2_to_string_z(bb2_zt bb, char* buf, size_t buf_size);
EXTERN_C const char* bb3_to_string_z(bb3_zt bb, char* buf, size_t buf_size);
EXTERN_C const char* bb4_to_string_z(bb4_zt bb, char* buf, size_t buf_size);
