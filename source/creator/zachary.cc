#include <cstdio>
#include <cstring>
#include <map>
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

#include "zachary.h"

// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
// =========================================================================================================================================
static const char* replace_me_prefix(arena_zh arena, const char* name)
{
    if (name != nullptr && strncmp(name, "ME", 2) == 0)
    {
        size_t name_len = strlen(name);
        char* new_name  = (char*)arena_alloc_z(arena, name_len + 4)->data; // "mesh_" is 5 chars, "ME" is 2, so +3, but +4 for safety
        snprintf(new_name, name_len + 4, "mesh_%s", name + 2);
        return new_name;
    }
    return name;
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
    printf("[zachary] zachary_main()\n");

    // ===============================================================================================
    // ===============================================================================================
    // ===============================================================================================
    // ===============================================================================================
    Main* bmain;
    {
        bmain = CTX_data_main(C);
        if (bmain == nullptr)
        {
            printf("No Main database available\n");
            return;
        }
    }

    // ===============================================================================================
    // ===============================================================================================
    // Validate .blend file
    // ===============================================================================================
    // ===============================================================================================
    std::vector<const char*> invalid_mesh_names;
    std::vector<Mesh*> valid_meshes;
    {
        printf("[zachary] Validating .blend file...\n");

        LISTBASE_FOREACH(Mesh*, mesh, &bmain->meshes)
        {
            bool is_invalid = false;
            if (mesh->totcol > 0 && mesh->mat != nullptr)
            {
                for (int i = 0; i < mesh->totcol; i++)
                {
                    if (mesh->mat[i] == nullptr)
                    {
                        printf("[zachary] VALIDATION ERROR: Mesh %s has empty material slot at index %d (total slots: %d)\n", mesh->id.name, i, mesh->totcol);
                        invalid_mesh_names.push_back(mesh->id.name);
                        is_invalid = true;
                        break;
                    }
                }
            }

            if (is_invalid)
            {
                continue;
            }

            if (mesh->verts_num == 0)
            {
                printf("[zachary] Skipping mesh %s (no vertices)\n", mesh->id.name);
                invalid_mesh_names.push_back(mesh->id.name);
                continue;
            }

            valid_meshes.push_back(mesh);
        }
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
        mesh_count = valid_meshes.size();
        printf("[zachary] Processing %d valid meshes\n", mesh_count);

        meshes       = (BBArchiveMesh*)arena_alloc_z(arena, sizeof(BBArchiveMesh) * mesh_count)->data;

        int mesh_idx = 0;
        for (Mesh* mesh : valid_meshes)
        {
            printf("[zachary] Processing mesh: %s (verts: %d, corners: %d, faces: %d)\n", mesh->id.name, mesh->verts_num, mesh->corners_num, mesh->faces_num);

            // Get mesh data
            blender::Span<blender::float3> positions                = mesh->vert_positions();
            blender::Span<int> corner_verts                         = mesh->corner_verts();
            blender::Span<blender::int3> corner_tris                = mesh->corner_tris();
            blender::Span<int> corner_tri_faces                     = mesh->corner_tri_faces();
            blender::bke::AttributeAccessor attributes              = mesh->attributes();
            blender::VectorSet<blender::StringRefNull> uv_map_names = mesh->uv_map_names();
            blender::Span<MDeformVert> deform_verts                 = mesh->deform_verts();
            bool has_joint_data                                     = !deform_verts.is_empty() && mesh->corners_num > 0;

            // Group tris by material index
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

            int submesh_count = triangles_by_material.size();
            printf("[zachary]   Found %d material(s), creating %d submesh(es)\n", submesh_count, submesh_count);
            printf("[zachary]   Total triangles: %d\n", (int)corner_tris.size());

            // Cache UV maps
            blender::VArraySpan<blender::float2> uv_maps[3];
            for (int uv_idx = 0; uv_idx < uv_map_names.size() && uv_idx < 3; uv_idx++)
            {
                uv_maps[uv_idx] = *attributes.lookup<blender::float2>(uv_map_names[uv_idx], blender::bke::AttrDomain::Corner);
                if (!uv_maps[uv_idx].is_empty())
                {
                    printf("[zachary]   Loaded UV map %d: %s\n", uv_idx, uv_map_names[uv_idx].c_str());
                }
            }

            // Allocate submeshes array
            BBArchiveSubmesh* submeshes = (BBArchiveSubmesh*)arena_alloc_z(arena, sizeof(BBArchiveSubmesh) * submesh_count)->data;

            int submesh_idx             = 0;
            for (const auto& [mat_idx, triangle_indices] : triangles_by_material)
            {
                int total_vertices = triangle_indices.size() * 3;

                printf("[zachary]   Material %d: %d triangles, %d vertices\n", mat_idx, (int)triangle_indices.size(), total_vertices);

                // Allocate position data
                float* positions_data = (float*)arena_alloc_z(arena, sizeof(float) * 3 * total_vertices)->data;

                // Allocate uv data
                float* uv_data[3]     = {nullptr, nullptr, nullptr};
                for (int uv_idx = 0; uv_idx < 3; uv_idx++)
                {
                    if (!uv_maps[uv_idx].is_empty())
                    {
                        uv_data[uv_idx] = (float*)arena_alloc_z(arena, sizeof(float) * 2 * total_vertices)->data;
                    }
                }

                // Allocate joint data
                uint8_t* joint_indices_data = nullptr;
                float* joint_weights_data   = nullptr;
                if (has_joint_data)
                {
                    joint_indices_data = (uint8_t*)arena_alloc_z(arena, sizeof(uint8_t) * 4 * total_vertices)->data;
                    joint_weights_data = (float*)arena_alloc_z(arena, sizeof(float) * 4 * total_vertices)->data;
                    memset(joint_indices_data, 0, sizeof(uint8_t) * 4 * total_vertices);
                    memset(joint_weights_data, 0, sizeof(float) * 4 * total_vertices);
                }

                // Fill data arrays
                int vertex_offset = 0;
                for (int tri_idx : triangle_indices)
                {
                    blender::int3 tri = corner_tris[tri_idx];
                    for (int i = 0; i < 3; i++)
                    {
                        int corner                            = tri[i];
                        int vert_idx                          = corner_verts[corner];

                        // Position
                        positions_data[vertex_offset * 3 + 0] = positions[vert_idx].x;
                        positions_data[vertex_offset * 3 + 1] = positions[vert_idx].y;
                        positions_data[vertex_offset * 3 + 2] = positions[vert_idx].z;

                        // UV maps
                        for (int uv_idx = 0; uv_idx < 3; uv_idx++)
                        {
                            if (uv_data[uv_idx] != nullptr && corner < uv_maps[uv_idx].size())
                            {
                                uv_data[uv_idx][vertex_offset * 2 + 0] = uv_maps[uv_idx][corner].x;
                                uv_data[uv_idx][vertex_offset * 2 + 1] = uv_maps[uv_idx][corner].y;
                            }
                        }

                        // Joint data
                        if (has_joint_data && vert_idx < deform_verts.size())
                        {
                            const MDeformVert& dvert = deform_verts[vert_idx];
                            int joint_count          = 0;
                            for (int j = 0; j < dvert.totweight && joint_count < 4; j++)
                            {
                                if (dvert.dw[j].weight > 0.0f)
                                {
                                    joint_indices_data[vertex_offset * 4 + joint_count] = (uint8_t)dvert.dw[j].def_nr;
                                    joint_weights_data[vertex_offset * 4 + joint_count] = dvert.dw[j].weight;
                                    joint_count++;
                                }
                            }
                        }

                        vertex_offset++;
                    }
                }

                // Create submesh
                BBArchiveSubmesh& submesh = submeshes[submesh_idx];
                submesh.vertex_count      = total_vertices;
                submesh.positions         = positions_data;
                submesh.uv0               = uv_data[0];
                submesh.uv1               = uv_data[1];
                submesh.uv2               = uv_data[2];
                submesh.joint_indices     = joint_indices_data;
                submesh.joint_weights     = joint_weights_data;

                submesh_idx++;
            }

            // Create mesh structure
            BBArchiveMesh* bb_mesh = &meshes[mesh_idx];
            bb_mesh->mesh_name     = replace_me_prefix(arena, mesh->id.name);
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
    BBArchiveScene* scenes = nullptr;
    int scene_count        = 0;
    {
        // Count scenes and mesh objects per scene
        int total_scenes = BLI_listbase_count(&bmain->scenes);
        printf("[zachary] Found %d scenes\n", total_scenes);

        if (total_scenes > 0)
        {
            // First pass: count mesh objects per scene
            std::vector<int> mesh_object_counts(total_scenes, 0);
            int scene_idx = 0;
            LISTBASE_FOREACH(Scene*, scene, &bmain->scenes)
            {
                FOREACH_SCENE_OBJECT_BEGIN(scene, ob)
                {
                    if (ob->type == OB_MESH && ob->data != nullptr)
                    {
                        mesh_object_counts[scene_idx]++;
                    }
                }
                FOREACH_SCENE_OBJECT_END;
                scene_idx++;
            }

            // Count total scenes with mesh objects
            int scenes_with_objects = 0;
            for (int count : mesh_object_counts)
            {
                if (count > 0)
                {
                    scenes_with_objects++;
                }
            }

            printf("[zachary] Found %d scenes with mesh objects\n", scenes_with_objects);

            if (scenes_with_objects > 0)
            {
                // Allocate scenes array
                scenes               = (BBArchiveScene*)arena_alloc_z(arena, sizeof(BBArchiveScene) * scenes_with_objects)->data;

                scene_idx            = 0;
                int output_scene_idx = 0;
                LISTBASE_FOREACH(Scene*, scene, &bmain->scenes)
                {
                    int mesh_object_count = mesh_object_counts[scene_idx];
                    if (mesh_object_count == 0)
                    {
                        scene_idx++;
                        continue;
                    }

                    printf("[zachary] Processing scene: %s (%d mesh objects)\n", scene->id.name, mesh_object_count);

                    // Allocate scene elements
                    BBArchiveSceneElem* elems = (BBArchiveSceneElem*)arena_alloc_z(arena, sizeof(BBArchiveSceneElem) * mesh_object_count)->data;

                    int elem_idx              = 0;
                    FOREACH_SCENE_OBJECT_BEGIN(scene, ob)
                    {
                        if (ob->type != OB_MESH || ob->data == nullptr)
                        {
                            continue;
                        }

                        Mesh* mesh = (Mesh*)ob->data;
                        printf("[zachary]   Processing object: %s (mesh: %s)\n", ob->id.name, mesh->id.name);

                        // Extract transform
                        float pos[3]  = {ob->loc[0], ob->loc[1], ob->loc[2]};
                        float rot[3]  = {ob->rot[0], ob->rot[1], ob->rot[2]};
                        float sca[3]  = {ob->scale[0], ob->scale[1], ob->scale[2]};

                        // Extract material names
                        int mat_count = 0;
                        for (int i = 0; i < ob->totcol; i++)
                        {
                            Material* mat = BKE_object_material_get(ob, i + 1);
                            if (mat != nullptr)
                            {
                                mat_count++;
                            }
                        }

                        const char** mat_names = nullptr;
                        if (mat_count > 0)
                        {
                            mat_names   = (const char**)arena_alloc_z(arena, sizeof(const char*) * mat_count)->data;
                            int mat_idx = 0;
                            for (int i = 0; i < ob->totcol; i++)
                            {
                                Material* mat = BKE_object_material_get(ob, i + 1);
                                if (mat != nullptr)
                                {
                                    mat_names[mat_idx] = mat->id.name;
                                    mat_idx++;
                                }
                            }
                        }

                        // Create scene element
                        BBArchiveSceneElem& elem = elems[elem_idx];
                        elem.mesh_name           = replace_me_prefix(arena, mesh->id.name);
                        elem.pos[0]              = pos[0];
                        elem.pos[1]              = pos[1];
                        elem.pos[2]              = pos[2];
                        elem.rot[0]              = rot[0];
                        elem.rot[1]              = rot[1];
                        elem.rot[2]              = rot[2];
                        elem.sca[0]              = sca[0];
                        elem.sca[1]              = sca[1];
                        elem.sca[2]              = sca[2];
                        elem.mat_count           = mat_count;
                        elem.mat_names           = mat_names;

                        elem_idx++;
                    }
                    FOREACH_SCENE_OBJECT_END;

                    // Create scene
                    scenes[output_scene_idx].scene_name = scene->id.name;
                    scenes[output_scene_idx].elem_count = mesh_object_count;
                    scenes[output_scene_idx].elems      = elems;

                    printf("[zachary] Created scene: %s with %u elements\n", scenes[output_scene_idx].scene_name, scenes[output_scene_idx].elem_count);

                    output_scene_idx++;
                    scene_idx++;
                }

                scene_count = scenes_with_objects;
            }
        }
    }

    // ===============================================================================================
    // ===============================================================================================
    // Extract images for bb_archive
    // ===============================================================================================
    // ===============================================================================================
    BBArchiveImage* images = nullptr;
    int image_count        = 0;
    {
        int total_image_count = 0;
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
                total_image_count++;
            }
        }

        printf("[zachary] Found %d packed images\n", total_image_count);

        if (total_image_count > 0)
        {
            images        = (BBArchiveImage*)arena_alloc_z(arena, sizeof(BBArchiveImage) * total_image_count)->data;

            int image_idx = 0;
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

                    int flag    = IB_byte_data | IB_metadata;
                    ImBuf* ibuf = IMB_load_image_from_memory((const unsigned char*)imapf->packedfile->data, imapf->packedfile->size, flag, "<packed data>", nullptr, ima->colorspace_settings.name);

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

                    char image_name[256];
                    snprintf(image_name, sizeof(image_name), "%s_%d", ima->id.name, imapf->tile_number);

                    // Create BBArchiveImage
                    BBArchiveImage& bb_image  = images[image_idx];
                    bb_image.image_name       = arena_strdup_z(arena, image_name);
                    bb_image.width            = ibuf->x;
                    bb_image.height           = ibuf->y;
                    bb_image.num_channels     = 4;
                    bb_image.bits_per_channel = 8;
                    bb_image.is_srgb          = IMB_colormanagement_space_is_srgb(ibuf->byte_buffer.colorspace);
                    bb_image.mip_levels       = 1;
                    bb_image.blob             = blob_data;
                    bb_image.blob_size        = blob_size;

                    printf("[zachary] Extracted rgba8 image: %s (%ux%u, %zu bytes)\n", bb_image.image_name, bb_image.width, bb_image.height, blob_size);

                    IMB_freeImBuf(ibuf);
                    image_idx++;
                }
            }

            image_count = image_idx;
        }
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
        info.images             = images;
        info.skeletons          = nullptr;
        info.animations         = nullptr;
        info.shader_graphs      = nullptr;
        info.scenes             = scenes;

        bb_archive_write(&info, bb_archive_output_dir);
        printf("[zachary] bb_archive_write completed\n");
    }

    // ===============================================================================================
    // ===============================================================================================
    // print meshes
    // ===============================================================================================
    // ===============================================================================================
    {
        FILE* fp = fopen("./dump.txt", "w");
        if (fp == nullptr)
        {
            printf("Failed to open dump.txt for writing\n");
            return;
        }

        fprintf(fp, "Meshes in file:\n");
        LISTBASE_FOREACH(Mesh*, mesh, &bmain->meshes)
        {
            fprintf(fp, "  - %s (verts: %d, edges: %d, faces: %d)\n", mesh->id.name, mesh->verts_num, mesh->edges_num, mesh->faces_num);
        }
        fprintf(fp, "Total meshes: %d\n", BLI_listbase_count(&bmain->meshes));

        fclose(fp);
    }

    // ===============================================================================================
    // ===============================================================================================
    // Print invalid meshes
    // ===============================================================================================
    // ===============================================================================================
    {
        if (!invalid_mesh_names.empty())
        {
            printf("[zachary] Invalid meshes skipped (empty material slots or no vertices):\n");
            for (const char* mesh_name : invalid_mesh_names)
            {
                printf("[zachary]   - %s\n", replace_me_prefix(arena, mesh_name));
            }
            printf("[zachary] Total invalid meshes: %d\n", (int)invalid_mesh_names.size());
        }
    }

    arena_destroy_z(arena);
}
