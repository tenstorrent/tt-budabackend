// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <exception>
#include <optional>

#include "device/tt_xy_pair.h"
#include "pipegen2_utils.h"

namespace pipegen2
{

// Base class for all pipegen2 exception types to which client can react. Holds informations necessary to populate
// tt_compile_result object, which is returned to the client. It is guaranteed that the exception objects will hold at
// least one core location - either only physical, only logical or both.
class BasePipegen2CompileException : public std::runtime_error
{
public:
    virtual std::string to_string() const { return std::string(what()); }

    const std::optional<tt_cxy_pair>& get_logical_core_location() const { return m_logical_core_location; }

    const std::optional<tt_cxy_pair>& get_physical_core_location() const { return m_physical_core_location; }

protected:
    // Constructor - initializes member variables, asserts if neither logical nor physical location have value stored.
    BasePipegen2CompileException(const std::string& error_message,
                                 const std::optional<tt_cxy_pair>& logical_location,
                                 const std::optional<tt_cxy_pair>& physical_location) :
        std::runtime_error(error_message),
        m_logical_core_location(logical_location),
        m_physical_core_location(physical_location)
    {
        log_assert(logical_location.has_value() || physical_location.has_value(),
                   "Expecting either logical or physical core location to be provided when creating exception.");
    }

private:
    // Optionally holds core location where the error occurred, in logical coordinates.
    std::optional<tt_cxy_pair> m_logical_core_location;

    // Optionally holds core location where the error occurred, in physical coordinates.
    std::optional<tt_cxy_pair> m_physical_core_location;
};

// Base class for all pipegen2 IO related exceptions - missing input files, wrong format of input files...
class BasePipegen2IOException : public std::runtime_error
{
public:
    virtual std::string to_string() const { return std::string(what()); }

protected:
    BasePipegen2IOException(const std::string& error_message) :
        std::runtime_error(error_message)
    {
    }
};

// Exception type used for illegal core resource allocation errors.
class IllegalCoreResourceAllocationException : public BasePipegen2CompileException
{
public:
    enum class CoreResourceType
    {
        kKernelInputIndex = 0,
        kKernelOutputIndex = 1,
        kL1DataBuffersMemory = 2,
        kGeneralPurposeStreams = 3,
        kUnpackerStreams = 4,
        kGatherMulticastStreams = 5,
        kEthernetStreams = 6,
        kPackerMulticastStreams = 7,
        kPackerStreams = 8,
        kIntermediateStreams = 9,
        kNcriscReaderStreams = 10,
        kNcriscWriterStreams = 11
    };

    IllegalCoreResourceAllocationException(const std::string& error_message,
                                           const tt_cxy_pair& physical_location,
                                           CoreResourceType core_resource_type) :
        BasePipegen2CompileException(error_message,
                                     std::nullopt /* logical_location */,
                                     std::make_optional(physical_location)),
        m_core_resource_type(core_resource_type)
    {
    }

    CoreResourceType get_core_resource_type() const { return m_core_resource_type; }

private:
    // Which core resource user tried to allocate and failed.
    CoreResourceType m_core_resource_type;
};

// Exception type used for the case of running out of available resources on a single core.
class OutOfCoreResourcesException : public IllegalCoreResourceAllocationException
{
public:
    OutOfCoreResourcesException(const std::string& error_message,
                                const tt_cxy_pair& physical_location,
                                CoreResourceType core_resource_type,
                                unsigned int avaiable_core_resources,
                                unsigned int used_core_resources) :
        IllegalCoreResourceAllocationException(error_message, physical_location, core_resource_type),
        m_total_core_resources_available(avaiable_core_resources),
        m_core_resources_used(used_core_resources)
    {
    }

    OutOfCoreResourcesException(const std::string& error_message,
                                const tt_cxy_pair& physical_location,
                                CoreResourceType core_resource_type,
                                unsigned int avaiable_core_resources) :
        OutOfCoreResourcesException(error_message,
                                    physical_location,
                                    core_resource_type,
                                    avaiable_core_resources,
                                    avaiable_core_resources + 1 /* used_core_resources */)
    {
    }

    unsigned int get_total_core_resources_available() const { return m_total_core_resources_available; }

    unsigned int get_core_resources_used() const { return m_core_resources_used; }

private:
    // Holds information about the total number of available resources users has tried to allocate on a specific core.
    unsigned int m_total_core_resources_available;

    // Holds information how many resources were actually used on a specific core.
    unsigned int m_core_resources_used;
};

// Exception type used for errors due to invalid pipe graph specification.
class InvalidPipeGraphSpecificationException : public BasePipegen2CompileException
{
public:
    InvalidPipeGraphSpecificationException(const std::string& error_message,
                                           const std::optional<tt_cxy_pair>& logical_location,
                                           const std::optional<tt_cxy_pair>& physical_location) :
        BasePipegen2CompileException(error_message, logical_location, physical_location)
    {
    }

    InvalidPipeGraphSpecificationException(const std::string& error_message,
                                           const tt_cxy_pair& logical_location,
                                           const std::optional<tt_cxy_pair>& physical_location) :
        InvalidPipeGraphSpecificationException(error_message, std::make_optional(logical_location), physical_location)
    {
    }

    InvalidPipeGraphSpecificationException(const std::string& error_message,
                                           const tt_cxy_pair& logical_location) :
        InvalidPipeGraphSpecificationException(error_message,
                                               std::make_optional(logical_location),
                                               std::nullopt /* physical_location */)
    {
    }

    InvalidPipeGraphSpecificationException(const std::string& error_message,
                                           const tt_cxy_pair& logical_location,
                                           const tt_cxy_pair& physical_location) :
        InvalidPipeGraphSpecificationException(error_message,
                                               std::make_optional(logical_location),
                                               std::make_optional(physical_location))
    {
    }
};

// Exception type used for pipegen yaml parsing exceptions.
class InvalidPipegenYamlFormatException : public BasePipegen2IOException
{
public:
    InvalidPipegenYamlFormatException(const std::string& error_message) :
        BasePipegen2IOException(error_message)
    {
    }
};

// Exception type used for SoC info yaml parsing exceptions.
class InvalidSoCInfoYamlException : public BasePipegen2IOException
{
public:
    InvalidSoCInfoYamlException(const std::string& error_message) :
        BasePipegen2IOException(error_message)
    {
    }
};


// Exception type used when there are no physical cores to be mapped to the logical core.
class NoPhysicalCoreException : public BasePipegen2CompileException
{
public:
    NoPhysicalCoreException(const std::string& error_message, const tt_cxy_pair& logical_location) :
        BasePipegen2CompileException(error_message, std::make_optional(logical_location), std::nullopt)
    {
    }
};

} // namespace pipegen2