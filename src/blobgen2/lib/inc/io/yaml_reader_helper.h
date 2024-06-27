// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include "yaml-cpp/yaml.h"

namespace blobgen2
{

// Helper class for reading raw token values from YAML file.
class YamlReaderHelper
{
public:
    // Used for error messages.
    static std::string get_node_type_string(const YAML::NodeType::value node_type)
    {
        switch (node_type)
        {
            case YAML::NodeType::Undefined:
                return "undefined";
            case YAML::NodeType::Null:
                return "null";
            case YAML::NodeType::Scalar:
                return "scalar";
            case YAML::NodeType::Sequence:
                return "sequence";
            case YAML::NodeType::Map:
                return "map";
            default:
                return "unknown";
        }
    }

    template <typename T>
    static T read_property(const YAML::Node sequence, const std::string key)
    {
        std::optional<T> optional_property = read_optional_property<T>(sequence, key);
        if (!optional_property.has_value())
        {
            throw std::runtime_error("Property '" + key + "' must be defined.");
        }
        return optional_property.value();
    }

    template <typename T>
    static std::vector<T> read_vector_property(const YAML::Node sequence, const std::string key)
    {
        std::optional<std::vector<T>> optional_vector = read_optional_vector_property<T>(sequence, key);
        if (!optional_vector.has_value())
        {
            throw std::runtime_error("Property '" + key + "' must be defined.");
        }
        return optional_vector.value();
    }

    template <typename T>
    static std::optional<T> read_optional_property(const YAML::Node sequence, const std::string key)
    {
        const YAML::Node node = sequence[key];
        if (node.IsDefined())
        {
            if (node.IsScalar())
            {
                return std::optional{node.as<T>()};
            }
            else
            {
                throw std::runtime_error(
                    "Property '" + key + "' must be a " + get_node_type_string(YAML::NodeType::Scalar) +
                    ", whereas its type is: '" + get_node_type_string(node.Type()) + "' and its value is: '" +
                    node.as<std::string>() + "'.");
            }
        }

        return {};
    }

    template <typename T>
    static std::optional<std::vector<T>> read_optional_vector_property(const YAML::Node sequence, const std::string key)
    {
        const YAML::Node node = sequence[key];
        if (node.IsDefined())
        {
            if (node.IsSequence())
            {
                std::vector<T> vec;
                for (YAML::const_iterator it = node.begin(); it != node.end(); ++it)
                {
                    vec.push_back((*it).as<T>());
                }
                return std::move(std::optional{vec});
            }
            else
            {
                throw std::runtime_error(
                    "Property '" + key + "' must be a " + get_node_type_string(YAML::NodeType::Sequence) +
                    ", whereas its type is: '" + get_node_type_string(node.Type()) + "' and its value is: '" +
                    node.as<std::string>() + "'.");
            }
        }

        return {};
    }
};

}  // namespace blobgen2