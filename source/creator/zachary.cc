#include "zachary.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <unordered_map>
#include <variant>
#include <vector>

#include "BLI_listbase.h"
#include "BLI_math_vector_types.hh"

#include "DNA_armature_types.h"
#include "DNA_colorband_types.h"
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
        float x, y, z;
    };
    struct socket_value_rgba_t
    {
        float r, g, b, a;
    };
    struct socket_value_string_t
    {
        std::string value;
    };

    using socket_default_value_t = std::variant<socket_value_float_t, socket_value_int_t, socket_value_bool_t, socket_value_vector_t, socket_value_rgba_t, socket_value_string_t>;

    struct shader_node_socket_t
    {
        std::string identifier;
        std::string idname; // e.g. "NodeSocketColor", "NodeSocketFloat"
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
        std::string idname; // e.g. "ShaderNodeTexImage", "ShaderNodeBsdfPrincipled"
        std::vector<shader_node_socket_t> inputs;
        std::vector<shader_node_socket_t> outputs;
        std::vector<shader_node_prop_t> props;
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
                    auto extract_default_value = [](bNodeSocket* socket) -> std::optional<socket_default_value_t>
                    {
                        if (socket->default_value == nullptr) return std::nullopt;

                        if (strncmp(socket->idname, "NodeSocketFloat", 15) == 0)
                        {
                            auto* val = static_cast<bNodeSocketValueFloat*>(socket->default_value);
                            return socket_value_float_t{val->value};
                        }
                        else if (strncmp(socket->idname, "NodeSocketInt", 13) == 0)
                        {
                            auto* val = static_cast<bNodeSocketValueInt*>(socket->default_value);
                            return socket_value_int_t{val->value};
                        }
                        else if (strcmp(socket->idname, "NodeSocketBool") == 0)
                        {
                            auto* val = static_cast<bNodeSocketValueBoolean*>(socket->default_value);
                            return socket_value_bool_t{val->value != 0};
                        }
                        else if (strncmp(socket->idname, "NodeSocketVector", 16) == 0)
                        {
                            auto* val = static_cast<bNodeSocketValueVector*>(socket->default_value);
                            return socket_value_vector_t{val->value[0], val->value[1], val->value[2]};
                        }
                        else if (strcmp(socket->idname, "NodeSocketColor") == 0)
                        {
                            auto* val = static_cast<bNodeSocketValueRGBA*>(socket->default_value);
                            return socket_value_rgba_t{val->value[0], val->value[1], val->value[2], val->value[3]};
                        }

                        return std::nullopt;
                    };

                    shader_node_t node_data;
                    node_data.name   = prefix + node->name;
                    node_data.idname = node->idname;

                    // Input sockets
                    LISTBASE_FOREACH(bNodeSocket*, socket, &node->inputs)
                    {
                        shader_node_socket_t socket_data;
                        socket_data.identifier    = socket->identifier;
                        socket_data.idname        = socket->idname;
                        socket_data.default_value = extract_default_value(socket);
                        node_data.inputs.push_back(socket_data);
                    }

                    // Output sockets
                    LISTBASE_FOREACH(bNodeSocket*, socket, &node->outputs)
                    {
                        shader_node_socket_t socket_data;
                        socket_data.identifier    = socket->identifier;
                        socket_data.idname        = socket->idname;
                        socket_data.default_value = extract_default_value(socket);
                        node_data.outputs.push_back(socket_data);
                    }

                    // Node properties
                    auto add_int_prop = [&](const char* id, int val)
                    {
                        shader_node_prop_t p;
                        p.identifier = id;
                        p.value      = socket_value_int_t{val};
                        node_data.props.push_back(p);
                    };
                    auto add_bool_prop = [&](const char* id, bool val)
                    {
                        shader_node_prop_t p;
                        p.identifier = id;
                        p.value      = socket_value_bool_t{val};
                        node_data.props.push_back(p);
                    };
                    auto add_float_prop = [&](const char* id, float val)
                    {
                        shader_node_prop_t p;
                        p.identifier = id;
                        p.value      = socket_value_float_t{val};
                        node_data.props.push_back(p);
                    };
                    auto add_string_prop = [&](const char* id, const std::string& val)
                    {
                        shader_node_prop_t p;
                        p.identifier = id;
                        p.value      = socket_value_string_t{val};
                        node_data.props.push_back(p);
                    };
                    auto add_rgba_prop = [&](const char* id, float r, float g, float b, float a)
                    {
                        shader_node_prop_t p;
                        p.identifier = id;
                        p.value      = socket_value_rgba_t{r, g, b, a};
                        node_data.props.push_back(p);
                    };

                    if (strcmp(node->idname, "ShaderNodeUVMap") == 0)
                    {
                        NodeShaderUVMap* data = (NodeShaderUVMap*)node->storage;
                        if (data) add_string_prop("uv_map", data->uv_map);
                    }
                    else if (strcmp(node->idname, "ShaderNodeMath") == 0)
                    {
                        add_int_prop("operation", node->custom1);
                        add_bool_prop("use_clamp", node->custom2 != 0);
                    }
                    else if (strcmp(node->idname, "ShaderNodeVectorMath") == 0)
                    {
                        add_int_prop("operation", node->custom1);
                    }
                    else if (strcmp(node->idname, "ShaderNodeMix") == 0)
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
                    else if (strcmp(node->idname, "ShaderNodeSeparateColor") == 0 || strcmp(node->idname, "ShaderNodeCombineColor") == 0)
                    {
                        NodeCombSepColor* data = (NodeCombSepColor*)node->storage;
                        if (data) add_int_prop("mode", data->mode);
                    }
                    else if (strcmp(node->idname, "ShaderNodeTexNoise") == 0)
                    {
                        NodeTexNoise* data = (NodeTexNoise*)node->storage;
                        if (data)
                        {
                            add_int_prop("noise_dimensions", data->dimensions);
                            add_int_prop("noise_type", data->type);
                            add_bool_prop("normalize", data->normalize != 0);
                        }
                    }
                    else if (strcmp(node->idname, "ShaderNodeTexImage") == 0)
                    {
                        if (node->id != nullptr)
                        {
                            Image* ima = (Image*)node->id;
                            add_string_prop("image", ima->id.name);
                        }
                        NodeTexImage* data = (NodeTexImage*)node->storage;
                        if (data)
                        {
                            add_int_prop("interpolation", data->interpolation);
                            add_int_prop("projection", data->projection);
                            add_int_prop("extension", data->extension);
                        }
                    }
                    else if (strcmp(node->idname, "ShaderNodeValToRGB") == 0)
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
                    else if (strcmp(node->idname, "ShaderNodeAmbientOcclusion") == 0)
                    {
                        add_int_prop("samples", node->custom1);
                        add_bool_prop("inside", (node->custom2 & SHD_AO_INSIDE) != 0);
                        add_bool_prop("only_local", (node->custom2 & SHD_AO_LOCAL) != 0);
                    }
                    else if (strcmp(node->idname, "ShaderNodeBump") == 0)
                    {
                        add_bool_prop("invert", node->custom1 != 0);
                    }
                    else if (strcmp(node->idname, "ShaderNodeClamp") == 0)
                    {
                        add_int_prop("clamp_type", node->custom1);
                    }
                    else if (strcmp(node->idname, "ShaderNodeDisplacement") == 0)
                    {
                        add_int_prop("space", node->custom1);
                    }
                    else if (strcmp(node->idname, "ShaderNodeMapRange") == 0)
                    {
                        NodeMapRange* data = (NodeMapRange*)node->storage;
                        if (data)
                        {
                            add_int_prop("data_type", data->data_type);
                            add_int_prop("interpolation_type", data->interpolation_type);
                            add_bool_prop("clamp", data->clamp != 0);
                        }
                    }
                    else if (strcmp(node->idname, "ShaderNodeMapping") == 0)
                    {
                        add_int_prop("vector_type", node->custom1);
                    }
                    else if (strcmp(node->idname, "ShaderNodeNormalMap") == 0)
                    {
                        NodeShaderNormalMap* data = (NodeShaderNormalMap*)node->storage;
                        if (data)
                        {
                            add_int_prop("space", data->space);
                            add_string_prop("uv_map", data->uv_map);
                        }
                    }
                    else if (strcmp(node->idname, "ShaderNodeTangent") == 0)
                    {
                        NodeShaderTangent* data = (NodeShaderTangent*)node->storage;
                        if (data)
                        {
                            add_int_prop("direction_type", data->direction_type);
                            add_int_prop("axis", data->axis);
                            add_string_prop("uv_map", data->uv_map);
                        }
                    }
                    else if (strcmp(node->idname, "ShaderNodeTexBrick") == 0)
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
                for (const auto& node : filtered_nodes)
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
                    for (const auto& node : filtered_nodes)
                    {
                        node_name_to_idname[node.name] = node.idname;
                    }

                    for (const auto& link : filtered_links)
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

            // Filter sockets to only supported ones
            {
                for (auto& node : filtered_nodes)
                {
                    // Filter node output sockets
                    auto it = supported_node_outputs.find(node.idname);
                    if (it != supported_node_outputs.end())
                    {
                        const auto& allowed_outputs = it->second;
                        std::vector<shader_node_socket_t> new_outputs;
                        for (auto& socket : node.outputs)
                        {
                            if (allowed_outputs.count(socket.identifier))
                            {
                                new_outputs.push_back(std::move(socket));
                            }
                        }
                        node.outputs = std::move(new_outputs);
                    }

                    // Filter Principled BSDF input sockets
                    if (node.idname == "ShaderNodeBsdfPrincipled")
                    {
                        std::vector<shader_node_socket_t> new_inputs;
                        for (auto& socket : node.inputs)
                        {
                            if (supported_sockets.count(socket.identifier))
                            {
                                new_inputs.push_back(std::move(socket));
                            }
                        }
                        node.inputs = std::move(new_inputs);
                    }
                }
            }

            // Topological sort: dependencies before dependents, Principled BSDF at the end
            {
                // Build adjacency list and in-degree count
                std::unordered_map<std::string, std::vector<std::string>> dependents;
                std::unordered_map<std::string, int> in_degree;

                // Initialize all nodes with in_degree 0
                for (const auto& node : filtered_nodes)
                {
                    in_degree[node.name] = 0;
                }

                // Build graph from links
                for (const auto& link : filtered_links)
                {
                    dependents[link.from_node].push_back(link.to_node);
                    in_degree[link.to_node]++;
                }

                // Kahn's algorithm
                std::queue<std::string> queue;
                for (const auto& [name, degree] : in_degree)
                {
                    if (degree == 0)
                    {
                        queue.push(name);
                    }
                }

                std::vector<std::string> sorted_order;
                while (!queue.empty())
                {
                    std::string current = queue.front();
                    queue.pop();
                    sorted_order.push_back(current);

                    for (const auto& dependent : dependents[current])
                    {
                        in_degree[dependent]--;
                        if (in_degree[dependent] == 0)
                        {
                            queue.push(dependent);
                        }
                    }
                }

                // Reorder filtered_nodes according to sorted_order
                std::unordered_map<std::string, shader_node_t> node_map;
                for (auto& node : filtered_nodes)
                {
                    node_map[node.name] = std::move(node);
                }

                filtered_nodes.clear();
                for (const auto& name : sorted_order)
                {
                    auto it = node_map.find(name);
                    if (it != node_map.end())
                    {
                        filtered_nodes.push_back(std::move(it->second));
                    }
                }
            }

            material_data.nodes                = std::move(filtered_nodes);
            material_data.links                = std::move(filtered_links);

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
                fprintf(log_file, "    node[%zu]: %s [%s] (inputs=%zu, outputs=%zu, props=%zu)\n", i, node.name.c_str(), node.idname.c_str(), node.inputs.size(), node.outputs.size(), node.props.size());
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
                            else if constexpr (std::is_same_v<T, socket_value_vector_t>) return " = (" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
                            else if constexpr (std::is_same_v<T, socket_value_rgba_t>) return " = (" + std::to_string(v.r) + ", " + std::to_string(v.g) + ", " + std::to_string(v.b) + ", " + std::to_string(v.a) + ")";
                            else if constexpr (std::is_same_v<T, socket_value_string_t>) return " = \"" + v.value + "\"";
                            else return "";
                        },
                        val.value()
                    );
                };
                for (const auto& input : node.inputs)
                {
                    fprintf(log_file, "      in: %s (%s)%s\n", input.identifier.c_str(), input.idname.c_str(), format_default_value(input.default_value).c_str());
                }
                for (const auto& output : node.outputs)
                {
                    fprintf(log_file, "      out: %s (%s)%s\n", output.identifier.c_str(), output.idname.c_str(), format_default_value(output.default_value).c_str());
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
                            else if constexpr (std::is_same_v<T, socket_value_vector_t>) return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
                            else if constexpr (std::is_same_v<T, socket_value_rgba_t>) return "(" + std::to_string(v.r) + ", " + std::to_string(v.g) + ", " + std::to_string(v.b) + ", " + std::to_string(v.a) + ")";
                            else if constexpr (std::is_same_v<T, socket_value_string_t>) return "\"" + v.value + "\"";
                            else return "";
                        },
                        prop.value
                    );
                    fprintf(log_file, "      prop: %s = %s\n", prop.identifier.c_str(), value_str.c_str());
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
    std::vector<BBArchiveImage> bb_images;
    {
        bb_images.resize(data.images.size());

        size_t image_idx = 0;
        for (const auto& [image_name, image] : data.images)
        {
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
    std::vector<BBArchiveShaderGraph> bb_shaders;
    {
        // Map Blender node idname to bb_shader_node_type_e
        auto get_node_type = [](const std::string& idname) -> std::optional<bb_shader_node_type_e>
        {
            static const std::unordered_map<std::string, bb_shader_node_type_e> node_type_map = {
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
            auto it = node_type_map.find(idname);
            if (it != node_type_map.end()) return it->second;
            return std::nullopt;
        };

        // Map socket identifier to input field type based on node type
        auto get_input_field = [](bb_shader_node_type_e node_type, const std::string& socket_id) -> std::optional<bb_shader_input_field_type_e>
        {
            // Output node inputs
            if (node_type == BB_SHADER_NODE_TYPE_OUTPUT)
            {
                if (socket_id == "Base Color") return BB_SHADER_INPUT_FIELD_OUTPUT_BASE_COLOUR;
                if (socket_id == "Emission Color") return BB_SHADER_INPUT_FIELD_OUTPUT_EMISSION;
                if (socket_id == "Emission Strength") return BB_SHADER_INPUT_FIELD_OUTPUT_EMISSION_STRENGTH;
                if (socket_id == "Roughness") return BB_SHADER_INPUT_FIELD_OUTPUT_ROUGHNESS;
                if (socket_id == "Metallic") return BB_SHADER_INPUT_FIELD_OUTPUT_METALLIC;
                if (socket_id == "Specular IOR Level") return BB_SHADER_INPUT_FIELD_OUTPUT_SPECULAR;
                if (socket_id == "Normal") return BB_SHADER_INPUT_FIELD_OUTPUT_NORMAL;
                if (socket_id == "Alpha") return BB_SHADER_INPUT_FIELD_OUTPUT_ALPHA;
            }
            // Math node
            if (node_type == BB_SHADER_NODE_TYPE_MATH)
            {
                if (socket_id == "Value") return BB_SHADER_INPUT_FIELD_MATH_VALUE;
                if (socket_id == "Value_001") return BB_SHADER_INPUT_FIELD_MATH_VALUE_001;
                if (socket_id == "Value_002") return BB_SHADER_INPUT_FIELD_MATH_VALUE_002;
            }
            // Vector Math node
            if (node_type == BB_SHADER_NODE_TYPE_VECTOR_MATH)
            {
                if (socket_id == "Vector") return BB_SHADER_INPUT_FIELD_VECTOR_MATH_VECTOR;
                if (socket_id == "Vector_001") return BB_SHADER_INPUT_FIELD_VECTOR_MATH_VECTOR_001;
                if (socket_id == "Vector_002") return BB_SHADER_INPUT_FIELD_VECTOR_MATH_VECTOR_002;
                if (socket_id == "Scale") return BB_SHADER_INPUT_FIELD_VECTOR_MATH_SCALE;
            }
            // Mix node
            if (node_type == BB_SHADER_NODE_TYPE_MIX)
            {
                if (socket_id == "Factor_Float") return BB_SHADER_INPUT_FIELD_MIX_FACTOR_FLOAT;
                if (socket_id == "Factor_Vector") return BB_SHADER_INPUT_FIELD_MIX_FACTOR_VECTOR;
                if (socket_id == "A_Float") return BB_SHADER_INPUT_FIELD_MIX_A_FLOAT;
                if (socket_id == "B_Float") return BB_SHADER_INPUT_FIELD_MIX_B_FLOAT;
                if (socket_id == "A_Vector") return BB_SHADER_INPUT_FIELD_MIX_A_VECTOR;
                if (socket_id == "B_Vector") return BB_SHADER_INPUT_FIELD_MIX_B_VECTOR;
                if (socket_id == "A_Color") return BB_SHADER_INPUT_FIELD_MIX_A_COLOR;
                if (socket_id == "B_Color") return BB_SHADER_INPUT_FIELD_MIX_B_COLOR;
                if (socket_id == "A_Rotation") return BB_SHADER_INPUT_FIELD_MIX_A_ROTATION;
                if (socket_id == "B_Rotation") return BB_SHADER_INPUT_FIELD_MIX_B_ROTATION;
            }
            // Separate/Combine Color
            if (node_type == BB_SHADER_NODE_TYPE_SEPARATE_COLOR)
            {
                if (socket_id == "Color") return BB_SHADER_INPUT_FIELD_SEPARATE_COLOR_COLOR;
            }
            if (node_type == BB_SHADER_NODE_TYPE_COMBINE_COLOR)
            {
                if (socket_id == "Red") return BB_SHADER_INPUT_FIELD_COMBINE_COLOR_RED;
                if (socket_id == "Green") return BB_SHADER_INPUT_FIELD_COMBINE_COLOR_GREEN;
                if (socket_id == "Blue") return BB_SHADER_INPUT_FIELD_COMBINE_COLOR_BLUE;
            }
            // Separate/Combine XYZ
            if (node_type == BB_SHADER_NODE_TYPE_SEPARATE_XYZ)
            {
                if (socket_id == "Vector") return BB_SHADER_INPUT_FIELD_SEPARATE_XYZ_VECTOR;
            }
            if (node_type == BB_SHADER_NODE_TYPE_COMBINE_XYZ)
            {
                if (socket_id == "X") return BB_SHADER_INPUT_FIELD_COMBINE_XYZ_X;
                if (socket_id == "Y") return BB_SHADER_INPUT_FIELD_COMBINE_XYZ_Y;
                if (socket_id == "Z") return BB_SHADER_INPUT_FIELD_COMBINE_XYZ_Z;
            }
            // Tex Noise
            if (node_type == BB_SHADER_NODE_TYPE_TEX_NOISE)
            {
                if (socket_id == "Vector") return BB_SHADER_INPUT_FIELD_TEX_NOISE_VECTOR;
                if (socket_id == "W") return BB_SHADER_INPUT_FIELD_TEX_NOISE_W;
                if (socket_id == "Scale") return BB_SHADER_INPUT_FIELD_TEX_NOISE_SCALE;
                if (socket_id == "Detail") return BB_SHADER_INPUT_FIELD_TEX_NOISE_DETAIL;
                if (socket_id == "Roughness") return BB_SHADER_INPUT_FIELD_TEX_NOISE_ROUGHNESS;
                if (socket_id == "Lacunarity") return BB_SHADER_INPUT_FIELD_TEX_NOISE_LACUNARITY;
                if (socket_id == "Offset") return BB_SHADER_INPUT_FIELD_TEX_NOISE_OFFSET;
                if (socket_id == "Gain") return BB_SHADER_INPUT_FIELD_TEX_NOISE_GAIN;
                if (socket_id == "Distortion") return BB_SHADER_INPUT_FIELD_TEX_NOISE_DISTORTION;
            }
            // Tex Image
            if (node_type == BB_SHADER_NODE_TYPE_TEX_IMAGE)
            {
                if (socket_id == "Vector") return BB_SHADER_INPUT_FIELD_TEX_IMAGE_VECTOR;
            }
            // Val To RGB
            if (node_type == BB_SHADER_NODE_TYPE_VAL_TO_RGB)
            {
                if (socket_id == "Fac") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_FAC;
            }
            // Ambient Occlusion
            if (node_type == BB_SHADER_NODE_TYPE_AMBIENT_OCCLUSION)
            {
                if (socket_id == "Color") return BB_SHADER_INPUT_FIELD_AMBIENT_OCCLUSION_COLOR;
                if (socket_id == "Distance") return BB_SHADER_INPUT_FIELD_AMBIENT_OCCLUSION_DISTANCE;
                if (socket_id == "Normal") return BB_SHADER_INPUT_FIELD_AMBIENT_OCCLUSION_NORMAL;
            }
            // Bump
            if (node_type == BB_SHADER_NODE_TYPE_BUMP)
            {
                if (socket_id == "Strength") return BB_SHADER_INPUT_FIELD_BUMP_STRENGTH;
                if (socket_id == "Distance") return BB_SHADER_INPUT_FIELD_BUMP_DISTANCE;
                if (socket_id == "Filter Width") return BB_SHADER_INPUT_FIELD_BUMP_FILTER_WIDTH;
                if (socket_id == "Height") return BB_SHADER_INPUT_FIELD_BUMP_HEIGHT;
                if (socket_id == "Normal") return BB_SHADER_INPUT_FIELD_BUMP_NORMAL;
            }
            // Clamp
            if (node_type == BB_SHADER_NODE_TYPE_CLAMP)
            {
                if (socket_id == "Value") return BB_SHADER_INPUT_FIELD_CLAMP_VALUE;
                if (socket_id == "Min") return BB_SHADER_INPUT_FIELD_CLAMP_MIN;
                if (socket_id == "Max") return BB_SHADER_INPUT_FIELD_CLAMP_MAX;
            }
            // Displacement
            if (node_type == BB_SHADER_NODE_TYPE_DISPLACEMENT)
            {
                if (socket_id == "Height") return BB_SHADER_INPUT_FIELD_DISPLACEMENT_HEIGHT;
                if (socket_id == "Midlevel") return BB_SHADER_INPUT_FIELD_DISPLACEMENT_MIDLEVEL;
                if (socket_id == "Scale") return BB_SHADER_INPUT_FIELD_DISPLACEMENT_SCALE;
                if (socket_id == "Normal") return BB_SHADER_INPUT_FIELD_DISPLACEMENT_NORMAL;
            }
            // Map Range
            if (node_type == BB_SHADER_NODE_TYPE_MAP_RANGE)
            {
                if (socket_id == "Value") return BB_SHADER_INPUT_FIELD_MAP_RANGE_VALUE;
                if (socket_id == "From Min") return BB_SHADER_INPUT_FIELD_MAP_RANGE_FROM_MIN;
                if (socket_id == "From Max") return BB_SHADER_INPUT_FIELD_MAP_RANGE_FROM_MAX;
                if (socket_id == "To Min") return BB_SHADER_INPUT_FIELD_MAP_RANGE_TO_MIN;
                if (socket_id == "To Max") return BB_SHADER_INPUT_FIELD_MAP_RANGE_TO_MAX;
                if (socket_id == "Steps") return BB_SHADER_INPUT_FIELD_MAP_RANGE_STEPS;
                if (socket_id == "Vector") return BB_SHADER_INPUT_FIELD_MAP_RANGE_VECTOR;
                if (socket_id == "From_Min_FLOAT3") return BB_SHADER_INPUT_FIELD_MAP_RANGE_FROM_MIN_FLOAT3;
                if (socket_id == "From_Max_FLOAT3") return BB_SHADER_INPUT_FIELD_MAP_RANGE_FROM_MAX_FLOAT3;
                if (socket_id == "To_Min_FLOAT3") return BB_SHADER_INPUT_FIELD_MAP_RANGE_TO_MIN_FLOAT3;
                if (socket_id == "To_Max_FLOAT3") return BB_SHADER_INPUT_FIELD_MAP_RANGE_TO_MAX_FLOAT3;
                if (socket_id == "Steps_FLOAT3") return BB_SHADER_INPUT_FIELD_MAP_RANGE_STEPS_FLOAT3;
            }
            // Mapping
            if (node_type == BB_SHADER_NODE_TYPE_MAPPING)
            {
                if (socket_id == "Vector") return BB_SHADER_INPUT_FIELD_MAPPING_VECTOR;
                if (socket_id == "Location") return BB_SHADER_INPUT_FIELD_MAPPING_LOCATION;
                if (socket_id == "Rotation") return BB_SHADER_INPUT_FIELD_MAPPING_ROTATION;
                if (socket_id == "Scale") return BB_SHADER_INPUT_FIELD_MAPPING_SCALE;
            }
            // Normal Map
            if (node_type == BB_SHADER_NODE_TYPE_NORMAL_MAP)
            {
                if (socket_id == "Strength") return BB_SHADER_INPUT_FIELD_NORMAL_MAP_STRENGTH;
                if (socket_id == "Color") return BB_SHADER_INPUT_FIELD_NORMAL_MAP_COLOR;
            }
            // Tex Brick
            if (node_type == BB_SHADER_NODE_TYPE_TEX_BRICK)
            {
                if (socket_id == "Vector") return BB_SHADER_INPUT_FIELD_TEX_BRICK_VECTOR;
                if (socket_id == "Color1") return BB_SHADER_INPUT_FIELD_TEX_BRICK_COLOR1;
                if (socket_id == "Color2") return BB_SHADER_INPUT_FIELD_TEX_BRICK_COLOR2;
                if (socket_id == "Mortar") return BB_SHADER_INPUT_FIELD_TEX_BRICK_MORTAR;
                if (socket_id == "Scale") return BB_SHADER_INPUT_FIELD_TEX_BRICK_SCALE;
                if (socket_id == "Mortar Size") return BB_SHADER_INPUT_FIELD_TEX_BRICK_MORTAR_SIZE;
                if (socket_id == "Mortar Smooth") return BB_SHADER_INPUT_FIELD_TEX_BRICK_MORTAR_SMOOTH;
                if (socket_id == "Bias") return BB_SHADER_INPUT_FIELD_TEX_BRICK_BIAS;
                if (socket_id == "Brick Width") return BB_SHADER_INPUT_FIELD_TEX_BRICK_BRICK_WIDTH;
                if (socket_id == "Row Height") return BB_SHADER_INPUT_FIELD_TEX_BRICK_ROW_HEIGHT;
            }
            // Gamma
            if (node_type == BB_SHADER_NODE_TYPE_GAMMA)
            {
                if (socket_id == "Color") return BB_SHADER_INPUT_FIELD_GAMMA_COLOR;
                if (socket_id == "Gamma") return BB_SHADER_INPUT_FIELD_GAMMA_GAMMA;
            }
            // Bright Contrast
            if (node_type == BB_SHADER_NODE_TYPE_BRIGHT_CONTRAST)
            {
                if (socket_id == "Color") return BB_SHADER_INPUT_FIELD_BRIGHT_CONTRAST_COLOR;
                if (socket_id == "Bright") return BB_SHADER_INPUT_FIELD_BRIGHT_CONTRAST_BRIGHT;
                if (socket_id == "Contrast") return BB_SHADER_INPUT_FIELD_BRIGHT_CONTRAST_CONTRAST;
            }
            // RGB to BW
            if (node_type == BB_SHADER_NODE_TYPE_RGB_TO_BW)
            {
                if (socket_id == "Color") return BB_SHADER_INPUT_FIELD_RGB_TO_BW_COLOR;
            }
            // Hue Saturation
            if (node_type == BB_SHADER_NODE_TYPE_HUE_SATURATION)
            {
                if (socket_id == "Hue") return BB_SHADER_INPUT_FIELD_HUE_SATURATION_HUE;
                if (socket_id == "Saturation") return BB_SHADER_INPUT_FIELD_HUE_SATURATION_SATURATION;
                if (socket_id == "Value") return BB_SHADER_INPUT_FIELD_HUE_SATURATION_VALUE;
                if (socket_id == "Fac") return BB_SHADER_INPUT_FIELD_HUE_SATURATION_FAC;
                if (socket_id == "Color") return BB_SHADER_INPUT_FIELD_HUE_SATURATION_COLOR;
            }
            // Invert
            if (node_type == BB_SHADER_NODE_TYPE_INVERT)
            {
                if (socket_id == "Fac") return BB_SHADER_INPUT_FIELD_INVERT_FAC;
                if (socket_id == "Color") return BB_SHADER_INPUT_FIELD_INVERT_COLOR;
            }
            // Fresnel
            if (node_type == BB_SHADER_NODE_TYPE_FRESNEL)
            {
                if (socket_id == "IOR") return BB_SHADER_INPUT_FIELD_FRESNEL_IOR;
                if (socket_id == "Normal") return BB_SHADER_INPUT_FIELD_FRESNEL_NORMAL;
            }
            // Value
            if (node_type == BB_SHADER_NODE_TYPE_VALUE)
            {
                if (socket_id == "Value") return BB_SHADER_INPUT_FIELD_VALUE_VALUE;
            }
            // RGB
            if (node_type == BB_SHADER_NODE_TYPE_RGB)
            {
                if (socket_id == "Color") return BB_SHADER_INPUT_FIELD_RGB_COLOR;
            }
            return std::nullopt;
        };

        // Map socket identifier to output field type
        auto get_output_field = [](bb_shader_node_type_e node_type, const std::string& socket_id) -> std::optional<bb_shader_output_field_type_e>
        {
            // Input
            if (node_type == BB_SHADER_NODE_TYPE_INPUT)
            {
                if (socket_id == "Object") return BB_SHADER_OUTPUT_FIELD_INPUT_OBJECT;
                if (socket_id == "Generated") return BB_SHADER_OUTPUT_FIELD_INPUT_GENERATED;
                if (socket_id == "UV") return BB_SHADER_OUTPUT_FIELD_INPUT_UV0;
                if (socket_id == "Position") return BB_SHADER_OUTPUT_FIELD_INPUT_POSITION;
                if (socket_id == "Normal") return BB_SHADER_OUTPUT_FIELD_INPUT_NORMAL;
                if (socket_id == "Tangent") return BB_SHADER_OUTPUT_FIELD_INPUT_TANGENT;
                if (socket_id == "True Normal") return BB_SHADER_OUTPUT_FIELD_INPUT_GEO_NORMAL;
                if (socket_id == "Location") return BB_SHADER_OUTPUT_FIELD_INPUT_LOCATION;
            }
            // UV Map
            if (node_type == BB_SHADER_NODE_TYPE_UV_MAP)
            {
                if (socket_id == "UV") return BB_SHADER_OUTPUT_FIELD_UV_MAP_UV;
            }
            // Math
            if (node_type == BB_SHADER_NODE_TYPE_MATH)
            {
                if (socket_id == "Value") return BB_SHADER_OUTPUT_FIELD_MATH_VALUE;
            }
            // Vector Math
            if (node_type == BB_SHADER_NODE_TYPE_VECTOR_MATH)
            {
                if (socket_id == "Vector") return BB_SHADER_OUTPUT_FIELD_VECTOR_MATH_VECTOR;
                if (socket_id == "Value") return BB_SHADER_OUTPUT_FIELD_VECTOR_MATH_VALUE;
            }
            // Mix
            if (node_type == BB_SHADER_NODE_TYPE_MIX)
            {
                if (socket_id == "Result_Float") return BB_SHADER_OUTPUT_FIELD_MIX_RESULT_FLOAT;
                if (socket_id == "Result_Vector") return BB_SHADER_OUTPUT_FIELD_MIX_RESULT_VECTOR;
                if (socket_id == "Result_Color") return BB_SHADER_OUTPUT_FIELD_MIX_RESULT_COLOR;
                if (socket_id == "Result_Rotation") return BB_SHADER_OUTPUT_FIELD_MIX_RESULT_ROTATION;
            }
            // Separate Color
            if (node_type == BB_SHADER_NODE_TYPE_SEPARATE_COLOR)
            {
                if (socket_id == "Red") return BB_SHADER_OUTPUT_FIELD_SEPARATE_COLOR_RED;
                if (socket_id == "Green") return BB_SHADER_OUTPUT_FIELD_SEPARATE_COLOR_GREEN;
                if (socket_id == "Blue") return BB_SHADER_OUTPUT_FIELD_SEPARATE_COLOR_BLUE;
            }
            // Combine Color
            if (node_type == BB_SHADER_NODE_TYPE_COMBINE_COLOR)
            {
                if (socket_id == "Color") return BB_SHADER_OUTPUT_FIELD_COMBINE_COLOR_COLOR;
            }
            // Separate/Combine XYZ
            if (node_type == BB_SHADER_NODE_TYPE_SEPARATE_XYZ)
            {
                if (socket_id == "X") return BB_SHADER_OUTPUT_FIELD_SEPARATE_XYZ_X;
                if (socket_id == "Y") return BB_SHADER_OUTPUT_FIELD_SEPARATE_XYZ_Y;
                if (socket_id == "Z") return BB_SHADER_OUTPUT_FIELD_SEPARATE_XYZ_Z;
            }
            if (node_type == BB_SHADER_NODE_TYPE_COMBINE_XYZ)
            {
                if (socket_id == "Vector") return BB_SHADER_OUTPUT_FIELD_COMBINE_XYZ_VECTOR;
            }
            // Tex Noise
            if (node_type == BB_SHADER_NODE_TYPE_TEX_NOISE)
            {
                if (socket_id == "Fac") return BB_SHADER_OUTPUT_FIELD_TEX_NOISE_FAC;
                if (socket_id == "Color") return BB_SHADER_OUTPUT_FIELD_TEX_NOISE_COLOR;
            }
            // Tex Image
            if (node_type == BB_SHADER_NODE_TYPE_TEX_IMAGE)
            {
                if (socket_id == "Color") return BB_SHADER_OUTPUT_FIELD_TEX_IMAGE_COLOR;
                if (socket_id == "Alpha") return BB_SHADER_OUTPUT_FIELD_TEX_IMAGE_ALPHA;
            }
            // Val To RGB
            if (node_type == BB_SHADER_NODE_TYPE_VAL_TO_RGB)
            {
                if (socket_id == "Color") return BB_SHADER_OUTPUT_FIELD_VAL_TO_RGB_COLOR;
                if (socket_id == "Alpha") return BB_SHADER_OUTPUT_FIELD_VAL_TO_RGB_ALPHA;
            }
            // Ambient Occlusion
            if (node_type == BB_SHADER_NODE_TYPE_AMBIENT_OCCLUSION)
            {
                if (socket_id == "Color") return BB_SHADER_OUTPUT_FIELD_AMBIENT_OCCLUSION_COLOR;
                if (socket_id == "AO") return BB_SHADER_OUTPUT_FIELD_AMBIENT_OCCLUSION_AO;
            }
            // Bump
            if (node_type == BB_SHADER_NODE_TYPE_BUMP)
            {
                if (socket_id == "Normal") return BB_SHADER_OUTPUT_FIELD_BUMP_NORMAL;
            }
            // Clamp
            if (node_type == BB_SHADER_NODE_TYPE_CLAMP)
            {
                if (socket_id == "Result") return BB_SHADER_OUTPUT_FIELD_CLAMP_RESULT;
            }
            // Displacement
            if (node_type == BB_SHADER_NODE_TYPE_DISPLACEMENT)
            {
                if (socket_id == "Displacement") return BB_SHADER_OUTPUT_FIELD_DISPLACEMENT_DISPLACEMENT;
            }
            // Map Range
            if (node_type == BB_SHADER_NODE_TYPE_MAP_RANGE)
            {
                if (socket_id == "Result") return BB_SHADER_OUTPUT_FIELD_MAP_RANGE_RESULT;
                if (socket_id == "Vector") return BB_SHADER_OUTPUT_FIELD_MAP_RANGE_VECTOR;
            }
            // Mapping
            if (node_type == BB_SHADER_NODE_TYPE_MAPPING)
            {
                if (socket_id == "Vector") return BB_SHADER_OUTPUT_FIELD_MAPPING_VECTOR;
            }
            // Normal Map
            if (node_type == BB_SHADER_NODE_TYPE_NORMAL_MAP)
            {
                if (socket_id == "Normal") return BB_SHADER_OUTPUT_FIELD_NORMAL_MAP_NORMAL;
            }
            // Tangent
            if (node_type == BB_SHADER_NODE_TYPE_TANGENT)
            {
                if (socket_id == "Tangent") return BB_SHADER_OUTPUT_FIELD_TANGENT_TANGENT;
            }
            // Tex Brick
            if (node_type == BB_SHADER_NODE_TYPE_TEX_BRICK)
            {
                if (socket_id == "Color") return BB_SHADER_OUTPUT_FIELD_TEX_BRICK_COLOR;
                if (socket_id == "Fac") return BB_SHADER_OUTPUT_FIELD_TEX_BRICK_FAC;
            }
            // Gamma
            if (node_type == BB_SHADER_NODE_TYPE_GAMMA)
            {
                if (socket_id == "Color") return BB_SHADER_OUTPUT_FIELD_GAMMA_COLOR;
            }
            // Bright Contrast
            if (node_type == BB_SHADER_NODE_TYPE_BRIGHT_CONTRAST)
            {
                if (socket_id == "Color") return BB_SHADER_OUTPUT_FIELD_BRIGHT_CONTRAST_COLOR;
            }
            // RGB to BW
            if (node_type == BB_SHADER_NODE_TYPE_RGB_TO_BW)
            {
                if (socket_id == "Val") return BB_SHADER_OUTPUT_FIELD_RGB_TO_BW_VAL;
            }
            // Hue Saturation
            if (node_type == BB_SHADER_NODE_TYPE_HUE_SATURATION)
            {
                if (socket_id == "Color") return BB_SHADER_OUTPUT_FIELD_HUE_SATURATION_COLOR;
            }
            // Invert
            if (node_type == BB_SHADER_NODE_TYPE_INVERT)
            {
                if (socket_id == "Color") return BB_SHADER_OUTPUT_FIELD_INVERT_COLOR;
            }
            // Fresnel
            if (node_type == BB_SHADER_NODE_TYPE_FRESNEL)
            {
                if (socket_id == "Fac") return BB_SHADER_OUTPUT_FIELD_FRESNEL_FAC;
            }
            // Value
            if (node_type == BB_SHADER_NODE_TYPE_VALUE)
            {
                if (socket_id == "Value") return BB_SHADER_OUTPUT_FIELD_VALUE_VALUE;
            }
            // RGB
            if (node_type == BB_SHADER_NODE_TYPE_RGB)
            {
                if (socket_id == "Color") return BB_SHADER_OUTPUT_FIELD_RGB_COLOR;
            }
            return std::nullopt;
        };

        // Map property identifier to input field type
        auto get_prop_field = [](bb_shader_node_type_e node_type, const std::string& prop_id) -> std::optional<bb_shader_input_field_type_e>
        {
            if (node_type == BB_SHADER_NODE_TYPE_MATH)
            {
                if (prop_id == "operation") return BB_SHADER_INPUT_FIELD_MATH_OPERATION;
                if (prop_id == "use_clamp") return BB_SHADER_INPUT_FIELD_MATH_USE_CLAMP;
            }
            if (node_type == BB_SHADER_NODE_TYPE_VECTOR_MATH)
            {
                if (prop_id == "operation") return BB_SHADER_INPUT_FIELD_VECTOR_MATH_OPERATION;
            }
            if (node_type == BB_SHADER_NODE_TYPE_MIX)
            {
                if (prop_id == "blend_type") return BB_SHADER_INPUT_FIELD_MIX_BLEND_TYPE;
                if (prop_id == "clamp_factor") return BB_SHADER_INPUT_FIELD_MIX_CLAMP_FACTOR;
                if (prop_id == "clamp_result") return BB_SHADER_INPUT_FIELD_MIX_CLAMP_RESULT;
                if (prop_id == "data_type") return BB_SHADER_INPUT_FIELD_MIX_DATA_TYPE;
                if (prop_id == "factor_mode") return BB_SHADER_INPUT_FIELD_MIX_FACTOR_MODE;
            }
            if (node_type == BB_SHADER_NODE_TYPE_SEPARATE_COLOR)
            {
                if (prop_id == "mode") return BB_SHADER_INPUT_FIELD_SEPARATE_COLOR_MODE;
            }
            if (node_type == BB_SHADER_NODE_TYPE_COMBINE_COLOR)
            {
                if (prop_id == "mode") return BB_SHADER_INPUT_FIELD_COMBINE_COLOR_MODE;
            }
            if (node_type == BB_SHADER_NODE_TYPE_TEX_NOISE)
            {
                if (prop_id == "noise_dimensions") return BB_SHADER_INPUT_FIELD_TEX_NOISE_NOISE_DIMENSIONS;
                if (prop_id == "noise_type") return BB_SHADER_INPUT_FIELD_TEX_NOISE_NOISE_TYPE;
                if (prop_id == "normalize") return BB_SHADER_INPUT_FIELD_TEX_NOISE_NORMALIZE;
            }
            if (node_type == BB_SHADER_NODE_TYPE_TEX_IMAGE)
            {
                if (prop_id == "interpolation") return BB_SHADER_INPUT_FIELD_TEX_IMAGE_INTERPOLATION;
                if (prop_id == "projection") return BB_SHADER_INPUT_FIELD_TEX_IMAGE_PROJECTION;
                if (prop_id == "extension") return BB_SHADER_INPUT_FIELD_TEX_IMAGE_EXTENSION;
                if (prop_id == "image") return BB_SHADER_INPUT_FIELD_TEX_IMAGE_IMAGE;
            }
            if (node_type == BB_SHADER_NODE_TYPE_VAL_TO_RGB)
            {
                if (prop_id == "interpolation") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_INTERPOLATION;
                if (prop_id == "element_count") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_ELEMENT_COUNT;
                if (prop_id == "position_0") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_0;
                if (prop_id == "position_1") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_1;
                if (prop_id == "position_2") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_2;
                if (prop_id == "position_3") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_3;
                if (prop_id == "position_4") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_4;
                if (prop_id == "position_5") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_5;
                if (prop_id == "position_6") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_6;
                if (prop_id == "position_7") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_POSITION_7;
                if (prop_id == "color_0") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_0;
                if (prop_id == "color_1") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_1;
                if (prop_id == "color_2") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_2;
                if (prop_id == "color_3") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_3;
                if (prop_id == "color_4") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_4;
                if (prop_id == "color_5") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_5;
                if (prop_id == "color_6") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_6;
                if (prop_id == "color_7") return BB_SHADER_INPUT_FIELD_VAL_TO_RGB_COLOR_7;
            }
            if (node_type == BB_SHADER_NODE_TYPE_AMBIENT_OCCLUSION)
            {
                if (prop_id == "samples") return BB_SHADER_INPUT_FIELD_AMBIENT_OCCLUSION_SAMPLES;
                if (prop_id == "inside") return BB_SHADER_INPUT_FIELD_AMBIENT_OCCLUSION_INSIDE;
                if (prop_id == "only_local") return BB_SHADER_INPUT_FIELD_AMBIENT_OCCLUSION_ONLY_LOCAL;
            }
            if (node_type == BB_SHADER_NODE_TYPE_BUMP)
            {
                if (prop_id == "invert") return BB_SHADER_INPUT_FIELD_BUMP_INVERT;
            }
            if (node_type == BB_SHADER_NODE_TYPE_CLAMP)
            {
                if (prop_id == "clamp_type") return BB_SHADER_INPUT_FIELD_CLAMP_CLAMP_TYPE;
            }
            if (node_type == BB_SHADER_NODE_TYPE_DISPLACEMENT)
            {
                if (prop_id == "space") return BB_SHADER_INPUT_FIELD_DISPLACEMENT_SPACE;
            }
            if (node_type == BB_SHADER_NODE_TYPE_MAP_RANGE)
            {
                if (prop_id == "data_type") return BB_SHADER_INPUT_FIELD_MAP_RANGE_DATA_TYPE;
                if (prop_id == "interpolation_type") return BB_SHADER_INPUT_FIELD_MAP_RANGE_INTERPOLATION_TYPE;
                if (prop_id == "clamp") return BB_SHADER_INPUT_FIELD_MAP_RANGE_CLAMP;
            }
            if (node_type == BB_SHADER_NODE_TYPE_MAPPING)
            {
                if (prop_id == "vector_type") return BB_SHADER_INPUT_FIELD_MAPPING_VECTOR_TYPE;
            }
            if (node_type == BB_SHADER_NODE_TYPE_NORMAL_MAP)
            {
                if (prop_id == "space") return BB_SHADER_INPUT_FIELD_NORMAL_MAP_SPACE;
                if (prop_id == "uv_map") return BB_SHADER_INPUT_FIELD_NORMAL_MAP_UV_MAP;
            }
            if (node_type == BB_SHADER_NODE_TYPE_TANGENT)
            {
                if (prop_id == "direction_type") return BB_SHADER_INPUT_FIELD_TANGENT_DIRECTION_TYPE;
                if (prop_id == "axis") return BB_SHADER_INPUT_FIELD_TANGENT_AXIS;
                if (prop_id == "uv_map") return BB_SHADER_INPUT_FIELD_TANGENT_UV_MAP;
            }
            if (node_type == BB_SHADER_NODE_TYPE_TEX_BRICK)
            {
                if (prop_id == "offset") return BB_SHADER_INPUT_FIELD_TEX_BRICK_OFFSET;
                if (prop_id == "offset_frequency") return BB_SHADER_INPUT_FIELD_TEX_BRICK_OFFSET_FREQUENCY;
                if (prop_id == "squash") return BB_SHADER_INPUT_FIELD_TEX_BRICK_SQUASH;
                if (prop_id == "squash_frequency") return BB_SHADER_INPUT_FIELD_TEX_BRICK_SQUASH_FREQUENCY;
            }
            return std::nullopt;
        };

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
                        field.vec3_val[0] = v.x;
                        field.vec3_val[1] = v.y;
                        field.vec3_val[2] = v.z;
                    }
                    else if constexpr (std::is_same_v<T, socket_value_rgba_t>)
                    {
                        field.value_type  = BB_SHADER_VALUE_TYPE_FLOAT4;
                        field.vec4_val[0] = v.r;
                        field.vec4_val[1] = v.g;
                        field.vec4_val[2] = v.b;
                        field.vec4_val[3] = v.a;
                    }
                    else if constexpr (std::is_same_v<T, socket_value_string_t>)
                    {
                        field.value_type = BB_SHADER_VALUE_TYPE_TEX;
                        field.str_val    = normalize_string(v.value);
                    }
                },
                val
            );

            return field;
        };

        bb_shaders.reserve(data.materials.size());

        for (const auto& [mat_name, mat] : data.materials)
        {
            // Build node name to index map
            std::unordered_map<std::string, uint32_t> node_name_to_idx;
            std::vector<std::pair<const shader_node_t*, bb_shader_node_type_e>> valid_nodes;

            for (const auto& node : mat.nodes)
            {
                auto node_type_opt = get_node_type(node.idname);
                if (!node_type_opt.has_value())
                {
                    fprintf(stderr, "[zachary] FATAL: Unknown node type '%s' in material '%s'\n", node.idname.c_str(), mat.name.c_str());
                    abort();
                }
                node_name_to_idx[node.name] = valid_nodes.size();
                valid_nodes.push_back({&node, node_type_opt.value()});
            }

            if (valid_nodes.empty())
            {
                fprintf(stderr, "[zachary] FATAL: No valid nodes in material '%s'\n", mat.name.c_str());
                abort();
            }

            // Build nodes
            BBArchiveShaderNode* bb_nodes = (BBArchiveShaderNode*)arena_alloc_z(arena, sizeof(BBArchiveShaderNode) * valid_nodes.size())->data;

            for (size_t i = 0; i < valid_nodes.size(); i++)
            {
                const shader_node_t* node    = valid_nodes[i].first;
                bb_shader_node_type_e n_type = valid_nodes[i].second;

                // Count fields (inputs with default values + props)
                std::vector<BBArchiveShaderField> fields;

                // Add input socket default values
                for (const auto& input : node->inputs)
                {
                    if (!input.default_value.has_value()) continue;
                    auto field_opt = get_input_field(n_type, input.identifier);
                    if (!field_opt.has_value())
                    {
                        fprintf(stderr, "[zachary] FATAL: Unknown input field '%s' for node type %d\n", input.identifier.c_str(), (int)n_type);
                        abort();
                    }
                    fields.push_back(make_field(field_opt.value(), input.default_value.value()));
                }

                // Add props
                for (const auto& prop : node->props)
                {
                    auto field_opt = get_prop_field(n_type, prop.identifier);
                    if (!field_opt.has_value())
                    {
                        fprintf(stderr, "[zachary] FATAL: Unknown prop field '%s' for node type %d\n", prop.identifier.c_str(), (int)n_type);
                        abort();
                    }
                    fields.push_back(make_field(field_opt.value(), prop.value));
                }

                // Allocate and copy fields
                BBArchiveShaderField* bb_fields = nullptr;
                if (!fields.empty())
                {
                    bb_fields = (BBArchiveShaderField*)arena_alloc_z(arena, sizeof(BBArchiveShaderField) * fields.size())->data;
                    memcpy(bb_fields, fields.data(), sizeof(BBArchiveShaderField) * fields.size());
                }

                bb_nodes[i].node_type   = n_type;
                bb_nodes[i].field_count = fields.size();
                bb_nodes[i].fields      = bb_fields;
            }

            // Build links
            std::vector<BBArchiveShaderLink> links;
            for (const auto& link : mat.links)
            {
                auto src_it = node_name_to_idx.find(link.from_node);
                auto dst_it = node_name_to_idx.find(link.to_node);
                if (src_it == node_name_to_idx.end())
                {
                    fprintf(stderr, "[zachary] FATAL: Link source node '%s' not found in material '%s'\n", link.from_node.c_str(), mat.name.c_str());
                    abort();
                }
                if (dst_it == node_name_to_idx.end())
                {
                    fprintf(stderr, "[zachary] FATAL: Link destination node '%s' not found in material '%s'\n", link.to_node.c_str(), mat.name.c_str());
                    abort();
                }

                uint32_t src_idx               = src_it->second;
                uint32_t dst_idx               = dst_it->second;
                bb_shader_node_type_e src_type = valid_nodes[src_idx].second;
                bb_shader_node_type_e dst_type = valid_nodes[dst_idx].second;

                auto from_field_opt            = get_output_field(src_type, link.from_socket);
                auto to_field_opt              = get_input_field(dst_type, link.to_socket);
                if (!from_field_opt.has_value())
                {
                    fprintf(stderr, "[zachary] FATAL: Unknown output field '%s' for node type %d in material '%s'\n", link.from_socket.c_str(), (int)src_type, mat.name.c_str());
                    abort();
                }
                if (!to_field_opt.has_value())
                {
                    fprintf(stderr, "[zachary] FATAL: Unknown input field '%s' for node type %d in material '%s'\n", link.to_socket.c_str(), (int)dst_type, mat.name.c_str());
                    abort();
                }

                BBArchiveShaderLink bb_link;
                bb_link.src_idx    = src_idx;
                bb_link.dst_idx    = dst_idx;
                bb_link.from_field = from_field_opt.value();
                bb_link.to_field   = to_field_opt.value();
                links.push_back(bb_link);
            }

            // Allocate and copy links
            BBArchiveShaderLink* bb_links = nullptr;
            if (!links.empty())
            {
                bb_links = (BBArchiveShaderLink*)arena_alloc_z(arena, sizeof(BBArchiveShaderLink) * links.size())->data;
                memcpy(bb_links, links.data(), sizeof(BBArchiveShaderLink) * links.size());
            }

            BBArchiveShaderGraph shader_graph;
            shader_graph.shader_name    = normalize_string(mat.name);
            shader_graph.node_count     = valid_nodes.size();
            shader_graph.nodes          = bb_nodes;
            shader_graph.link_count     = links.size();
            shader_graph.links          = bb_links;
            shader_graph.mat_data_count = 0;
            shader_graph.mat_data       = nullptr;

            bb_shaders.push_back(shader_graph);
        }

        fprintf(log_file, "[zachary] Exported %zu shader graphs\n", bb_shaders.size());
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
        info.skeleton_count     = 0;
        info.animation_count    = 0;
        info.shader_graph_count = bb_shaders.size();
        info.scene_count        = bb_scenes.size();
        info.meshes             = meshes;
        info.images             = bb_images.data();
        info.skeletons          = nullptr;
        info.animations         = nullptr;
        info.shader_graphs      = bb_shaders.data();
        info.scenes             = bb_scenes.data();

        bb_archive_write(&info, bb_archive_output_dir);
        fprintf(log_file, "[zachary] bb_archive_write completed\n");
    }

    fclose(log_file);
    arena_destroy_z(arena);
}
