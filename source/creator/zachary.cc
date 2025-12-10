#include "zachary.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include "BLI_listbase.h"
#include "BLI_math_vector_types.hh"

#include "DNA_armature_types.h"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_node_types.h"
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
        vec2_t uv0;
        vec2_t uv1;
        vec2_t uv2;
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
        std::string name;
        std::optional<std::string> parent_name;
        vec3_t pos;
        vec3_t euler_rot;
        vec3_t scale;
        std::optional<std::string> mesh_name;
        std::optional<std::string> skel_name;
    };

    struct scene_t
    {
        std::string name;
        std::map<std::string, scene_elem_t> elems;
    };

    struct image_t
    {
        std::string name;
        std::vector<uint8_t> rgba_data;
        uint32_t width;
        uint32_t height;
        bool is_srgb;
    };

    struct shader_node_socket_t
    {
        std::string identifier;
        std::string idname; // e.g. "NodeSocketColor", "NodeSocketFloat"
    };

    struct shader_node_t
    {
        std::string name;
        std::string idname; // e.g. "ShaderNodeTexImage", "ShaderNodeBsdfPrincipled"
        std::vector<shader_node_socket_t> inputs;
        std::vector<shader_node_socket_t> outputs;
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
        std::vector<shader_node_t> nodes;
        std::vector<shader_link_t> links;
    };

    struct data_t
    {
        std::vector<scene_t> scenes;
        std::unordered_map<std::string, mesh_t> meshes;
        std::unordered_map<std::string, image_t> images;
        std::unordered_map<std::string, material_t> materials;
    };
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
        struct socket_endpoint_t
        {
            std::string node_name;
            std::string socket_id;
        };

        std::function<void(bNodeTree * tree, const std::string& prefix, material_t& material_data, const std::map<std::string, socket_endpoint_t>& input_mappings, std::map<std::string, socket_endpoint_t>& output_mappings)> flatten_node_tree;
        flatten_node_tree = [&flatten_node_tree](bNodeTree* tree, const std::string& prefix, material_t& material_data, const std::map<std::string, socket_endpoint_t>& input_mappings, std::map<std::string, socket_endpoint_t>& output_mappings)
        {
            if (tree == nullptr) return;

            // Build reroute resolution map: traces through reroute chains to find actual source
            // Maps reroute node name -> (source node, source socket)
            std::map<std::string, std::pair<bNode*, bNodeSocket*>> reroute_sources;
            {
                // First, find what each reroute's input is directly connected to
                std::map<std::string, std::pair<bNode*, bNodeSocket*>> direct_sources;
                LISTBASE_FOREACH(bNodeLink*, link, &tree->links)
                {
                    if (!link->fromnode || !link->tonode || !link->fromsock || !link->tosock) continue;
                    if (strcmp(link->tonode->idname, "NodeReroute") == 0)
                    {
                        direct_sources[link->tonode->name] = {link->fromnode, link->fromsock};
                    }
                }

                // Resolve chains: trace through reroutes to find the actual source
                for (auto& [reroute_name, source] : direct_sources)
                {
                    bNode* current_node       = source.first;
                    bNodeSocket* current_sock = source.second;
                    while (current_node && strcmp(current_node->idname, "NodeReroute") == 0)
                    {
                        auto it = direct_sources.find(current_node->name);
                        if (it == direct_sources.end()) break;
                        current_node = it->second.first;
                        current_sock = it->second.second;
                    }
                    reroute_sources[reroute_name] = {current_node, current_sock};
                }
            }

            // Helper to resolve actual source (handles reroutes)
            auto resolve_source = [&](bNode* from_node, bNodeSocket* from_sock) -> std::pair<bNode*, bNodeSocket*>
            {
                if (strcmp(from_node->idname, "NodeReroute") == 0)
                {
                    auto it = reroute_sources.find(from_node->name);
                    if (it != reroute_sources.end())
                    {
                        return it->second;
                    }
                }
                return {from_node, from_sock};
            };

            // First pass: identify group nodes, build their io mappings, collect internal links for resolving group io
            std::map<std::string, bNodeTree*> group_trees;
            std::map<std::string, std::map<std::string, socket_endpoint_t>> group_input_maps;
            std::map<std::string, std::map<std::string, socket_endpoint_t>> group_output_maps;
            LISTBASE_FOREACH(bNode*, node, &tree->nodes)
            {
                if (strcmp(node->idname, "ShaderNodeGroup") != 0 || node->id == nullptr)
                {
                    continue;
                }

                bNodeTree* group_tree   = (bNodeTree*)node->id;
                group_trees[node->name] = group_tree;

                LISTBASE_FOREACH(bNodeLink*, link, &group_tree->links)
                {
                    if (!link->fromnode || !link->tonode || !link->fromsock || !link->tosock) continue;
                    if (strcmp(link->fromnode->idname, "NodeGroupInput") == 0) group_input_maps[node->name][link->fromsock->identifier] = {prefix + node->name + "." + link->tonode->name, link->tosock->identifier};
                    if (strcmp(link->tonode->idname, "NodeGroupOutput") == 0) group_output_maps[node->name][link->tosock->identifier] = {prefix + node->name + "." + link->fromnode->name, link->fromsock->identifier};
                }
            }

            // Second pass: process all nodes
            LISTBASE_FOREACH(bNode*, node, &tree->nodes)
            {
                if (strcmp(node->idname, "NodeGroupInput") == 0 || strcmp(node->idname, "NodeGroupOutput") == 0 || strcmp(node->idname, "NodeReroute") == 0)
                {
                    continue;
                }
                else if (strcmp(node->idname, "ShaderNodeGroup") == 0 && node->id != nullptr)
                {
                    std::map<std::string, socket_endpoint_t> nested_input_mappings;
                    LISTBASE_FOREACH(bNodeLink*, link, &tree->links)
                    {
                        if (link->tonode != node || !link->fromnode || !link->fromsock || !link->tosock)
                        {
                            continue;
                        }

                        // Resolve through reroutes
                        auto [resolved_node, resolved_sock] = resolve_source(link->fromnode, link->fromsock);
                        if (!resolved_node) continue;

                        if (strcmp(resolved_node->idname, "ShaderNodeGroup") == 0)
                        {
                            auto& src_output_map = group_output_maps[resolved_node->name];
                            auto it              = src_output_map.find(resolved_sock->identifier);
                            if (it != src_output_map.end())
                            {
                                nested_input_mappings[link->tosock->identifier] = it->second;
                                continue;
                            }
                        }
                        else if (strcmp(resolved_node->idname, "NodeGroupInput") == 0)
                        {
                            auto it = input_mappings.find(resolved_sock->identifier);
                            if (it != input_mappings.end())
                            {
                                nested_input_mappings[link->tosock->identifier] = it->second;
                                continue;
                            }
                        }

                        std::string from_node_name                      = prefix + resolved_node->name;
                        nested_input_mappings[link->tosock->identifier] = {from_node_name, resolved_sock->identifier};
                    }

                    // Recursively flatten the group
                    bNodeTree* group_tree    = (bNodeTree*)node->id;
                    std::string group_prefix = prefix + node->name + ".";
                    std::map<std::string, socket_endpoint_t> nested_output_mappings;
                    flatten_node_tree(group_tree, group_prefix, material_data, nested_input_mappings, nested_output_mappings);

                    // Store output mappings for link resolution
                    group_output_maps[node->name] = nested_output_mappings;
                }
                else
                {
                    shader_node_t node_data;
                    node_data.name   = prefix + node->name;
                    node_data.idname = node->idname;

                    // Input sockets
                    LISTBASE_FOREACH(bNodeSocket*, socket, &node->inputs)
                    {
                        shader_node_socket_t socket_data;
                        socket_data.identifier = socket->identifier;
                        socket_data.idname     = socket->idname;
                        node_data.inputs.push_back(socket_data);
                    }

                    // Output sockets
                    LISTBASE_FOREACH(bNodeSocket*, socket, &node->outputs)
                    {
                        shader_node_socket_t socket_data;
                        socket_data.identifier = socket->identifier;
                        socket_data.idname     = socket->idname;
                        node_data.outputs.push_back(socket_data);
                    }

                    material_data.nodes.push_back(node_data);
                }
            }

            // Third pass: process links and resolve group boundaries
            LISTBASE_FOREACH(bNodeLink*, link, &tree->links)
            {
                if (!link->fromnode || !link->tonode || !link->fromsock || !link->tosock) continue;

                if (strcmp(link->fromnode->idname, "NodeGroupInput") == 0) continue;

                if (strcmp(link->tonode->idname, "NodeReroute") == 0) continue;

                auto [resolved_node, resolved_sock] = resolve_source(link->fromnode, link->fromsock);
                if (!resolved_node) continue;

                if (strcmp(link->tonode->idname, "NodeGroupOutput") == 0)
                {
                    std::string from_node_name = prefix + resolved_node->name;
                    std::string from_socket    = resolved_sock->identifier;

                    if (strcmp(resolved_node->idname, "ShaderNodeGroup") == 0)
                    {
                        auto& src_output_map = group_output_maps[resolved_node->name];
                        auto it              = src_output_map.find(resolved_sock->identifier);
                        if (it != src_output_map.end())
                        {
                            from_node_name = it->second.node_name;
                            from_socket    = it->second.socket_id;
                        }
                    }

                    output_mappings[link->tosock->identifier] = {from_node_name, from_socket};
                    continue;
                }

                if (strcmp(link->tonode->idname, "ShaderNodeGroup") == 0) continue;
                if (strcmp(resolved_node->idname, "ShaderNodeGroup") == 0)
                {
                    auto& src_output_map = group_output_maps[resolved_node->name];
                    auto it              = src_output_map.find(resolved_sock->identifier);
                    if (it != src_output_map.end())
                    {
                        shader_link_t link_data;
                        link_data.from_node   = it->second.node_name;
                        link_data.from_socket = it->second.socket_id;
                        link_data.to_node     = prefix + link->tonode->name;
                        link_data.to_socket   = link->tosock->identifier;
                        material_data.links.push_back(link_data);
                    }
                    continue;
                }

                shader_link_t link_data;
                link_data.from_node   = prefix + resolved_node->name;
                link_data.from_socket = resolved_sock->identifier;
                link_data.to_node     = prefix + link->tonode->name;
                link_data.to_socket   = link->tosock->identifier;
                material_data.links.push_back(link_data);
            }

            // Fourth pass: add links from input mappings for nodes that read from GroupInput
            LISTBASE_FOREACH(bNodeLink*, link, &tree->links)
            {
                if (!link->fromnode || !link->tonode || !link->fromsock || !link->tosock) continue;

                if (strcmp(link->tonode->idname, "NodeReroute") == 0) continue;

                if (strcmp(link->fromnode->idname, "NodeGroupInput") == 0 && strcmp(link->tonode->idname, "NodeGroupOutput") != 0 && strcmp(link->tonode->idname, "ShaderNodeGroup") != 0)
                {
                    auto it = input_mappings.find(link->fromsock->identifier);
                    if (it != input_mappings.end())
                    {
                        shader_link_t link_data;
                        link_data.from_node   = it->second.node_name;
                        link_data.from_socket = it->second.socket_id;
                        link_data.to_node     = prefix + link->tonode->name;
                        link_data.to_socket   = link->tosock->identifier;
                        material_data.links.push_back(link_data);
                    }
                }
            }
        };

        // Process each material
        LISTBASE_FOREACH(Material*, mat, &bmain->materials)
        {
            if (mat->nodetree == nullptr) continue;

            material_t material_data;
            material_data.name = mat->id.name;

            // Flatten tree
            {
                std::map<std::string, socket_endpoint_t> input_mappings;
                std::map<std::string, socket_endpoint_t> output_mappings;

                flatten_node_tree(mat->nodetree, "", material_data, input_mappings, output_mappings);
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
                    fprintf(log_file, "[zachary] Skipping material %s (no Principled BSDF)\n", mat->id.name);
                    continue;
                }
            }

            // Supported Principled BSDF input sockets
            static const std::set<std::string> supported_sockets = {"Base Color", "Emission Color", "Emission Strength", "Roughness", "Metallic", "Specular IOR Level", "Normal", "Alpha"};

            // Build reverse adjacency: for each node, which nodes feed into it
            std::map<std::string, std::vector<std::string>> reverse_adj;
            {
                for (const auto& link : material_data.links)
                {
                    reverse_adj[link.to_node].push_back(link.from_node);
                }
            }

            // BFS backwards from supported Principled BSDF sockets only
            std::set<std::string> reachable_nodes;
            {
                reachable_nodes.insert(principled_node_name.value());

                std::vector<std::string> queue;
                for (const auto& link : material_data.links)
                {
                    if (link.to_node == principled_node_name.value() && supported_sockets.count(link.to_socket))
                    {
                        if (reachable_nodes.find(link.from_node) == reachable_nodes.end())
                        {
                            reachable_nodes.insert(link.from_node);
                            queue.push_back(link.from_node);
                        }
                    }
                }

                while (!queue.empty())
                {
                    std::string current = queue.back();
                    queue.pop_back();
                    for (const auto& pred : reverse_adj[current])
                    {
                        if (reachable_nodes.find(pred) == reachable_nodes.end())
                        {
                            reachable_nodes.insert(pred);
                            queue.push_back(pred);
                        }
                    }
                }
            }

            // Filter nodes to only those reachable
            std::vector<shader_node_t> filtered_nodes;
            {
                for (auto& node : material_data.nodes)
                {
                    if (reachable_nodes.count(node.name))
                    {
                        filtered_nodes.push_back(std::move(node));
                    }
                }
            }

            // Filter links to only those between reachable nodes, and only supported sockets for Principled BSDF
            std::vector<shader_link_t> filtered_links;
            {
                for (auto& link : material_data.links)
                {
                    if (!reachable_nodes.count(link.from_node) || !reachable_nodes.count(link.to_node))
                    {
                        continue;
                    }
                    // For links going to Principled BSDF, only keep supported sockets
                    if (link.to_node == principled_node_name.value() && !supported_sockets.count(link.to_socket))
                    {
                        continue;
                    }
                    filtered_links.push_back(std::move(link));
                }
            }

            material_data.nodes                                                              = std::move(filtered_nodes);
            material_data.links                                                              = std::move(filtered_links);

            static const std::map<std::string, std::set<std::string>> supported_node_outputs = {
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

            bool is_supported = true;
            {
                for (const auto& node : material_data.nodes)
                {
                    if (supported_node_outputs.find(node.idname) == supported_node_outputs.end())
                    {
                        fprintf(log_file, "[zachary] Skipping material %s (unsupported node type: %s)\n", mat->id.name, node.idname.c_str());
                        is_supported = false;
                        break;
                    }
                }

                if (is_supported)
                {
                    std::map<std::string, std::string> node_name_to_idname;
                    for (const auto& node : material_data.nodes)
                    {
                        node_name_to_idname[node.name] = node.idname;
                    }

                    for (const auto& link : material_data.links)
                    {
                        auto node_it = node_name_to_idname.find(link.from_node);
                        if (node_it == node_name_to_idname.end()) continue;

                        const std::string& idname     = node_it->second;
                        const auto& supported_outputs = supported_node_outputs.at(idname);

                        if (!supported_outputs.count(link.from_socket))
                        {
                            fprintf(log_file, "[zachary] Skipping material %s (unsupported output socket: %s.%s)\n", mat->id.name, idname.c_str(), link.from_socket.c_str());
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

            data.materials[material_data.name] = std::move(material_data);
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
                        fprintf(log_file, "[zachary] Skipping mesh %s (has empty material slot)\n", mesh->id.name);
                        has_invalid_material = true;
                        break;
                    }
                    std::string mat_name = mesh->mat[i]->id.name;
                    if (data.materials.find(mat_name) == data.materials.end())
                    {
                        fprintf(log_file, "[zachary] Skipping mesh %s (material %s not in map)\n", mesh->id.name, mat_name.c_str());
                        has_invalid_material = true;
                        break;
                    }
                }

                if (has_invalid_material)
                {
                    continue;
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

            mesh_t mesh_data;
            mesh_data.name = mesh->id.name;

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

            // Check for invalid material indices and skip entire mesh if found
            bool has_invalid_mat_idx = false;
            for (const auto& [mat_idx, _] : triangles_by_material)
            {
                if (mat_idx < 0 || mat_idx >= mesh->totcol || !mesh->mat[mat_idx])
                {
                    fprintf(log_file, "[zachary] Skipping mesh %s (face references invalid material index %d)\n", mesh->id.name, mat_idx);
                    has_invalid_mat_idx = true;
                    break;
                }
            }
            if (has_invalid_mat_idx)
            {
                continue;
            }

            // Create submeshes from grouped triangles
            for (const auto& [mat_idx, triangle_indices] : triangles_by_material)
            {
                submesh_t submesh;

                submesh.material_name = mesh->mat[mat_idx]->id.name;

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
                    }
                    submesh.triangles.push_back(triangle);
                }
                mesh_data.submeshes.push_back(submesh);
            }

            data.meshes[mesh_data.name] = std::move(mesh_data);
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
                    if (data.meshes.find(mesh_name) == data.meshes.end())
                    {
                        continue;
                    }

                    scene_elem_t elem           = {};
                    elem.name                   = ob->id.name;
                    elem.parent_name            = parent_name;
                    elem.pos.x                  = ob->loc[0];
                    elem.pos.y                  = ob->loc[1];
                    elem.pos.z                  = ob->loc[2];
                    elem.euler_rot.x            = ob->rot[0];
                    elem.euler_rot.y            = ob->rot[1];
                    elem.euler_rot.z            = ob->rot[2];
                    elem.scale.x                = ob->scale[0];
                    elem.scale.y                = ob->scale[1];
                    elem.scale.z                = ob->scale[2];
                    elem.mesh_name              = mesh_name;

                    scene_data.elems[elem.name] = std::move(elem);
                }
                else if (ob->type == OB_ARMATURE)
                {
                    bArmature* arm              = (bArmature*)ob->data;
                    std::string skel_name       = arm->id.name;

                    scene_elem_t elem           = {};
                    elem.name                   = ob->id.name;
                    elem.parent_name            = parent_name;
                    elem.pos.x                  = ob->loc[0];
                    elem.pos.y                  = ob->loc[1];
                    elem.pos.z                  = ob->loc[2];
                    elem.euler_rot.x            = ob->rot[0];
                    elem.euler_rot.y            = ob->rot[1];
                    elem.euler_rot.z            = ob->rot[2];
                    elem.scale.x                = ob->scale[0];
                    elem.scale.y                = ob->scale[1];
                    elem.scale.z                = ob->scale[2];
                    elem.skel_name              = skel_name;

                    scene_data.elems[elem.name] = std::move(elem);
                }
            }
            FOREACH_SCENE_OBJECT_END;

            data.scenes.push_back(scene_data);
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

            LISTBASE_FOREACH(ImagePackedFile*, imapf, &ima->packedfiles)
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
                data.images[image.name] = std::move(image);
            }
        }
    }

    // ===============================================================================================
    // ===============================================================================================
    // dump data_t to log file
    // ===============================================================================================
    // ===============================================================================================
    {
        fprintf(log_file, "[zachary] === DATA DUMP ===\n\n");

        // Scenes
        fprintf(log_file, "SCENES (%zu):\n", data.scenes.size());
        for (const auto& scene : data.scenes)
        {
            fprintf(log_file, "  scene: %s\n", scene.name.c_str());
            for (const auto& [elem_name, elem] : scene.elems)
            {
                fprintf(log_file, "    elem: %s\n", elem.name.c_str());
                if (elem.parent_name.has_value())
                {
                    fprintf(log_file, "      parent: %s\n", elem.parent_name.value().c_str());
                }
                fprintf(log_file, "      pos: (%.3f, %.3f, %.3f)\n", elem.pos.x, elem.pos.y, elem.pos.z);
                fprintf(log_file, "      rot: (%.3f, %.3f, %.3f)\n", elem.euler_rot.x, elem.euler_rot.y, elem.euler_rot.z);
                fprintf(log_file, "      scale: (%.3f, %.3f, %.3f)\n", elem.scale.x, elem.scale.y, elem.scale.z);
                if (elem.mesh_name.has_value())
                {
                    fprintf(log_file, "      mesh: %s\n", elem.mesh_name.value().c_str());
                }
                if (elem.skel_name.has_value())
                {
                    fprintf(log_file, "      skel: %s\n", elem.skel_name.value().c_str());
                }
            }
        }
        fprintf(log_file, "\n");

        // Meshes
        fprintf(log_file, "MESHES (%zu):\n", data.meshes.size());
        for (const auto& [mesh_name, mesh] : data.meshes)
        {
            size_t total_tris = 0;
            for (const auto& submesh : mesh.submeshes)
            {
                total_tris += submesh.triangles.size();
            }
            fprintf(log_file, "  mesh: %s (%zu submeshes, %zu triangles)\n", mesh.name.c_str(), mesh.submeshes.size(), total_tris);
            for (size_t i = 0; i < mesh.submeshes.size(); i++)
            {
                const auto& submesh = mesh.submeshes[i];
                fprintf(log_file, "    submesh[%zu]: material=%s, tris=%zu\n", i, submesh.material_name.c_str(), submesh.triangles.size());
            }
        }
        fprintf(log_file, "\n");

        // Images
        fprintf(log_file, "IMAGES (%zu):\n", data.images.size());
        for (const auto& [image_name, image] : data.images)
        {
            fprintf(log_file, "  image: %s (%ux%u, srgb=%s, %zu bytes)\n", image.name.c_str(), image.width, image.height, image.is_srgb ? "true" : "false", image.rgba_data.size());
        }
        fprintf(log_file, "\n");

        // Materials
        fprintf(log_file, "MATERIALS (%zu):\n", data.materials.size());
        for (const auto& [mat_name, mat] : data.materials)
        {
            fprintf(log_file, "  material: %s (%zu nodes, %zu links)\n", mat.name.c_str(), mat.nodes.size(), mat.links.size());
            for (size_t i = 0; i < mat.nodes.size(); i++)
            {
                const auto& node = mat.nodes[i];
                fprintf(log_file, "    node[%zu]: %s [%s] (inputs=%zu, outputs=%zu)\n", i, node.name.c_str(), node.idname.c_str(), node.inputs.size(), node.outputs.size());
                for (const auto& input : node.inputs)
                {
                    fprintf(log_file, "      in: %s (%s)\n", input.identifier.c_str(), input.idname.c_str());
                }
                for (const auto& output : node.outputs)
                {
                    fprintf(log_file, "      out: %s (%s)\n", output.identifier.c_str(), output.idname.c_str());
                }
            }
            for (const auto& link : mat.links)
            {
                fprintf(log_file, "    link: %s.%s -> %s.%s\n", link.from_node.c_str(), link.from_socket.c_str(), link.to_node.c_str(), link.to_socket.c_str());
            }
        }

        fprintf(log_file, "\n[zachary] === END DATA DUMP ===\n");
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
        mesh_count   = data.meshes.size();
        meshes       = (BBArchiveMesh*)arena_alloc_z(arena, sizeof(BBArchiveMesh) * mesh_count)->data;

        int mesh_idx = 0;
        for (const auto& [mesh_name, mesh] : data.meshes)
        {

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

                // Fill data arrays from triangles
                int vertex_offset            = 0;
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
                        uv0_data[vertex_offset * 2 + 0]       = vert.uv0.x;
                        uv0_data[vertex_offset * 2 + 1]       = vert.uv0.y;
                        uv1_data[vertex_offset * 2 + 0]       = vert.uv1.x;
                        uv1_data[vertex_offset * 2 + 1]       = vert.uv1.y;
                        uv2_data[vertex_offset * 2 + 0]       = vert.uv2.x;
                        uv2_data[vertex_offset * 2 + 1]       = vert.uv2.y;

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
                submesh.joint_indices     = nullptr;
                submesh.joint_weights     = nullptr;
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
    std::vector<BBArchiveScene> bb_scenes;
    {
        bb_scenes.resize(data.scenes.size());

        for (size_t scene_idx = 0; scene_idx < data.scenes.size(); scene_idx++)
        {
            const scene_t& scene   = data.scenes[scene_idx];

            size_t mesh_elem_count = 0;
            for (const auto& [elem_name, src_elem] : scene.elems)
            {
                if (src_elem.mesh_name.has_value()) mesh_elem_count++;
            }

            BBArchiveSceneElem* elems = (BBArchiveSceneElem*)arena_alloc_z(arena, sizeof(BBArchiveSceneElem) * mesh_elem_count)->data;

            size_t elem_idx           = 0;
            for (const auto& [elem_name, src_elem] : scene.elems)
            {
                if (!src_elem.mesh_name.has_value()) continue;

                const mesh_t& mesh     = data.meshes.at(src_elem.mesh_name.value());

                int mat_count          = mesh.submeshes.size();
                const char** mat_names = (const char**)arena_alloc_z(arena, sizeof(const char*) * mat_count)->data;
                for (size_t i = 0; i < mesh.submeshes.size(); i++)
                {
                    mat_names[i] = mesh.submeshes[i].material_name.c_str();
                }

                BBArchiveSceneElem& elem = elems[elem_idx];
                elem.mesh_name           = src_elem.mesh_name.value().c_str();
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

            bb_scenes[scene_idx].scene_name = scene.name.c_str();
            bb_scenes[scene_idx].elem_count = mesh_elem_count;
            bb_scenes[scene_idx].elems      = elems;
        }
    }

    // ===============================================================================================
    // ===============================================================================================
    // Extract images for bb_archive
    // ===============================================================================================
    // ===============================================================================================
    std::vector<BBArchiveImage> bb_images;
    {
        bb_images.resize(data.images.size());

        size_t image_idx = 0;
        for (const auto& [image_name, image] : data.images)
        {
            BBArchiveImage& bb_image  = bb_images[image_idx];
            bb_image.image_name       = image.name.c_str();
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
    // Extract meshes and write to bb_archive
    // ===============================================================================================
    // ===============================================================================================
    {
        BBArchiveInfo info      = {0};
        info.mesh_count         = mesh_count;
        info.image_count        = bb_images.size();
        info.skeleton_count     = 0;
        info.animation_count    = 0;
        info.shader_graph_count = 0;
        info.scene_count        = bb_scenes.size();
        info.meshes             = meshes;
        info.images             = bb_images.data();
        info.skeletons          = nullptr;
        info.animations         = nullptr;
        info.shader_graphs      = nullptr;
        info.scenes             = bb_scenes.data();

        bb_archive_write(&info, bb_archive_output_dir);
        fprintf(log_file, "[zachary] bb_archive_write completed\n");
    }

    fclose(log_file);
    arena_destroy_z(arena);
}
