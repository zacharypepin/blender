#include <bb_archive/bb_archive.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zp_c/fs.h>
#include <zp_c/arena.h>
#include <zp_c/json.h>
#include <zp_c/hash.h>
#include <zp_c/fatal.h>

// =========================================================================================================================================
// =========================================================================================================================================
// write_mesh_manifest: Write JSON manifest for a mesh with submeshes array.
// =========================================================================================================================================
// =========================================================================================================================================
static void write_mesh_manifest(
    arena_zh arena, const BBArchiveMesh* mesh, const char* output_dir, const char** pos_blob_filenames, const char** uv0_blob_filenames, const char** uv1_blob_filenames, const char** uv2_blob_filenames, const char** joint_indices_blob_filenames, const char** joint_weights_blob_filenames
)
{
    fatal_check_z(arena, "arena is null");
    fatal_check_z(mesh, "mesh is null");
    fatal_check_z(output_dir, "output_dir is null");

    // =============================================================================================
    // =============================================================================================
    // Build JSON object using zp_c/json.
    // =============================================================================================
    // =============================================================================================
    json_zh root;
    json_zh submeshes;
    {
        root      = json_object_z(arena);
        submeshes = json_array_z(arena);

        json_object_set_z(root, "mesh_name", json_string_z(arena, mesh->mesh_name));

        // =============================================================================================
        // =============================================================================================
        // Build submeshes array.
        // =============================================================================================
        // =============================================================================================
        for (uint32_t submesh_idx = 0; submesh_idx < mesh->submesh_count; submesh_idx++)
        {
            const BBArchiveSubmesh* submesh = &mesh->submeshes[submesh_idx];

            json_zh submesh_obj             = json_object_z(arena);
            json_zh attributes              = json_object_z(arena);
            json_zh positions               = json_object_z(arena);
            json_zh uv0_obj                 = nullptr;
            json_zh uv1_obj                 = nullptr;
            json_zh uv2_obj                 = nullptr;
            json_zh joint_indices_obj       = nullptr;
            json_zh joint_weights_obj       = nullptr;

            json_object_set_z(submesh_obj, "vertex_count", json_integer_z(arena, submesh->vertex_count));
            json_object_set_z(submesh_obj, "triangle_count", json_integer_z(arena, submesh->vertex_count / 3));

            // Set positions attribute
            json_object_set_z(positions, "blob", json_string_z(arena, pos_blob_filenames[submesh_idx]));
            json_object_set_z(positions, "format", json_string_z(arena, "float32"));
            json_object_set_z(positions, "components", json_integer_z(arena, 3));
            json_object_set_z(positions, "num", json_integer_z(arena, submesh->vertex_count));
            json_object_set_z(attributes, "positions", positions);

            // Add UV channel attributes
            if (submesh->uv0 && uv0_blob_filenames && uv0_blob_filenames[submesh_idx])
            {
                uv0_obj = json_object_z(arena);
                json_object_set_z(uv0_obj, "blob", json_string_z(arena, uv0_blob_filenames[submesh_idx]));
                json_object_set_z(uv0_obj, "format", json_string_z(arena, "float32"));
                json_object_set_z(uv0_obj, "components", json_integer_z(arena, 2));
                json_object_set_z(uv0_obj, "num", json_integer_z(arena, submesh->vertex_count));
                json_object_set_z(attributes, "uv0", uv0_obj);
            }

            if (submesh->uv1 && uv1_blob_filenames && uv1_blob_filenames[submesh_idx])
            {
                uv1_obj = json_object_z(arena);
                json_object_set_z(uv1_obj, "blob", json_string_z(arena, uv1_blob_filenames[submesh_idx]));
                json_object_set_z(uv1_obj, "format", json_string_z(arena, "float32"));
                json_object_set_z(uv1_obj, "components", json_integer_z(arena, 2));
                json_object_set_z(uv1_obj, "num", json_integer_z(arena, submesh->vertex_count));
                json_object_set_z(attributes, "uv1", uv1_obj);
            }

            if (submesh->uv2 && uv2_blob_filenames && uv2_blob_filenames[submesh_idx])
            {
                uv2_obj = json_object_z(arena);
                json_object_set_z(uv2_obj, "blob", json_string_z(arena, uv2_blob_filenames[submesh_idx]));
                json_object_set_z(uv2_obj, "format", json_string_z(arena, "float32"));
                json_object_set_z(uv2_obj, "components", json_integer_z(arena, 2));
                json_object_set_z(uv2_obj, "num", json_integer_z(arena, submesh->vertex_count));
                json_object_set_z(attributes, "uv2", uv2_obj);
            }

            if (submesh->joint_indices && joint_indices_blob_filenames && joint_indices_blob_filenames[submesh_idx])
            {
                joint_indices_obj = json_object_z(arena);
                json_object_set_z(joint_indices_obj, "blob", json_string_z(arena, joint_indices_blob_filenames[submesh_idx]));
                json_object_set_z(joint_indices_obj, "format", json_string_z(arena, "uint8"));
                json_object_set_z(joint_indices_obj, "components", json_integer_z(arena, 4));
                json_object_set_z(joint_indices_obj, "num", json_integer_z(arena, submesh->vertex_count));
                json_object_set_z(attributes, "joint_indices", joint_indices_obj);
            }

            if (submesh->joint_weights && joint_weights_blob_filenames && joint_weights_blob_filenames[submesh_idx])
            {
                joint_weights_obj = json_object_z(arena);
                json_object_set_z(joint_weights_obj, "blob", json_string_z(arena, joint_weights_blob_filenames[submesh_idx]));
                json_object_set_z(joint_weights_obj, "format", json_string_z(arena, "float32"));
                json_object_set_z(joint_weights_obj, "components", json_integer_z(arena, 4));
                json_object_set_z(joint_weights_obj, "num", json_integer_z(arena, submesh->vertex_count));
                json_object_set_z(attributes, "joint_weights", joint_weights_obj);
            }

            json_object_set_z(submesh_obj, "attributes", attributes);
            json_array_append_z(submeshes, submesh_obj);
        }

        json_object_set_z(root, "submeshes", submeshes);
    }

    // =============================================================================================
    // =============================================================================================
    // Write JSON to file.
    // =============================================================================================
    // =============================================================================================
    char manifest_path[1024];
    char* json_string;
    {
        snprintf(manifest_path, sizeof(manifest_path), "%s/assets/meshes/%s.json", output_dir, mesh->mesh_name);
        json_string = json_dumps_z(arena, root);
        fs_write_text_file_z(manifest_path, json_string);
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// write_image_manifest: Write JSON manifest for an image.
// =========================================================================================================================================
// =========================================================================================================================================
static void write_image_manifest(arena_zh arena, const BBArchiveImage* image, const char* output_dir, const char* blob_filename)
{
    fatal_check_z(arena, "arena is null");
    fatal_check_z(image, "image is null");
    fatal_check_z(output_dir, "output_dir is null");

    // =============================================================================================
    // =============================================================================================
    // Build JSON object using zp_c/json.
    // =============================================================================================
    // =============================================================================================
    json_zh root;
    json_zh attributes;
    json_zh blob_obj;
    {
        root       = json_object_z(arena);
        attributes = json_object_z(arena);
        blob_obj   = json_object_z(arena);

        // Set root object properties
        json_object_set_z(root, "image_name", json_string_z(arena, image->image_name));
        json_object_set_z(root, "width", json_integer_z(arena, image->width));
        json_object_set_z(root, "height", json_integer_z(arena, image->height));
        json_object_set_z(root, "num_channels", json_integer_z(arena, image->num_channels));
        json_object_set_z(root, "bits_per_channel", json_integer_z(arena, image->bits_per_channel));
        json_object_set_z(root, "is_srgb", json_boolean_z(arena, image->is_srgb));
        json_object_set_z(root, "mip_levels", json_integer_z(arena, image->mip_levels));

        // Set blob attribute
        json_object_set_z(blob_obj, "blob", json_string_z(arena, blob_filename));
        json_object_set_z(blob_obj, "format", json_string_z(arena, "uint8"));
        json_object_set_z(blob_obj, "components", json_integer_z(arena, image->num_channels));
        json_object_set_z(blob_obj, "size", json_integer_z(arena, image->blob_size));
        json_object_set_z(attributes, "blob", blob_obj);
        json_object_set_z(root, "attributes", attributes);
    }

    // =============================================================================================
    // =============================================================================================
    // Write JSON to file.
    // =============================================================================================
    // =============================================================================================
    char manifest_path[1024];
    char* json_string;
    {
        snprintf(manifest_path, sizeof(manifest_path), "%s/assets/images/%s.json", output_dir, image->image_name);
        json_string = json_dumps_z(arena, root);
        fs_write_text_file_z(manifest_path, json_string);
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// process_single_skeleton: Process a single skeleton by writing JSON data.
// =========================================================================================================================================
// =========================================================================================================================================
static void process_single_skeleton(arena_zh arena, const BBArchiveSkel* skeleton, const char* output_dir)
{
    fatal_check_z(arena, "arena is null");
    fatal_check_z(skeleton, "skeleton is null");
    fatal_check_z(output_dir, "output_dir is null");

    // =============================================================================================
    // =============================================================================================
    // Build JSON object using zp_c/json.
    // =============================================================================================
    // =============================================================================================
    json_zh root;
    json_zh bones;
    {
        root  = json_object_z(arena);
        bones = json_array_z(arena);

        // Add skeleton metadata
        json_object_set_z(root, "name", json_string_z(arena, skeleton->skeleton_name));
        json_object_set_z(root, "bone_count", json_integer_z(arena, skeleton->bone_count));

        // Add bones array
        for (uint32_t bone_idx = 0; bone_idx < skeleton->bone_count; bone_idx++)
        {
            json_zh bone = json_object_z(arena);

            // Add bone name
            json_object_set_z(bone, "name", json_string_z(arena, skeleton->bone_names[bone_idx]));

            // Add parent index
            int32_t parent_idx = skeleton->parent_bone_indices[bone_idx];
            json_object_set_z(bone, "parent_index", json_integer_z(arena, parent_idx));

            // Add bind_local matrix (4x4 matrix as array of 16 floats)
            json_zh bind_local_matrix    = json_array_z(arena);
            const float* bind_local_data = &skeleton->bind_local_matrices[bone_idx * 16];
            for (int i = 0; i < 16; i++)
            {
                json_array_append_z(bind_local_matrix, json_real_z(arena, bind_local_data[i]));
            }
            json_object_set_z(bone, "bind_local_matrix", bind_local_matrix);

            // Add inverse_bind matrix (4x4 matrix as array of 16 floats)
            json_zh inverse_bind_matrix    = json_array_z(arena);
            const float* inverse_bind_data = &skeleton->inverse_bind_matrices[bone_idx * 16];
            for (int i = 0; i < 16; i++)
            {
                json_array_append_z(inverse_bind_matrix, json_real_z(arena, inverse_bind_data[i]));
            }
            json_object_set_z(bone, "inverse_bind_matrix", inverse_bind_matrix);

            json_array_append_z(bones, bone);
        }

        json_object_set_z(root, "bones", bones);
    }

    // =============================================================================================
    // =============================================================================================
    // Write JSON to file.
    // =============================================================================================
    // =============================================================================================
    char skeleton_path[1024];
    char* json_string;
    {
        snprintf(skeleton_path, sizeof(skeleton_path), "%s/assets/skeletons/%s.json", output_dir, skeleton->skeleton_name);
        json_string = json_dumps_z(arena, root);
        fs_write_text_file_z(skeleton_path, json_string);
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// process_single_animation: Process a single animation by writing JSON data.
// =========================================================================================================================================
// =========================================================================================================================================
static void process_single_animation(arena_zh arena, const BBArchiveAnim* animation, const char* output_dir)
{
    fatal_check_z(arena, "arena is null");
    fatal_check_z(animation, "animation is null");
    fatal_check_z(output_dir, "output_dir is null");

    // =============================================================================================
    // =============================================================================================
    // Build JSON object using zp_c/json.
    // =============================================================================================
    // =============================================================================================
    json_zh root;
    json_zh channels;
    {
        root     = json_object_z(arena);
        channels = json_array_z(arena);

        json_object_set_z(root, "name", json_string_z(arena, animation->animation_name));
        json_object_set_z(root, "channel_count", json_integer_z(arena, animation->channel_count));

        // Calculate total keyframes for all channels
        uint32_t keyframe_offset = 0;
        for (uint32_t channel_idx = 0; channel_idx < animation->channel_count; channel_idx++)
        {
            json_zh channel = json_object_z(arena);

            json_object_set_z(channel, "bone_idx", json_integer_z(arena, animation->bone_indices[channel_idx]));
            json_object_set_z(channel, "type", json_integer_z(arena, animation->channel_types[channel_idx]));
            json_object_set_z(channel, "interpolation", json_integer_z(arena, animation->interpolation_types[channel_idx]));

            // Add keyframes for this channel
            json_zh keyframes       = json_array_z(arena);
            uint32_t keyframe_count = animation->keyframe_counts[channel_idx];

            for (uint32_t kf_idx = 0; kf_idx < keyframe_count; kf_idx++)
            {
                json_zh keyframe = json_object_z(arena);

                // Add time
                float time       = animation->keyframe_times[keyframe_offset + kf_idx];
                json_object_set_z(keyframe, "time", json_real_z(arena, time));

                // Add value (4-component vector)
                json_zh value           = json_array_z(arena);
                const float* value_data = &animation->keyframe_values[(keyframe_offset + kf_idx) * 4];
                for (int i = 0; i < 4; i++)
                {
                    json_array_append_z(value, json_real_z(arena, value_data[i]));
                }
                json_object_set_z(keyframe, "value", value);

                json_array_append_z(keyframes, keyframe);
            }

            json_object_set_z(channel, "keyframes", keyframes);
            json_array_append_z(channels, channel);

            keyframe_offset += keyframe_count;
        }

        json_object_set_z(root, "channels", channels);
    }

    // =============================================================================================
    // =============================================================================================
    // Write JSON to file.
    // =============================================================================================
    // =============================================================================================
    char animation_path[1024];
    char* json_string;
    {
        snprintf(animation_path, sizeof(animation_path), "%s/assets/animations/%s.json", output_dir, animation->animation_name);
        json_string = json_dumps_z(arena, root);
        fs_write_text_file_z(animation_path, json_string);
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// shader_field_to_json: Convert a shader field to JSON object.
// =========================================================================================================================================
// =========================================================================================================================================
static json_zh shader_field_to_json(arena_zh arena, const BBArchiveShaderField* field)
{
    json_zh field_obj = json_object_z(arena);

    json_object_set_z(field_obj, "field_type", json_integer_z(arena, field->field_type));
    json_object_set_z(field_obj, "value_type", json_integer_z(arena, field->value_type));

    switch (field->value_type)
    {
        case BB_SHADER_VALUE_TYPE_BOOL:   json_object_set_z(field_obj, "value", json_boolean_z(arena, field->bool_val != 0)); break;
        case BB_SHADER_VALUE_TYPE_INT:    json_object_set_z(field_obj, "value", json_integer_z(arena, field->int_val)); break;
        case BB_SHADER_VALUE_TYPE_FLOAT:  json_object_set_z(field_obj, "value", json_real_z(arena, field->float_val)); break;
        case BB_SHADER_VALUE_TYPE_UV_MAP:
        case BB_SHADER_VALUE_TYPE_TEX:    json_object_set_z(field_obj, "value", json_string_z(arena, field->str_val ? field->str_val : "")); break;
        case BB_SHADER_VALUE_TYPE_FLOAT2:
        {
            json_zh arr = json_array_z(arena);
            json_array_append_z(arr, json_real_z(arena, field->vec2_val[0]));
            json_array_append_z(arr, json_real_z(arena, field->vec2_val[1]));
            json_object_set_z(field_obj, "value", arr);
            break;
        }
        case BB_SHADER_VALUE_TYPE_FLOAT3:
        {
            json_zh arr = json_array_z(arena);
            json_array_append_z(arr, json_real_z(arena, field->vec3_val[0]));
            json_array_append_z(arr, json_real_z(arena, field->vec3_val[1]));
            json_array_append_z(arr, json_real_z(arena, field->vec3_val[2]));
            json_object_set_z(field_obj, "value", arr);
            break;
        }
        case BB_SHADER_VALUE_TYPE_FLOAT4:
        case BB_SHADER_VALUE_TYPE_COL:
        {
            json_zh arr = json_array_z(arena);
            json_array_append_z(arr, json_real_z(arena, field->vec4_val[0]));
            json_array_append_z(arr, json_real_z(arena, field->vec4_val[1]));
            json_array_append_z(arr, json_real_z(arena, field->vec4_val[2]));
            json_array_append_z(arr, json_real_z(arena, field->vec4_val[3]));
            json_object_set_z(field_obj, "value", arr);
            break;
        }
        default: break;
    }

    return field_obj;
}

// =========================================================================================================================================
// =========================================================================================================================================
// process_single_shader_graph: Process a single shader graph by writing JSON data.
// =========================================================================================================================================
// =========================================================================================================================================
static void process_single_shader_graph(arena_zh arena, const BBArchiveShaderGraph* shader_graph, const char* output_dir)
{
    fatal_check_z(arena, "arena is null");
    fatal_check_z(shader_graph, "shader_graph is null");
    fatal_check_z(output_dir, "output_dir is null");
    fatal_check_z(shader_graph->shader_name, "shader_graph->shader_name is null");

    // =============================================================================================
    // =============================================================================================
    // Build JSON object for shader graph.
    // =============================================================================================
    // =============================================================================================
    json_zh root;
    json_zh nodes_arr;
    json_zh links_arr;
    json_zh mat_data_arr;
    {
        root         = json_object_z(arena);
        nodes_arr    = json_array_z(arena);
        links_arr    = json_array_z(arena);
        mat_data_arr = json_array_z(arena);

        json_object_set_z(root, "shader_name", json_string_z(arena, shader_graph->shader_name));

        // Build nodes array
        for (uint32_t i = 0; i < shader_graph->node_count; i++)
        {
            const BBArchiveShaderNode* node = &shader_graph->nodes[i];
            json_zh node_obj                = json_object_z(arena);
            json_zh fields_arr              = json_array_z(arena);

            json_object_set_z(node_obj, "node_type", json_integer_z(arena, node->node_type));

            for (uint32_t j = 0; j < node->field_count; j++)
            {
                json_array_append_z(fields_arr, shader_field_to_json(arena, &node->fields[j]));
            }

            json_object_set_z(node_obj, "fields", fields_arr);
            json_array_append_z(nodes_arr, node_obj);
        }

        json_object_set_z(root, "nodes", nodes_arr);

        // Build links array
        for (uint32_t i = 0; i < shader_graph->link_count; i++)
        {
            const BBArchiveShaderLink* link = &shader_graph->links[i];
            json_zh link_obj                = json_object_z(arena);

            json_object_set_z(link_obj, "src_idx", json_integer_z(arena, link->src_idx));
            json_object_set_z(link_obj, "dst_idx", json_integer_z(arena, link->dst_idx));
            json_object_set_z(link_obj, "from_field", json_integer_z(arena, link->from_field));
            json_object_set_z(link_obj, "to_field", json_integer_z(arena, link->to_field));

            json_array_append_z(links_arr, link_obj);
        }

        json_object_set_z(root, "links", links_arr);

        // Build mat_data array
        for (uint32_t i = 0; i < shader_graph->mat_data_count; i++)
        {
            const BBArchiveShaderMatData* mat_data = &shader_graph->mat_data[i];
            json_zh mat_data_obj                   = json_object_z(arena);

            json_object_set_z(mat_data_obj, "node_idx", json_integer_z(arena, mat_data->node_idx));
            json_object_set_z(mat_data_obj, "field", shader_field_to_json(arena, &mat_data->field));

            json_array_append_z(mat_data_arr, mat_data_obj);
        }

        json_object_set_z(root, "mat_data", mat_data_arr);
    }

    // =============================================================================================
    // =============================================================================================
    // Write JSON to file.
    // =============================================================================================
    // =============================================================================================
    char shader_path[1024];
    char* json_string;
    {
        snprintf(shader_path, sizeof(shader_path), "%s/assets/shaders/%s.json", output_dir, shader_graph->shader_name);
        json_string = json_dumps_z(arena, root);
        fs_write_text_file_z(shader_path, json_string);
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// process_single_scene: Process a single scene by writing JSON data.
// =========================================================================================================================================
// =========================================================================================================================================
static void process_single_scene(arena_zh arena, const BBArchiveScene* scene, const char* output_dir)
{
    fatal_check_z(arena, "arena is null");
    fatal_check_z(scene, "scene is null");
    fatal_check_z(output_dir, "output_dir is null");

    // =============================================================================================
    // =============================================================================================
    // Build JSON object using zp_c/json.
    // =============================================================================================
    // =============================================================================================
    json_zh root;
    json_zh elems_arr;
    {
        root      = json_object_z(arena);
        elems_arr = json_array_z(arena);

        json_object_set_z(root, "name", json_string_z(arena, scene->scene_name));

        for (uint32_t i = 0; i < scene->elem_count; i++)
        {
            const BBArchiveSceneElem* elem = &scene->elems[i];
            json_zh elem_obj               = json_object_z(arena);

            json_object_set_z(elem_obj, "mesh", json_string_z(arena, elem->mesh_name));

            json_zh pos_arr = json_array_z(arena);
            json_array_append_z(pos_arr, json_real_z(arena, elem->pos[0]));
            json_array_append_z(pos_arr, json_real_z(arena, elem->pos[1]));
            json_array_append_z(pos_arr, json_real_z(arena, elem->pos[2]));
            json_object_set_z(elem_obj, "pos", pos_arr);

            json_zh rot_arr = json_array_z(arena);
            json_array_append_z(rot_arr, json_real_z(arena, elem->rot[0]));
            json_array_append_z(rot_arr, json_real_z(arena, elem->rot[1]));
            json_array_append_z(rot_arr, json_real_z(arena, elem->rot[2]));
            json_object_set_z(elem_obj, "rot", rot_arr);

            json_zh sca_arr = json_array_z(arena);
            json_array_append_z(sca_arr, json_real_z(arena, elem->sca[0]));
            json_array_append_z(sca_arr, json_real_z(arena, elem->sca[1]));
            json_array_append_z(sca_arr, json_real_z(arena, elem->sca[2]));
            json_object_set_z(elem_obj, "sca", sca_arr);

            json_zh mats_arr = json_array_z(arena);
            for (uint32_t j = 0; j < elem->mat_count; j++)
            {
                json_array_append_z(mats_arr, json_string_z(arena, elem->mat_names[j]));
            }
            json_object_set_z(elem_obj, "mats", mats_arr);

            json_array_append_z(elems_arr, elem_obj);
        }

        json_object_set_z(root, "elems", elems_arr);
    }

    // =============================================================================================
    // =============================================================================================
    // Write JSON to file.
    // =============================================================================================
    // =============================================================================================
    char scene_path[1024];
    char* json_string;
    {
        snprintf(scene_path, sizeof(scene_path), "%s/assets/scenes/%s.json", output_dir, scene->scene_name);
        json_string = json_dumps_z(arena, root);
        fs_write_text_file_z(scene_path, json_string);
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// process_single_mesh: Process a single mesh by writing binary blobs and generating manifest.
// =========================================================================================================================================
// =========================================================================================================================================
static void process_single_mesh(arena_zh arena, const BBArchiveMesh* mesh, const char* data_dir, const char* output_dir)
{
    fatal_check_z(arena, "arena is null");
    fatal_check_z(mesh, "mesh is null");
    fatal_check_z(data_dir, "data_dir is null");
    fatal_check_z(output_dir, "output_dir is null");
    fatal_check_bool_z(mesh->submesh_count > 0, "mesh has no submeshes");
    fatal_check_z(mesh->submeshes, "mesh submeshes is null");

    // Allocate arrays for blob filenames (one per submesh)
    const char** pos_blob_filenames            = (const char**)arena_alloc_z(arena, mesh->submesh_count * sizeof(const char*))->data;
    const char** uv0_blob_filenames            = (const char**)arena_alloc_z(arena, mesh->submesh_count * sizeof(const char*))->data;
    const char** uv1_blob_filenames            = (const char**)arena_alloc_z(arena, mesh->submesh_count * sizeof(const char*))->data;
    const char** uv2_blob_filenames            = (const char**)arena_alloc_z(arena, mesh->submesh_count * sizeof(const char*))->data;
    const char** joint_indices_blob_filenames  = (const char**)arena_alloc_z(arena, mesh->submesh_count * sizeof(const char*))->data;
    const char** joint_weights_blob_filenames  = (const char**)arena_alloc_z(arena, mesh->submesh_count * sizeof(const char*))->data;

    // Allocate arrays for actual filename strings
    char** pos_blob_filename_strings           = (char**)arena_alloc_z(arena, mesh->submesh_count * sizeof(char*))->data;
    char** uv0_blob_filename_strings           = (char**)arena_alloc_z(arena, mesh->submesh_count * sizeof(char*))->data;
    char** uv1_blob_filename_strings           = (char**)arena_alloc_z(arena, mesh->submesh_count * sizeof(char*))->data;
    char** uv2_blob_filename_strings           = (char**)arena_alloc_z(arena, mesh->submesh_count * sizeof(char*))->data;
    char** joint_indices_blob_filename_strings = (char**)arena_alloc_z(arena, mesh->submesh_count * sizeof(char*))->data;
    char** joint_weights_blob_filename_strings = (char**)arena_alloc_z(arena, mesh->submesh_count * sizeof(char*))->data;

    // Process each submesh
    for (uint32_t submesh_idx = 0; submesh_idx < mesh->submesh_count; submesh_idx++)
    {
        const BBArchiveSubmesh* submesh                  = &mesh->submeshes[submesh_idx];

        // Allocate filename strings
        pos_blob_filename_strings[submesh_idx]           = (char*)arena_alloc_z(arena, 256)->data;
        uv0_blob_filename_strings[submesh_idx]           = (char*)arena_alloc_z(arena, 256)->data;
        uv1_blob_filename_strings[submesh_idx]           = (char*)arena_alloc_z(arena, 256)->data;
        uv2_blob_filename_strings[submesh_idx]           = (char*)arena_alloc_z(arena, 256)->data;
        joint_indices_blob_filename_strings[submesh_idx] = (char*)arena_alloc_z(arena, 256)->data;
        joint_weights_blob_filename_strings[submesh_idx] = (char*)arena_alloc_z(arena, 256)->data;

        // Write positions blob
        fs_write_binary_blob_z(submesh->positions, sizeof(float), submesh->vertex_count * 3, data_dir, "pos_", pos_blob_filename_strings[submesh_idx], 256);
        pos_blob_filenames[submesh_idx] = pos_blob_filename_strings[submesh_idx];

        // Write UV channel blobs
        if (submesh->uv0)
        {
            fs_write_binary_blob_z(submesh->uv0, sizeof(float), submesh->vertex_count * 2, data_dir, "uv0_", uv0_blob_filename_strings[submesh_idx], 256);
            uv0_blob_filenames[submesh_idx] = uv0_blob_filename_strings[submesh_idx];
        }

        if (submesh->uv1)
        {
            fs_write_binary_blob_z(submesh->uv1, sizeof(float), submesh->vertex_count * 2, data_dir, "uv1_", uv1_blob_filename_strings[submesh_idx], 256);
            uv1_blob_filenames[submesh_idx] = uv1_blob_filename_strings[submesh_idx];
        }

        if (submesh->uv2)
        {
            fs_write_binary_blob_z(submesh->uv2, sizeof(float), submesh->vertex_count * 2, data_dir, "uv2_", uv2_blob_filename_strings[submesh_idx], 256);
            uv2_blob_filenames[submesh_idx] = uv2_blob_filename_strings[submesh_idx];
        }

        if (submesh->joint_indices)
        {
            fs_write_binary_blob_z(submesh->joint_indices, sizeof(uint8_t), submesh->vertex_count * 4, data_dir, "jidx_", joint_indices_blob_filename_strings[submesh_idx], 256);
            joint_indices_blob_filenames[submesh_idx] = joint_indices_blob_filename_strings[submesh_idx];
        }

        if (submesh->joint_weights)
        {
            fs_write_binary_blob_z(submesh->joint_weights, sizeof(float), submesh->vertex_count * 4, data_dir, "jwt_", joint_weights_blob_filename_strings[submesh_idx], 256);
            joint_weights_blob_filenames[submesh_idx] = joint_weights_blob_filename_strings[submesh_idx];
        }
    }

    // Write manifest
    write_mesh_manifest(arena, mesh, output_dir, pos_blob_filenames, uv0_blob_filenames, uv1_blob_filenames, uv2_blob_filenames, joint_indices_blob_filenames, joint_weights_blob_filenames);
}

// =========================================================================================================================================
// =========================================================================================================================================
// process_single_image: Process a single image by writing binary blob and generating manifest.
// =========================================================================================================================================
// =========================================================================================================================================
static void process_single_image(arena_zh arena, const BBArchiveImage* image, const char* data_dir, const char* output_dir)
{
    fatal_check_z(arena, "arena is null");
    fatal_check_z(image, "image is null");
    fatal_check_z(data_dir, "data_dir is null");
    fatal_check_z(output_dir, "output_dir is null");

    char blob_filename[256];
    fs_write_binary_blob_z(image->blob, 1, image->blob_size, data_dir, "img_", blob_filename, sizeof(blob_filename));

    // Write manifest
    write_image_manifest(arena, image, output_dir, blob_filename);
}

// =========================================================================================================================================
// =========================================================================================================================================
// validate_directory_structure: Check that archive directory has required structure.
// =========================================================================================================================================
// =========================================================================================================================================
static void validate_directory_structure(const char* archive_dir)
{
    fatal_check_z(archive_dir, "archive_dir is null");

    char data_dir[1024];
    char assets_dir[1024];

    snprintf(data_dir, sizeof(data_dir), "%s/data", archive_dir);
    snprintf(assets_dir, sizeof(assets_dir), "%s/assets", archive_dir);

    fatal_check_bool_z(fs_is_valid_dir_z(data_dir), "data directory does not exist or is not a directory");
    fatal_check_bool_z(fs_is_valid_dir_z(assets_dir), "assets directory does not exist or is not a directory");
}

// =========================================================================================================================================
// =========================================================================================================================================
// scan_asset_directory: Scan a directory for .json files and extract names (without .json extension).
// =========================================================================================================================================
// =========================================================================================================================================
static void scan_asset_directory(arena_zh arena, const char* dir_path, char*** names_out, uint32_t* count_out)
{
    if (!fs_is_valid_dir_z(dir_path))
    {
        *names_out = nullptr;
        *count_out = 0;
        return;
    }

    char** file_paths   = nullptr;
    uint32_t file_count = 0;

    fs_collect_files_by_extension_z(arena, dir_path, ".json", &file_paths, &file_count);

    if (file_count == 0)
    {
        *names_out = nullptr;
        *count_out = 0;
        return;
    }

    char** names = (char**)arena_alloc_z(arena, file_count * sizeof(char*))->data;
    for (uint32_t i = 0; i < file_count; i++)
    {
        names[i] = fs_get_basename_z(arena, file_paths[i], ".json");
    }

    *names_out = names;
    *count_out = file_count;
}

// =========================================================================================================================================
// =========================================================================================================================================
// scan_asset_directories: Scan asset directories and extract mesh, image, skeleton, animation, shader graph, and scene names.
// =========================================================================================================================================
// =========================================================================================================================================
static void scan_asset_directories(
    arena_zh arena,
    const char* archive_dir,
    char*** mesh_names_out,
    uint32_t* mesh_count_out,
    char*** image_names_out,
    uint32_t* image_count_out,
    char*** skeleton_names_out,
    uint32_t* skeleton_count_out,
    char*** animation_names_out,
    uint32_t* animation_count_out,
    char*** shader_graph_names_out,
    uint32_t* shader_graph_count_out,
    char*** scene_names_out,
    uint32_t* scene_count_out
)
{
    // Initialize output pointers to null
    *mesh_names_out         = nullptr;
    *image_names_out        = nullptr;
    *skeleton_names_out     = nullptr;
    *animation_names_out    = nullptr;
    *shader_graph_names_out = nullptr;
    *scene_names_out        = nullptr;
    *mesh_count_out         = 0;
    *image_count_out        = 0;
    *skeleton_count_out     = 0;
    *animation_count_out    = 0;
    *shader_graph_count_out = 0;
    *scene_count_out        = 0;

    char meshes_dir[1024];
    char images_dir[1024];
    char skeletons_dir[1024];
    char animations_dir[1024];
    char shaders_dir[1024];
    char scenes_dir[1024];

    snprintf(meshes_dir, sizeof(meshes_dir), "%s/assets/meshes", archive_dir);
    snprintf(images_dir, sizeof(images_dir), "%s/assets/images", archive_dir);
    snprintf(skeletons_dir, sizeof(skeletons_dir), "%s/assets/skeletons", archive_dir);
    snprintf(animations_dir, sizeof(animations_dir), "%s/assets/animations", archive_dir);
    snprintf(shaders_dir, sizeof(shaders_dir), "%s/assets/shaders", archive_dir);
    snprintf(scenes_dir, sizeof(scenes_dir), "%s/assets/scenes", archive_dir);

    scan_asset_directory(arena, meshes_dir, mesh_names_out, mesh_count_out);
    scan_asset_directory(arena, images_dir, image_names_out, image_count_out);
    scan_asset_directory(arena, skeletons_dir, skeleton_names_out, skeleton_count_out);
    scan_asset_directory(arena, animations_dir, animation_names_out, animation_count_out);
    scan_asset_directory(arena, shaders_dir, shader_graph_names_out, shader_graph_count_out);
    scan_asset_directory(arena, scenes_dir, scene_names_out, scene_count_out);
}

// =========================================================================================================================================
// =========================================================================================================================================
// parse_mesh_manifest: Parse mesh manifest with submeshes array and validate blob references.
// =========================================================================================================================================
// =========================================================================================================================================
static void parse_mesh_manifest(arena_zh arena, const char* archive_dir, const char* mesh_name, BBArchiveMesh* mesh_out)
{
    fatal_check_z(arena, "arena is null");
    fatal_check_z(archive_dir, "archive_dir is null");
    fatal_check_z(mesh_name, "mesh_name is null");
    fatal_check_z(mesh_out, "mesh_out is null");

    char manifest_path[1024];
    char data_dir[1024];
    snprintf(manifest_path, sizeof(manifest_path), "%s/assets/meshes/%s.json", archive_dir, mesh_name);
    snprintf(data_dir, sizeof(data_dir), "%s/data", archive_dir);

    json_zh root = json_load_file_z(arena, manifest_path);
    fatal_check_z(root, "failed to load mesh manifest");

    json_zh submeshes_json      = json_object_get_array_z(root, "submeshes");
    size_t submesh_count        = json_array_size_z(submeshes_json);

    // Allocate mutable struct, then assign to output (casting away const for arena allocation)
    BBArchiveMesh* mesh         = (BBArchiveMesh*)arena_alloc_z(arena, sizeof(BBArchiveMesh))->data;
    mesh->mesh_name             = arena_strdup_z(arena, json_object_get_string_z(root, "mesh_name"));
    mesh->submesh_count         = (uint32_t)submesh_count;

    BBArchiveSubmesh* submeshes = (BBArchiveSubmesh*)arena_alloc_z(arena, submesh_count * sizeof(BBArchiveSubmesh))->data;
    memset(submeshes, 0, submesh_count * sizeof(BBArchiveSubmesh));
    mesh->submeshes = submeshes;

    // Parse each submesh
    for (size_t i = 0; i < submesh_count; i++)
    {
        BBArchiveSubmesh* submesh = &submeshes[i];
        json_zh submesh_json      = json_array_get_object_z(submeshes_json, i);
        json_zh attributes        = json_object_get_object_z(submesh_json, "attributes");

        submesh->vertex_count     = json_object_get_integer_z(submesh_json, "vertex_count");
        submesh->uv0              = nullptr;
        submesh->uv1              = nullptr;
        submesh->uv2              = nullptr;
        submesh->joint_indices    = nullptr;
        submesh->joint_weights    = nullptr;

        // Parse positions (required)
        json_zh positions         = json_object_get_object_z(attributes, "positions");

        char pos_filepath[1024];
        snprintf(pos_filepath, sizeof(pos_filepath), "%s/%s", data_dir, json_object_get_string_z(positions, "blob"));
        span_zt pos_span;
        fs_read_file_to_arena_z(arena, pos_filepath, &pos_span);
        submesh->positions = (const float*)pos_span.data;

        // Parse optional UV channels
        if (json_object_has_object_z(attributes, "uv0"))
        {
            json_zh uv0 = json_object_get_object_z(attributes, "uv0");
            char uv0_filepath[1024];
            snprintf(uv0_filepath, sizeof(uv0_filepath), "%s/%s", data_dir, json_object_get_string_z(uv0, "blob"));
            span_zt uv0_span;
            fs_read_file_to_arena_z(arena, uv0_filepath, &uv0_span);
            submesh->uv0 = (const float*)uv0_span.data;
        }

        if (json_object_has_object_z(attributes, "uv1"))
        {
            json_zh uv1 = json_object_get_object_z(attributes, "uv1");
            char uv1_filepath[1024];
            snprintf(uv1_filepath, sizeof(uv1_filepath), "%s/%s", data_dir, json_object_get_string_z(uv1, "blob"));
            span_zt uv1_span;
            fs_read_file_to_arena_z(arena, uv1_filepath, &uv1_span);
            submesh->uv1 = (const float*)uv1_span.data;
        }

        if (json_object_has_object_z(attributes, "uv2"))
        {
            json_zh uv2 = json_object_get_object_z(attributes, "uv2");
            char uv2_filepath[1024];
            snprintf(uv2_filepath, sizeof(uv2_filepath), "%s/%s", data_dir, json_object_get_string_z(uv2, "blob"));
            span_zt uv2_span;
            fs_read_file_to_arena_z(arena, uv2_filepath, &uv2_span);
            submesh->uv2 = (const float*)uv2_span.data;
        }

        // Parse optional joint data
        if (json_object_has_object_z(attributes, "joint_indices"))
        {
            json_zh joint_indices = json_object_get_object_z(attributes, "joint_indices");
            char joint_indices_filepath[1024];
            snprintf(joint_indices_filepath, sizeof(joint_indices_filepath), "%s/%s", data_dir, json_object_get_string_z(joint_indices, "blob"));
            span_zt joint_indices_span;
            fs_read_file_to_arena_z(arena, joint_indices_filepath, &joint_indices_span);
            submesh->joint_indices = (const uint8_t*)joint_indices_span.data;
        }

        if (json_object_has_object_z(attributes, "joint_weights"))
        {
            json_zh joint_weights = json_object_get_object_z(attributes, "joint_weights");
            char joint_weights_filepath[1024];
            snprintf(joint_weights_filepath, sizeof(joint_weights_filepath), "%s/%s", data_dir, json_object_get_string_z(joint_weights, "blob"));
            span_zt joint_weights_span;
            fs_read_file_to_arena_z(arena, joint_weights_filepath, &joint_weights_span);
            submesh->joint_weights = (const float*)joint_weights_span.data;
        }
    }

    *mesh_out = *mesh;
}

// =========================================================================================================================================
// =========================================================================================================================================
// parse_image_manifest: Parse image manifest and validate blob references.
// =========================================================================================================================================
// =========================================================================================================================================
static void parse_image_manifest(arena_zh arena, const char* archive_dir, const char* image_name, BBArchiveImage* image_out)
{
    fatal_check_z(arena, "arena is null");
    fatal_check_z(archive_dir, "archive_dir is null");
    fatal_check_z(image_name, "image_name is null");
    fatal_check_z(image_out, "image_out is null");

    char manifest_path[1024];
    char data_dir[1024];
    snprintf(manifest_path, sizeof(manifest_path), "%s/assets/images/%s.json", archive_dir, image_name);
    snprintf(data_dir, sizeof(data_dir), "%s/data", archive_dir);

    json_zh root = json_load_file_z(arena, manifest_path);
    fatal_check_z(root, "failed to load image manifest");

    json_zh attributes      = json_object_get_object_z(root, "attributes");

    BBArchiveImage* image   = (BBArchiveImage*)arena_alloc_z(arena, sizeof(BBArchiveImage))->data;
    image->image_name       = arena_strdup_z(arena, json_object_get_string_z(root, "image_name"));
    image->width            = json_object_get_integer_z(root, "width");
    image->height           = json_object_get_integer_z(root, "height");
    image->num_channels     = json_object_get_integer_z(root, "num_channels");
    image->bits_per_channel = json_object_get_integer_z(root, "bits_per_channel");
    image->is_srgb          = json_object_get_boolean_z(root, "is_srgb");
    image->mip_levels       = json_object_get_integer_z(root, "mip_levels");

    json_zh blob_obj        = json_object_get_object_z(attributes, "blob");

    char blob_filepath[1024];
    snprintf(blob_filepath, sizeof(blob_filepath), "%s/%s", data_dir, json_object_get_string_z(blob_obj, "blob"));

    size_t expected_size = json_object_get_integer_z(blob_obj, "size");
    span_zt blob_span;
    fs_read_file_to_arena_z(arena, blob_filepath, &blob_span);
    image->blob      = (const uint8_t*)blob_span.data;
    image->blob_size = (uint64_t)blob_span.size;

    fatal_check_bool_z(image->blob_size == expected_size, "blob size mismatch");

    *image_out = *image;
}

// =========================================================================================================================================
// =========================================================================================================================================
// bb_archive_write: Process all meshes and images and write to directory-based output format.
// =========================================================================================================================================
// =========================================================================================================================================
void bb_archive_write(const BBArchiveInfo* info, const char* output_dir)
{
    // =============================================================================================
    // =============================================================================================
    // Guard: Validate inputs and create output directories.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(info, "info is null");
        fatal_check_z(output_dir, "output_dir is null");

        if (info->mesh_count > 0)
        {
            fatal_check_z(info->meshes, "info->meshes is null when mesh_count > 0");
        }

        if (info->image_count > 0)
        {
            fatal_check_z(info->images, "info->images is null when image_count > 0");
        }

        if (info->skeleton_count > 0)
        {
            fatal_check_z(info->skeletons, "info->skeletons is null when skeleton_count > 0");
        }

        if (info->animation_count > 0)
        {
            fatal_check_z(info->animations, "info->animations is null when animation_count > 0");
        }

        if (info->shader_graph_count > 0)
        {
            fatal_check_z(info->shader_graphs, "info->shader_graphs is null when shader_graph_count > 0");
        }

        if (info->scene_count > 0)
        {
            fatal_check_z(info->scenes, "info->scenes is null when scene_count > 0");
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Create output directory structure. This is additive: if directories already exist, they
    // will be preserved. Existing assets will be overwritten with new updates.
    // =============================================================================================
    // =============================================================================================
    char data_dir[1024];
    char assets_dir[1024];
    char meshes_dir[1024];
    char images_dir[1024];
    char skeletons_dir[1024];
    char animations_dir[1024];
    char shaders_dir[1024];
    char scenes_dir[1024];
    {
        snprintf(data_dir, sizeof(data_dir), "%s/data", output_dir);
        snprintf(assets_dir, sizeof(assets_dir), "%s/assets", output_dir);
        snprintf(meshes_dir, sizeof(meshes_dir), "%s/assets/meshes", output_dir);
        snprintf(images_dir, sizeof(images_dir), "%s/assets/images", output_dir);
        snprintf(skeletons_dir, sizeof(skeletons_dir), "%s/assets/skeletons", output_dir);
        snprintf(animations_dir, sizeof(animations_dir), "%s/assets/animations", output_dir);
        snprintf(shaders_dir, sizeof(shaders_dir), "%s/assets/shaders", output_dir);
        snprintf(scenes_dir, sizeof(scenes_dir), "%s/assets/scenes", output_dir);

        // Always create all directories to ensure complete archive structure, even if
        // the current info doesn't have all asset types. fs_mkdir_z handles existing
        // directories gracefully.
        fs_mkdir_z(output_dir, 0755);
        fs_mkdir_z(data_dir, 0755);
        fs_mkdir_z(assets_dir, 0755);
        fs_mkdir_z(meshes_dir, 0755);
        fs_mkdir_z(images_dir, 0755);
        fs_mkdir_z(skeletons_dir, 0755);
        fs_mkdir_z(animations_dir, 0755);
        fs_mkdir_z(shaders_dir, 0755);
        fs_mkdir_z(scenes_dir, 0755);
    }

    // =============================================================================================
    // =============================================================================================
    // Process all assets: write binary blobs and generate manifests.
    // =============================================================================================
    // =============================================================================================
    {
        arena_zh arena = arena_init_z(1024U * 1024U * 1024U);

        for (uint32_t mesh_idx = 0; mesh_idx < info->mesh_count; mesh_idx++)
        {
            arena_reset_z(arena);
            process_single_mesh(arena, &info->meshes[mesh_idx], data_dir, output_dir);
        }

        for (uint32_t image_idx = 0; image_idx < info->image_count; image_idx++)
        {
            arena_reset_z(arena);
            process_single_image(arena, &info->images[image_idx], data_dir, output_dir);
        }

        for (uint32_t skeleton_idx = 0; skeleton_idx < info->skeleton_count; skeleton_idx++)
        {
            arena_reset_z(arena);
            process_single_skeleton(arena, &info->skeletons[skeleton_idx], output_dir);
        }

        for (uint32_t animation_idx = 0; animation_idx < info->animation_count; animation_idx++)
        {
            arena_reset_z(arena);
            process_single_animation(arena, &info->animations[animation_idx], output_dir);
        }

        for (uint32_t shader_graph_idx = 0; shader_graph_idx < info->shader_graph_count; shader_graph_idx++)
        {
            arena_reset_z(arena);
            process_single_shader_graph(arena, &info->shader_graphs[shader_graph_idx], output_dir);
        }

        for (uint32_t scene_idx = 0; scene_idx < info->scene_count; scene_idx++)
        {
            arena_reset_z(arena);
            process_single_scene(arena, &info->scenes[scene_idx], output_dir);
        }

        arena_destroy_z(arena);
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// parse_skeleton_json: Parse skeleton JSON file and load skeleton data.
// =========================================================================================================================================
// =========================================================================================================================================
static void parse_skeleton_json(arena_zh arena, const char* archive_dir, const char* skeleton_name, BBArchiveSkel* skeleton_out)
{
    fatal_check_z(arena, "arena is null");
    fatal_check_z(archive_dir, "archive_dir is null");
    fatal_check_z(skeleton_name, "skeleton_name is null");
    fatal_check_z(skeleton_out, "skeleton_out is null");

    char skeleton_path[1024];
    snprintf(skeleton_path, sizeof(skeleton_path), "%s/assets/skeletons/%s.json", archive_dir, skeleton_name);

    json_zh root = json_load_file_z(arena, skeleton_path);
    fatal_check_z(root, "failed to load skeleton JSON");

    // Parse skeleton metadata
    json_zh bones_array             = json_object_get_array_z(root, "bones");
    uint32_t bone_count             = json_object_get_integer_z(root, "bone_count");

    // Allocate skeleton data
    BBArchiveSkel* skeleton         = (BBArchiveSkel*)arena_alloc_z(arena, sizeof(BBArchiveSkel))->data;
    skeleton->skeleton_name         = arena_strdup_z(arena, json_object_get_string_z(root, "name"));
    skeleton->bone_count            = bone_count;

    char** bone_names               = (char**)arena_alloc_z(arena, bone_count * sizeof(char*))->data;
    int32_t* parent_bone_indices    = (int32_t*)arena_alloc_z(arena, bone_count * sizeof(int32_t))->data;
    float* bind_local_matrices      = (float*)arena_alloc_z(arena, bone_count * 16 * sizeof(float))->data;
    float* inverse_bind_matrices    = (float*)arena_alloc_z(arena, bone_count * 16 * sizeof(float))->data;

    skeleton->bone_names            = (const char* const*)bone_names;
    skeleton->parent_bone_indices   = parent_bone_indices;
    skeleton->bind_local_matrices   = bind_local_matrices;
    skeleton->inverse_bind_matrices = inverse_bind_matrices;

    // Parse bones
    for (uint32_t i = 0; i < bone_count; i++)
    {
        json_zh bone              = json_array_get_object_z(bones_array, i);

        // Parse bone name
        bone_names[i]             = arena_strdup_z(arena, json_object_get_string_z(bone, "name"));

        // Parse parent index
        parent_bone_indices[i]    = json_object_get_integer_z(bone, "parent_index");

        // Parse bind_local_matrix
        json_zh bind_local_matrix = json_object_get_array_z(bone, "bind_local_matrix");
        for (int j = 0; j < 16; j++)
        {
            bind_local_matrices[i * 16 + j] = json_array_get_real_z(bind_local_matrix, j);
        }

        // Parse inverse_bind_matrix
        json_zh inverse_bind_matrix = json_object_get_array_z(bone, "inverse_bind_matrix");
        for (int j = 0; j < 16; j++)
        {
            inverse_bind_matrices[i * 16 + j] = json_array_get_real_z(inverse_bind_matrix, j);
        }
    }

    *skeleton_out = *skeleton;
}

// =========================================================================================================================================
// =========================================================================================================================================
// parse_animation_json: Parse animation JSON file and load animation data.
// =========================================================================================================================================
// =========================================================================================================================================
static void parse_animation_json(arena_zh arena, const char* archive_dir, const char* animation_name, BBArchiveAnim* animation_out)
{
    fatal_check_z(arena, "arena is null");
    fatal_check_z(archive_dir, "archive_dir is null");
    fatal_check_z(animation_name, "animation_name is null");
    fatal_check_z(animation_out, "animation_out is null");

    char animation_path[1024];
    snprintf(animation_path, sizeof(animation_path), "%s/assets/animations/%s.json", archive_dir, animation_name);

    json_zh root = json_load_file_z(arena, animation_path);
    fatal_check_z(root, "failed to load animation JSON");

    // Parse animation metadata
    json_zh channels_array         = json_object_get_array_z(root, "channels");
    uint32_t channel_count         = json_object_get_integer_z(root, "channel_count");

    // Allocate animation data
    BBArchiveAnim* animation       = (BBArchiveAnim*)arena_alloc_z(arena, sizeof(BBArchiveAnim))->data;
    animation->animation_name      = arena_strdup_z(arena, json_object_get_string_z(root, "name"));
    animation->channel_count       = channel_count;

    uint32_t* bone_indices         = (uint32_t*)arena_alloc_z(arena, channel_count * sizeof(uint32_t))->data;
    uint32_t* channel_types        = (uint32_t*)arena_alloc_z(arena, channel_count * sizeof(uint32_t))->data;
    uint32_t* interpolation_types  = (uint32_t*)arena_alloc_z(arena, channel_count * sizeof(uint32_t))->data;
    uint32_t* keyframe_counts      = (uint32_t*)arena_alloc_z(arena, channel_count * sizeof(uint32_t))->data;

    animation->bone_indices        = bone_indices;
    animation->channel_types       = channel_types;
    animation->interpolation_types = interpolation_types;
    animation->keyframe_counts     = keyframe_counts;

    // Calculate total keyframes and allocate arrays
    uint32_t total_keyframes       = 0;
    for (uint32_t i = 0; i < channel_count; i++)
    {
        json_zh channel          = json_array_get_object_z(channels_array, i);
        json_zh keyframes_array  = json_object_get_array_z(channel, "keyframes");
        keyframe_counts[i]       = json_array_size_z(keyframes_array);
        total_keyframes         += keyframe_counts[i];
    }

    float* keyframe_times      = (float*)arena_alloc_z(arena, total_keyframes * sizeof(float))->data;
    float* keyframe_values     = (float*)arena_alloc_z(arena, total_keyframes * 4 * sizeof(float))->data;
    animation->keyframe_times  = keyframe_times;
    animation->keyframe_values = keyframe_values;

    // Parse channels and keyframes
    uint32_t keyframe_offset   = 0;
    for (uint32_t i = 0; i < channel_count; i++)
    {
        json_zh channel         = json_array_get_object_z(channels_array, i);
        json_zh keyframes_array = json_object_get_array_z(channel, "keyframes");

        bone_indices[i]         = json_object_get_integer_z(channel, "bone_idx");
        channel_types[i]        = json_object_get_integer_z(channel, "type");
        interpolation_types[i]  = json_object_get_integer_z(channel, "interpolation");

        // Parse keyframes for this channel
        size_t keyframe_count   = json_array_size_z(keyframes_array);
        for (size_t j = 0; j < keyframe_count; j++)
        {
            json_zh keyframe                    = json_array_get_object_z(keyframes_array, j);
            json_zh value_array                 = json_object_get_array_z(keyframe, "value");

            keyframe_times[keyframe_offset + j] = json_object_get_real_z(keyframe, "time");

            for (int k = 0; k < 4; k++)
            {
                keyframe_values[(keyframe_offset + j) * 4 + k] = json_array_get_real_z(value_array, k);
            }
        }

        keyframe_offset += keyframe_count;
    }

    *animation_out = *animation;
}

// =========================================================================================================================================
// =========================================================================================================================================
// json_to_shader_field: Parse a JSON object into a shader field.
// =========================================================================================================================================
// =========================================================================================================================================
static void json_to_shader_field(arena_zh arena, json_zh field_json, BBArchiveShaderField* field)
{
    field->field_type = (bb_shader_input_field_type_e)json_object_get_integer_z(field_json, "field_type");
    field->value_type = (bb_shader_value_type_e)json_object_get_integer_z(field_json, "value_type");

    switch (field->value_type)
    {
        case BB_SHADER_VALUE_TYPE_BOOL:   field->bool_val = json_object_get_boolean_z(field_json, "value") ? 1 : 0; break;
        case BB_SHADER_VALUE_TYPE_INT:    field->int_val = json_object_get_integer_z(field_json, "value"); break;
        case BB_SHADER_VALUE_TYPE_FLOAT:  field->float_val = (float)json_object_get_real_z(field_json, "value"); break;
        case BB_SHADER_VALUE_TYPE_UV_MAP:
        case BB_SHADER_VALUE_TYPE_TEX:    field->str_val = arena_strdup_z(arena, json_object_get_string_z(field_json, "value")); break;
        case BB_SHADER_VALUE_TYPE_FLOAT2:
        {
            json_zh arr        = json_object_get_array_z(field_json, "value");
            field->vec2_val[0] = (float)json_array_get_real_z(arr, 0);
            field->vec2_val[1] = (float)json_array_get_real_z(arr, 1);
            break;
        }
        case BB_SHADER_VALUE_TYPE_FLOAT3:
        {
            json_zh arr        = json_object_get_array_z(field_json, "value");
            field->vec3_val[0] = (float)json_array_get_real_z(arr, 0);
            field->vec3_val[1] = (float)json_array_get_real_z(arr, 1);
            field->vec3_val[2] = (float)json_array_get_real_z(arr, 2);
            break;
        }
        case BB_SHADER_VALUE_TYPE_FLOAT4:
        case BB_SHADER_VALUE_TYPE_COL:
        {
            json_zh arr        = json_object_get_array_z(field_json, "value");
            field->vec4_val[0] = (float)json_array_get_real_z(arr, 0);
            field->vec4_val[1] = (float)json_array_get_real_z(arr, 1);
            field->vec4_val[2] = (float)json_array_get_real_z(arr, 2);
            field->vec4_val[3] = (float)json_array_get_real_z(arr, 3);
            break;
        }
        default: break;
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// parse_shader_graph_json: Parse shader graph JSON file and load shader graph data.
// =========================================================================================================================================
// =========================================================================================================================================
static void parse_shader_graph_json(arena_zh arena, const char* archive_dir, const char* shader_graph_name, BBArchiveShaderGraph* shader_graph_out)
{
    fatal_check_z(arena, "arena is null");
    fatal_check_z(archive_dir, "archive_dir is null");
    fatal_check_z(shader_graph_name, "shader_graph_name is null");
    fatal_check_z(shader_graph_out, "shader_graph_out is null");

    char shader_path[1024];
    snprintf(shader_path, sizeof(shader_path), "%s/assets/shaders/%s.json", archive_dir, shader_graph_name);

    json_zh root = json_load_file_z(arena, shader_path);
    fatal_check_z(root, "failed to load shader graph JSON");

    // =============================================================================================
    // =============================================================================================
    // Parse shader graph metadata and nodes.
    // =============================================================================================
    // =============================================================================================
    BBArchiveShaderGraph* shader_graph;
    {
        shader_graph               = (BBArchiveShaderGraph*)arena_alloc_z(arena, sizeof(BBArchiveShaderGraph))->data;
        shader_graph->shader_name  = arena_strdup_z(arena, json_object_get_string_z(root, "shader_name"));

        json_zh nodes_arr          = json_object_get_array_z(root, "nodes");
        shader_graph->node_count   = json_array_size_z(nodes_arr);

        BBArchiveShaderNode* nodes = (BBArchiveShaderNode*)arena_alloc_z(arena, shader_graph->node_count * sizeof(BBArchiveShaderNode))->data;
        shader_graph->nodes        = nodes;

        for (uint32_t i = 0; i < shader_graph->node_count; i++)
        {
            json_zh node_json            = json_array_get_object_z(nodes_arr, i);
            BBArchiveShaderNode* node    = &nodes[i];

            node->node_type              = (bb_shader_node_type_e)json_object_get_integer_z(node_json, "node_type");

            json_zh fields_arr           = json_object_get_array_z(node_json, "fields");
            node->field_count            = json_array_size_z(fields_arr);

            BBArchiveShaderField* fields = (BBArchiveShaderField*)arena_alloc_z(arena, node->field_count * sizeof(BBArchiveShaderField))->data;
            node->fields                 = fields;

            for (uint32_t j = 0; j < node->field_count; j++)
            {
                json_zh field_json = json_array_get_object_z(fields_arr, j);
                json_to_shader_field(arena, field_json, &fields[j]);
            }
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Parse links.
    // =============================================================================================
    // =============================================================================================
    {
        json_zh links_arr          = json_object_get_array_z(root, "links");
        shader_graph->link_count   = json_array_size_z(links_arr);

        BBArchiveShaderLink* links = (BBArchiveShaderLink*)arena_alloc_z(arena, shader_graph->link_count * sizeof(BBArchiveShaderLink))->data;
        shader_graph->links        = links;

        for (uint32_t i = 0; i < shader_graph->link_count; i++)
        {
            json_zh link_json         = json_array_get_object_z(links_arr, i);
            BBArchiveShaderLink* link = &links[i];

            link->src_idx             = json_object_get_integer_z(link_json, "src_idx");
            link->dst_idx             = json_object_get_integer_z(link_json, "dst_idx");
            link->from_field          = (bb_shader_output_field_type_e)json_object_get_integer_z(link_json, "from_field");
            link->to_field            = (bb_shader_input_field_type_e)json_object_get_integer_z(link_json, "to_field");
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Parse mat_data.
    // =============================================================================================
    // =============================================================================================
    {
        json_zh mat_data_arr             = json_object_get_array_z(root, "mat_data");
        shader_graph->mat_data_count     = json_array_size_z(mat_data_arr);

        BBArchiveShaderMatData* mat_data = (BBArchiveShaderMatData*)arena_alloc_z(arena, shader_graph->mat_data_count * sizeof(BBArchiveShaderMatData))->data;
        shader_graph->mat_data           = mat_data;

        for (uint32_t i = 0; i < shader_graph->mat_data_count; i++)
        {
            json_zh mat_data_json         = json_array_get_object_z(mat_data_arr, i);
            BBArchiveShaderMatData* entry = &mat_data[i];

            entry->node_idx               = json_object_get_integer_z(mat_data_json, "node_idx");

            json_zh field_json            = json_object_get_object_z(mat_data_json, "field");
            json_to_shader_field(arena, field_json, &entry->field);
        }
    }

    *shader_graph_out = *shader_graph;
}

// =========================================================================================================================================
// =========================================================================================================================================
// parse_scene_json: Parse scene JSON file and load scene data.
// =========================================================================================================================================
// =========================================================================================================================================
static void parse_scene_json(arena_zh arena, const char* archive_dir, const char* scene_name, BBArchiveScene* scene_out)
{
    fatal_check_z(arena, "arena is null");
    fatal_check_z(archive_dir, "archive_dir is null");
    fatal_check_z(scene_name, "scene_name is null");
    fatal_check_z(scene_out, "scene_out is null");

    char scene_path[1024];
    snprintf(scene_path, sizeof(scene_path), "%s/assets/scenes/%s.json", archive_dir, scene_name);

    json_zh root = json_load_file_z(arena, scene_path);
    fatal_check_z(root, "failed to load scene JSON");

    BBArchiveScene* scene = (BBArchiveScene*)arena_alloc_z(arena, sizeof(BBArchiveScene))->data;
    scene->scene_name     = arena_strdup_z(arena, json_object_get_string_z(root, "name"));

    json_zh elems_arr     = json_object_get_array_z(root, "elems");
    scene->elem_count     = json_array_size_z(elems_arr);

    if (scene->elem_count > 0)
    {
        BBArchiveSceneElem* elems = (BBArchiveSceneElem*)arena_alloc_z(arena, scene->elem_count * sizeof(BBArchiveSceneElem))->data;
        scene->elems              = elems;

        for (uint32_t i = 0; i < scene->elem_count; i++)
        {
            BBArchiveSceneElem* elem = &elems[i];
            json_zh elem_json        = json_array_get_object_z(elems_arr, i);

            elem->mesh_name          = arena_strdup_z(arena, json_object_get_string_z(elem_json, "mesh"));

            json_zh pos_arr          = json_object_get_array_z(elem_json, "pos");
            elem->pos[0]             = (float)json_array_get_real_z(pos_arr, 0);
            elem->pos[1]             = (float)json_array_get_real_z(pos_arr, 1);
            elem->pos[2]             = (float)json_array_get_real_z(pos_arr, 2);

            json_zh rot_arr          = json_object_get_array_z(elem_json, "rot");
            elem->rot[0]             = (float)json_array_get_real_z(rot_arr, 0);
            elem->rot[1]             = (float)json_array_get_real_z(rot_arr, 1);
            elem->rot[2]             = (float)json_array_get_real_z(rot_arr, 2);

            json_zh sca_arr          = json_object_get_array_z(elem_json, "sca");
            elem->sca[0]             = (float)json_array_get_real_z(sca_arr, 0);
            elem->sca[1]             = (float)json_array_get_real_z(sca_arr, 1);
            elem->sca[2]             = (float)json_array_get_real_z(sca_arr, 2);

            json_zh mats_arr         = json_object_get_array_z(elem_json, "mats");
            elem->mat_count          = json_array_size_z(mats_arr);

            if (elem->mat_count > 0)
            {
                char** mat_names = (char**)arena_alloc_z(arena, elem->mat_count * sizeof(char*))->data;
                for (uint32_t j = 0; j < elem->mat_count; j++)
                {
                    mat_names[j] = arena_strdup_z(arena, json_array_get_string_z(mats_arr, j));
                }
                elem->mat_names = (const char* const*)mat_names;
            }
            else
            {
                elem->mat_names = nullptr;
            }
        }
    }
    else
    {
        scene->elems = nullptr;
    }

    *scene_out = *scene;
}

// =========================================================================================================================================
// =========================================================================================================================================
// bb_archive_read: Validate and read an existing bb_archive directory structure.
// =========================================================================================================================================
// =========================================================================================================================================
void bb_archive_read(arena_zh arena, const char* archive_dir, BBArchiveInfo* info)
{
    // =============================================================================================
    // =============================================================================================
    // Guard: Validate input directory exists and has required structure.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(arena, "arena is null");
        fatal_check_z(archive_dir, "archive_dir is null");
        fatal_check_z(info, "info is null");

        validate_directory_structure(archive_dir);
    }

    // =============================================================================================
    // =============================================================================================
    // Parse catalogue.json to get mesh and image name lists.
    // =============================================================================================
    // =============================================================================================
    char** mesh_names;
    uint32_t mesh_count;
    char** image_names;
    uint32_t image_count;
    char** skeleton_names;
    uint32_t skeleton_count;
    char** animation_names;
    uint32_t animation_count;
    char** shader_graph_names;
    uint32_t shader_graph_count;
    char** scene_names;
    uint32_t scene_count;
    {
        scan_asset_directories(arena, archive_dir, &mesh_names, &mesh_count, &image_names, &image_count, &skeleton_names, &skeleton_count, &animation_names, &animation_count, &shader_graph_names, &shader_graph_count, &scene_names, &scene_count);
    }

    // =============================================================================================
    // =============================================================================================
    // Allocate BBArchiveInfo arrays based on counts from catalogue.
    // =============================================================================================
    // =============================================================================================
    BBArchiveMesh* meshes               = (BBArchiveMesh*)arena_alloc_z(arena, mesh_count * sizeof(BBArchiveMesh))->data;
    BBArchiveImage* images              = (BBArchiveImage*)arena_alloc_z(arena, image_count * sizeof(BBArchiveImage))->data;
    BBArchiveSkel* skeletons            = (BBArchiveSkel*)arena_alloc_z(arena, skeleton_count * sizeof(BBArchiveSkel))->data;
    BBArchiveAnim* animations           = (BBArchiveAnim*)arena_alloc_z(arena, animation_count * sizeof(BBArchiveAnim))->data;
    BBArchiveShaderGraph* shader_graphs = (BBArchiveShaderGraph*)arena_alloc_z(arena, shader_graph_count * sizeof(BBArchiveShaderGraph))->data;
    BBArchiveScene* scenes              = (BBArchiveScene*)arena_alloc_z(arena, scene_count * sizeof(BBArchiveScene))->data;

    // =============================================================================================
    // =============================================================================================
    // For each mesh: parse manifest, verify blob files exist, verify checksums, load blob data.
    // =============================================================================================
    // =============================================================================================
    {
        for (uint32_t i = 0; i < mesh_count; i++)
        {
            parse_mesh_manifest(arena, archive_dir, mesh_names[i], &meshes[i]);
        }
    }

    // =============================================================================================
    // =============================================================================================
    // For each image: parse manifest, verify blob file exists, verify checksum, load blob data.
    // =============================================================================================
    // =============================================================================================
    {
        for (uint32_t i = 0; i < image_count; i++)
        {
            parse_image_manifest(arena, archive_dir, image_names[i], &images[i]);
        }
    }

    // =============================================================================================
    // =============================================================================================
    // For each skeleton: parse JSON file and load skeleton data.
    // =============================================================================================
    // =============================================================================================
    {
        for (uint32_t i = 0; i < skeleton_count; i++)
        {
            parse_skeleton_json(arena, archive_dir, skeleton_names[i], &skeletons[i]);
        }
    }

    // =============================================================================================
    // =============================================================================================
    // For each animation: parse JSON file and load animation data.
    // =============================================================================================
    // =============================================================================================
    {
        for (uint32_t i = 0; i < animation_count; i++)
        {
            parse_animation_json(arena, archive_dir, animation_names[i], &animations[i]);
        }
    }

    // =============================================================================================
    // =============================================================================================
    // For each shader graph: parse JSON file and load shader graph data.
    // =============================================================================================
    // =============================================================================================
    {
        for (uint32_t i = 0; i < shader_graph_count; i++)
        {
            parse_shader_graph_json(arena, archive_dir, shader_graph_names[i], &shader_graphs[i]);
        }
    }

    // =============================================================================================
    // =============================================================================================
    // For each scene: parse JSON file and load scene data.
    // =============================================================================================
    // =============================================================================================
    {
        for (uint32_t i = 0; i < scene_count; i++)
        {
            parse_scene_json(arena, archive_dir, scene_names[i], &scenes[i]);
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Populate final info struct (name arrays are in arena, no cleanup needed).
    // =============================================================================================
    // =============================================================================================
    {
        info->meshes             = meshes;
        info->mesh_count         = mesh_count;
        info->images             = images;
        info->image_count        = image_count;
        info->skeletons          = skeletons;
        info->skeleton_count     = skeleton_count;
        info->animations         = animations;
        info->animation_count    = animation_count;
        info->shader_graphs      = shader_graphs;
        info->shader_graph_count = shader_graph_count;
        info->scenes             = scenes;
        info->scene_count        = scene_count;
    }
}
