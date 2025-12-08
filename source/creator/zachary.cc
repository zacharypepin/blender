#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "BLI_listbase.h"
#include "BLI_math_vector_types.hh"
#include "BLI_path_utils.hh"

#include "DNA_image_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_packedFile_types.h"

#include "BKE_attribute.hh"
#include "BKE_context.hh"
#include "BKE_image.hh"
#include "BKE_main.hh"
#include "BKE_mesh.hh"

#include "IMB_imbuf.hh"
#include "IMB_imbuf_enums.h"

#include "bb_archive/bb_archive.h"
#include "zp_c/arena.h"

#include "zachary.h"

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
        mesh_count = BLI_listbase_count(&bmain->meshes);
        printf("[zachary] Found %d meshes\n", mesh_count);

        meshes       = (BBArchiveMesh*)arena_alloc_z(arena, sizeof(BBArchiveMesh) * mesh_count)->data;

        int mesh_idx = 0;
        LISTBASE_FOREACH(Mesh*, mesh, &bmain->meshes)
        {
            if (mesh->verts_num == 0)
            {
                printf("[zachary] Skipping mesh %s (no vertices)\n", mesh->id.name);
                continue;
            }

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
            bb_mesh->mesh_name     = mesh->id.name;
            bb_mesh->submesh_count = submesh_count;
            bb_mesh->submeshes     = submeshes;

            mesh_idx++;
        }

        mesh_count = mesh_idx;
    }

    // ===============================================================================================
    // ===============================================================================================
    // Extract meshes and write to bb_archive
    // ===============================================================================================
    // ===============================================================================================
    {
        BBArchiveInfo info      = {0};
        info.mesh_count         = mesh_count;
        info.image_count        = 0;
        info.skeleton_count     = 0;
        info.animation_count    = 0;
        info.shader_graph_count = 0;
        info.scene_count        = 0;
        info.meshes             = meshes;
        info.images             = nullptr;
        info.skeletons          = nullptr;
        info.animations         = nullptr;
        info.shader_graphs      = nullptr;
        info.scenes             = nullptr;

        bb_archive_write(&info, bb_archive_output_dir);
        printf("[zachary] bb_archive_write completed\n");

        arena_destroy_z(arena);
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
    // write images to png
    // ===============================================================================================
    // ===============================================================================================
    {
        int image_count = 0;
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

                ibuf->ftype            = IMB_FTYPE_PNG;
                ibuf->foptions.quality = 100;

                char filepath[FILE_MAX];
                snprintf(filepath, sizeof(filepath), "./image_%d_%d.png", image_count, imapf->tile_number);

                BLI_path_abs_from_cwd(filepath, sizeof(filepath));

                if (IMB_save_image(ibuf, filepath, 0))
                {
                    printf("Saved packed image to %s\n", filepath);
                }

                IMB_freeImBuf(ibuf);
            }
            image_count++;
        }
    }
}
