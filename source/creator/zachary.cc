#include "zachary.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_set>
#include <vector>

#include "BLI_listbase.h"
#include "BLI_math_vector_types.hh"

#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_packedFile_types.h"
#include "DNA_scene_types.h"

#include "BKE_attribute.hh"
#include "BKE_collection.hh"
#include "BKE_context.hh"
#include "BKE_image.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_mesh.hh"

#include "IMB_colormanagement.hh"
#include "IMB_imbuf.hh"

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
    struct vec2_t
    {
        float x;
        float y;
    };

    struct vec3_t
    {
        float x;
        float y;
        float z;
    };

    struct vertex_t
    {
        vec3_t position;
        vec2_t uv;
        std::array<uint8_t, 4> joint_indices;
        std::array<float, 4> joint_weights;
    };

    struct triangle_t
    {
        std::array<vertex_t, 3> vertices;
    };

    struct submesh_t
    {
        std::string material_name;
        std::vector<triangle_t> triangles;
    };

    struct mesh_t
    {
        std::string name;
        std::vector<submesh_t> submeshes;
    };

    struct scene_elem_t
    {
        vec3_t pos;
        vec3_t euler_rot;
        vec3_t scale;
        mesh_t mesh;
    };

    struct scene_t
    {
        std::string name;
        std::vector<scene_elem_t> elems;
    };

    struct image_t
    {
        std::string name;
        std::string colourspace_name;
        const void* packedfile_data;
        uint64_t packedfile_size;
    };

    struct data_t
    {
        std::vector<scene_t> scenes;
        std::vector<image_t> images;
        std::vector<std::string> invalid_mesh_names;
    };
}

// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
static std::string replace_prefix(const char* name, const char* old_prefix, const char* new_prefix)
{
    if (name == nullptr)
    {
        return std::string();
    }

    std::string result    = name;

    // Replace prefix if it matches
    size_t old_prefix_len = strlen(old_prefix);
    if (result.compare(0, old_prefix_len, old_prefix) == 0)
    {
        result = std::string(new_prefix) + result.substr(old_prefix_len);
    }

    // Replace spaces with underscores
    for (char& c : result)
    {
        if (c == ' ')
        {
            c = '_';
        }
    }

    return result;
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
            fprintf(stderr, "[zachary] ERROR: No Main database available\n");
            return;
        }
    }

    // ===============================================================================================
    // ===============================================================================================
    // ===============================================================================================
    // ===============================================================================================
    FILE* log_file;
    {
        std::string log_filename;
        if (bmain->filepath[0] != '\0')
        {
            std::string filepath = bmain->filepath;
            size_t last_slash    = filepath.find_last_of("/\\");
            std::string basename = (last_slash != std::string::npos) ? filepath.substr(last_slash + 1) : filepath;
            size_t dot_pos       = basename.rfind(".blend");
            if (dot_pos != std::string::npos)
            {
                basename = basename.substr(0, dot_pos);
            }
            log_filename = basename + "_log.txt";
        }
        else
        {
            log_filename = "zachary_log.txt";
        }

        if ((log_file = fopen(log_filename.c_str(), "w")) == nullptr)
        {
            fprintf(stderr, "[zachary] Failed to open log file: %s\n", log_filename.c_str());
            return;
        }
    }

    // ===============================================================================================
    // ===============================================================================================
    // ===============================================================================================
    // ===============================================================================================
    fprintf(log_file, "[zachary] zachary_main()\n");

    // ===============================================================================================
    // ===============================================================================================
    // populate data_t
    // ===============================================================================================
    // ===============================================================================================
    data_t data;
    {
        // scenes
        LISTBASE_FOREACH(Scene*, scene, &bmain->scenes)
        {
            scene_t scene_data;
            scene_data.name = replace_prefix(scene->id.name, "SC", "scene_");

            FOREACH_SCENE_OBJECT_BEGIN(scene, ob)
            {
                if (ob->type != OB_MESH || ob->data == nullptr) continue;

                // Extract mesh geometry data
                Mesh* mesh                                              = (Mesh*)ob->data;
                blender::Span<blender::float3> positions                = mesh->vert_positions();
                blender::Span<int> corner_verts                         = mesh->corner_verts();
                blender::Span<blender::int3> corner_tris                = mesh->corner_tris();
                blender::Span<int> corner_tri_faces                     = mesh->corner_tri_faces();
                blender::bke::AttributeAccessor attributes              = mesh->attributes();
                blender::VectorSet<blender::StringRefNull> uv_map_names = mesh->uv_map_names();
                blender::Span<MDeformVert> deform_verts                 = mesh->deform_verts();
                bool has_joint_data                                     = !deform_verts.is_empty() && mesh->corners_num > 0;

                // Cache UV map (just the first one for now)
                blender::VArraySpan<blender::float2> uv_map;
                if (!uv_map_names.is_empty())
                {
                    uv_map = *attributes.lookup<blender::float2>(uv_map_names[0], blender::bke::AttrDomain::Corner);
                }

                // Check for empty material slots
                {
                    bool has_empty_material_slot = false;
                    if (mesh->totcol > 0 && mesh->mat != nullptr)
                    {
                        for (int i = 0; i < mesh->totcol; i++)
                        {
                            if (mesh->mat[i] == nullptr)
                            {
                                has_empty_material_slot = true;
                                break;
                            }
                        }
                    }
                    if (has_empty_material_slot)
                    {
                        fprintf(log_file, "[zachary] Skipping mesh %s (has empty material slot)\n", mesh->id.name);
                        data.invalid_mesh_names.push_back(mesh->id.name);
                        continue;
                    }
                }

                scene_elem_t elem = {};
                elem.pos.x        = ob->loc[0];
                elem.pos.y        = ob->loc[1];
                elem.pos.z        = ob->loc[2];
                elem.euler_rot.x  = ob->rot[0];
                elem.euler_rot.y  = ob->rot[1];
                elem.euler_rot.z  = ob->rot[2];
                elem.scale.x      = ob->scale[0];
                elem.scale.y      = ob->scale[1];
                elem.scale.z      = ob->scale[2];

                elem.mesh.name    = replace_prefix(mesh->id.name, "ME", "mesh_");

                // Group triangles by material index
                std::map<int, std::vector<int>> triangles_by_material;
                {
                    const blender::VArray<int> material_indices_varray = *attributes.lookup_or_default<int>("material_index", blender::bke::AttrDomain::Face, 0);
                    blender::VArraySpan<int> material_indices(material_indices_varray);

                    for (int tri_idx = 0; tri_idx < corner_tris.size(); tri_idx++)
                    {
                        int face_idx = corner_tri_faces[tri_idx];
                        int mat_idx  = material_indices[face_idx];
                        triangles_by_material[mat_idx].push_back(tri_idx);
                    }
                }

                // Create submeshes from grouped triangles
                for (const auto& [mat_idx, triangle_indices] : triangles_by_material)
                {
                    submesh_t submesh;

                    Material* mat = BKE_object_material_get(ob, mat_idx + 1);
                    if (mat != nullptr)
                    {
                        submesh.material_name = replace_prefix(mat->id.name, "MA", "mat_shad_");
                    }

                    for (int tri_idx : triangle_indices)
                    {
                        blender::int3 tri = corner_tris[tri_idx];
                        triangle_t triangle;
                        for (int i = 0; i < 3; i++)
                        {
                            int corner                      = tri[i];
                            int vert_idx                    = corner_verts[corner];

                            // Position
                            triangle.vertices[i].position.x = positions[vert_idx].x;
                            triangle.vertices[i].position.y = positions[vert_idx].y;
                            triangle.vertices[i].position.z = positions[vert_idx].z;

                            // UV
                            if (!uv_map.is_empty() && corner < uv_map.size())
                            {
                                triangle.vertices[i].uv.x = uv_map[corner].x;
                                triangle.vertices[i].uv.y = uv_map[corner].y;
                            }

                            // Joint data
                            triangle.vertices[i].joint_indices.fill(0);
                            triangle.vertices[i].joint_weights.fill(0.0f);
                            if (has_joint_data && vert_idx < deform_verts.size())
                            {
                                const MDeformVert& dvert = deform_verts[vert_idx];
                                int joint_count          = 0;
                                for (int j = 0; j < dvert.totweight && joint_count < 4; j++)
                                {
                                    if (dvert.dw[j].weight > 0.0f)
                                    {
                                        triangle.vertices[i].joint_indices[joint_count] = (uint8_t)dvert.dw[j].def_nr;
                                        triangle.vertices[i].joint_weights[joint_count] = dvert.dw[j].weight;
                                        joint_count++;
                                    }
                                }
                            }
                        }
                        submesh.triangles.push_back(triangle);
                    }
                    elem.mesh.submeshes.push_back(submesh);
                }

                scene_data.elems.push_back(elem);
            }
            FOREACH_SCENE_OBJECT_END;

            data.scenes.push_back(scene_data);
        }

        // images
        LISTBASE_FOREACH(Image*, ima, &bmain->images)
        {
            if (!BKE_image_has_packedfile(ima))
            {
                continue;
            }

            LISTBASE_FOREACH(ImagePackedFile*, imapf, &ima->packedfiles)
            {
                if (imapf->packedfile == nullptr)
                {
                    continue;
                }

                image_t image          = {};
                image.name             = replace_prefix(ima->id.name, "IM", "img_");
                image.colourspace_name = ima->colorspace_settings.name;
                image.packedfile_data  = imapf->packedfile->data;
                image.packedfile_size  = imapf->packedfile->size;
                data.images.push_back(image);
            }
        }
    }

    // ===============================================================================================
    // ===============================================================================================
    // dump data_t to log file
    // ===============================================================================================
    // ===============================================================================================
    {
        fprintf(log_file, "[zachary] === DATA DUMP ===\n");
        fprintf(log_file, "[zachary] Scene count: %zu\n", data.scenes.size());

        for (size_t scene_idx = 0; scene_idx < data.scenes.size(); scene_idx++)
        {
            const scene_t& scene = data.scenes[scene_idx];
            fprintf(log_file, "[zachary]   Scene[%zu]: %s\n", scene_idx, scene.name.c_str());
            fprintf(log_file, "[zachary]     Element count: %zu\n", scene.elems.size());

            for (size_t elem_idx = 0; elem_idx < scene.elems.size(); elem_idx++)
            {
                const scene_elem_t& elem = scene.elems[elem_idx];
                fprintf(log_file, "[zachary]       Elem[%zu]:\n", elem_idx);
                fprintf(log_file, "[zachary]         pos: (%.4f, %.4f, %.4f)\n", elem.pos.x, elem.pos.y, elem.pos.z);
                fprintf(log_file, "[zachary]         rot: (%.4f, %.4f, %.4f)\n", elem.euler_rot.x, elem.euler_rot.y, elem.euler_rot.z);
                fprintf(log_file, "[zachary]         scale: (%.4f, %.4f, %.4f)\n", elem.scale.x, elem.scale.y, elem.scale.z);
                fprintf(log_file, "[zachary]         mesh: %s\n", elem.mesh.name.c_str());
                fprintf(log_file, "[zachary]           submesh count: %zu\n", elem.mesh.submeshes.size());

                for (size_t submesh_idx = 0; submesh_idx < elem.mesh.submeshes.size(); submesh_idx++)
                {
                    const submesh_t& submesh = elem.mesh.submeshes[submesh_idx];
                    fprintf(log_file, "[zachary]             Submesh[%zu]: material=%s, triangles=%zu\n", submesh_idx, submesh.material_name.c_str(), submesh.triangles.size());
                }
            }
        }

        fprintf(log_file, "[zachary] Image count: %zu\n", data.images.size());
        for (size_t image_idx = 0; image_idx < data.images.size(); image_idx++)
        {
            const image_t& image = data.images[image_idx];
            fprintf(log_file, "[zachary]   Image[%zu]: %s (colourspace=%s, size=%lu)\n", image_idx, image.name.c_str(), image.colourspace_name.c_str(), image.packedfile_size);
        }

        if (!data.invalid_mesh_names.empty())
        {
            fprintf(log_file, "[zachary]   Invalid mesh names: %zu\n", data.invalid_mesh_names.size());
            for (const std::string& name : data.invalid_mesh_names)
            {
                fprintf(log_file, "[zachary]     - %s\n", name.c_str());
            }
        }

        fprintf(log_file, "[zachary] === END DATA DUMP ===\n");
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
    BBArchiveMesh* meshes = nullptr;
    int mesh_count        = 0;
    {
        // Collect unique meshes by name from data_t
        std::map<std::string, const mesh_t*> unique_meshes;
        for (const scene_t& scene : data.scenes)
        {
            for (const scene_elem_t& elem : scene.elems)
            {
                if (unique_meshes.find(elem.mesh.name) == unique_meshes.end())
                {
                    unique_meshes[elem.mesh.name] = &elem.mesh;
                }
            }
        }

        mesh_count = unique_meshes.size();
        fprintf(log_file, "[zachary] Processing %d unique meshes from data_t\n", mesh_count);

        meshes       = (BBArchiveMesh*)arena_alloc_z(arena, sizeof(BBArchiveMesh) * mesh_count)->data;

        int mesh_idx = 0;
        for (const auto& [mesh_name, mesh_ptr] : unique_meshes)
        {
            const mesh_t& mesh = *mesh_ptr;
            fprintf(log_file, "[zachary] Processing mesh: %s (%zu submeshes)\n", mesh.name.c_str(), mesh.submeshes.size());

            int submesh_count           = mesh.submeshes.size();
            BBArchiveSubmesh* submeshes = (BBArchiveSubmesh*)arena_alloc_z(arena, sizeof(BBArchiveSubmesh) * submesh_count)->data;

            for (size_t submesh_idx = 0; submesh_idx < mesh.submeshes.size(); submesh_idx++)
            {
                const submesh_t& src_submesh = mesh.submeshes[submesh_idx];
                int total_vertices           = src_submesh.triangles.size() * 3;

                fprintf(log_file, "[zachary]   Submesh %zu: %zu triangles, %d vertices\n", submesh_idx, src_submesh.triangles.size(), total_vertices);

                // Allocate data arrays
                float* positions_data       = (float*)arena_alloc_z(arena, sizeof(float) * 3 * total_vertices)->data;
                float* uv_data              = (float*)arena_alloc_z(arena, sizeof(float) * 2 * total_vertices)->data;
                uint8_t* joint_indices_data = (uint8_t*)arena_alloc_z(arena, sizeof(uint8_t) * 4 * total_vertices)->data;
                float* joint_weights_data   = (float*)arena_alloc_z(arena, sizeof(float) * 4 * total_vertices)->data;

                // Fill data arrays from triangles
                int vertex_offset           = 0;
                for (const triangle_t& tri : src_submesh.triangles)
                {
                    for (int i = 0; i < 3; i++)
                    {
                        const vertex_t& vert                  = tri.vertices[i];

                        // Position
                        positions_data[vertex_offset * 3 + 0] = vert.position.x;
                        positions_data[vertex_offset * 3 + 1] = vert.position.y;
                        positions_data[vertex_offset * 3 + 2] = vert.position.z;

                        // UV
                        uv_data[vertex_offset * 2 + 0]        = vert.uv.x;
                        uv_data[vertex_offset * 2 + 1]        = vert.uv.y;

                        // Joint data
                        for (int j = 0; j < 4; j++)
                        {
                            joint_indices_data[vertex_offset * 4 + j] = vert.joint_indices[j];
                            joint_weights_data[vertex_offset * 4 + j] = vert.joint_weights[j];
                        }

                        vertex_offset++;
                    }
                }

                // Create submesh
                BBArchiveSubmesh& submesh = submeshes[submesh_idx];
                submesh.vertex_count      = total_vertices;
                submesh.positions         = positions_data;
                submesh.uv0               = uv_data;
                submesh.uv1               = nullptr;
                submesh.uv2               = nullptr;
                submesh.joint_indices     = joint_indices_data;
                submesh.joint_weights     = joint_weights_data;
            }

            // Create mesh structure
            BBArchiveMesh* bb_mesh = &meshes[mesh_idx];
            bb_mesh->mesh_name     = mesh.name.c_str();
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
    BBArchiveScene* bb_scenes;
    int scene_count;
    {
        fprintf(log_file, "[zachary] Found %zu scenes\n", data.scenes.size());

        bb_scenes = (BBArchiveScene*)arena_alloc_z(arena, sizeof(BBArchiveScene) * data.scenes.size())->data;

        for (size_t scene_idx = 0; scene_idx < data.scenes.size(); scene_idx++)
        {
            const scene_t& scene = data.scenes[scene_idx];

            fprintf(log_file, "[zachary] Processing scene: %s (%zu mesh objects)\n", scene.name.c_str(), scene.elems.size());

            BBArchiveSceneElem* elems = (BBArchiveSceneElem*)arena_alloc_z(arena, sizeof(BBArchiveSceneElem) * scene.elems.size())->data;

            for (size_t elem_idx = 0; elem_idx < scene.elems.size(); elem_idx++)
            {
                const scene_elem_t& src_elem = scene.elems[elem_idx];

                int mat_count                = src_elem.mesh.submeshes.size();
                const char** mat_names       = (const char**)arena_alloc_z(arena, sizeof(const char*) * mat_count)->data;
                for (size_t i = 0; i < src_elem.mesh.submeshes.size(); i++)
                {
                    mat_names[i] = src_elem.mesh.submeshes[i].material_name.c_str();
                }

                BBArchiveSceneElem& elem = elems[elem_idx];
                elem.mesh_name           = src_elem.mesh.name.c_str();
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
            }

            bb_scenes[scene_idx].scene_name = scene.name.c_str();
            bb_scenes[scene_idx].elem_count = scene.elems.size();
            bb_scenes[scene_idx].elems      = elems;
            fprintf(log_file, "[zachary] Created scene: %s with %u elements\n", bb_scenes[scene_idx].scene_name, bb_scenes[scene_idx].elem_count);
        }

        scene_count = data.scenes.size();
    }

    // ===============================================================================================
    // ===============================================================================================
    // Extract images for bb_archive
    // ===============================================================================================
    // ===============================================================================================
    BBArchiveImage* bb_images = nullptr;
    int image_count           = 0;
    {
        fprintf(log_file, "[zachary] Found %zu packed images\n", data.images.size());

        bb_images     = (BBArchiveImage*)arena_alloc_z(arena, sizeof(BBArchiveImage) * data.images.size())->data;

        int image_idx = 0;
        for (const image_t& image : data.images)
        {
            int flag    = IB_byte_data | IB_metadata;
            ImBuf* ibuf = IMB_load_image_from_memory((const unsigned char*)image.packedfile_data, image.packedfile_size, flag, "<packed data>", nullptr, (char*)image.colourspace_name.c_str());

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

            // Extract rgba8 data
            size_t pixel_count = IMB_get_pixel_count(ibuf);
            size_t blob_size   = pixel_count * 4;
            uint8_t* blob_data = (uint8_t*)arena_alloc_z(arena, blob_size)->data;
            memcpy(blob_data, ibuf->byte_buffer.data, blob_size);

            // Create BBArchiveImage
            BBArchiveImage& bb_image  = bb_images[image_idx];
            bb_image.image_name       = image.name.c_str();
            bb_image.width            = ibuf->x;
            bb_image.height           = ibuf->y;
            bb_image.num_channels     = 4;
            bb_image.bits_per_channel = 8;
            bb_image.is_srgb          = IMB_colormanagement_space_is_srgb(ibuf->byte_buffer.colorspace);
            bb_image.mip_levels       = 1;
            bb_image.blob             = blob_data;
            bb_image.blob_size        = blob_size;

            fprintf(log_file, "[zachary] Extracted rgba8 image: %s (%ux%u, %zu bytes)\n", bb_image.image_name, bb_image.width, bb_image.height, blob_size);

            IMB_freeImBuf(ibuf);
            image_idx++;
        }

        image_count = image_idx;
    }

    // ===============================================================================================
    // ===============================================================================================
    // Extract meshes and write to bb_archive
    // ===============================================================================================
    // ===============================================================================================
    {
        BBArchiveInfo info      = {0};
        info.mesh_count         = mesh_count;
        info.image_count        = image_count;
        info.skeleton_count     = 0;
        info.animation_count    = 0;
        info.shader_graph_count = 0;
        info.scene_count        = scene_count;
        info.meshes             = meshes;
        info.images             = bb_images;
        info.skeletons          = nullptr;
        info.animations         = nullptr;
        info.shader_graphs      = nullptr;
        info.scenes             = bb_scenes;

        bb_archive_write(&info, bb_archive_output_dir);
        fprintf(log_file, "[zachary] bb_archive_write completed\n");
    }

    fclose(log_file);
    arena_destroy_z(arena);
}
