#include "device/ncrisc_writers_checker.h"

#include "device/core_resources_constants.h"
#include "model/rational_graph/pipes/ncrisc_writer_pipe_interface.h"
#include "model/rational_graph/pipes/base_rg_pipe.h"
#include "pipegen2_exceptions.h"

namespace pipegen2
{

void NcriscWritersChecker::check(const RationalGraph* rational_graph)
{
    std::unordered_map<tt_cxy_pair, unsigned int> num_ncrisc_writing_streams_per_core;

    for (const std::unique_ptr<RGBasePipe>& pipe : rational_graph->get_pipes())
    {
        count_stream_usage(pipe.get(), num_ncrisc_writing_streams_per_core);
    }

    for (const auto& [stream_location, streams_used] : num_ncrisc_writing_streams_per_core)
    {
        if (streams_used > core_resources_constants::max_num_ncrisc_writing_streams)
        {
            throw OutOfCoreResourcesException(
                "Core " + stream_location.str() + " uses too many streams writing to DRAM or PCIe (" +
                std::to_string(streams_used) + " out of " +
                std::to_string(core_resources_constants::max_num_ncrisc_reading_streams) + ").",
                stream_location,
                OutOfCoreResourcesException::CoreResourceType::kNcriscWriterStreams,
                core_resources_constants::max_num_ncrisc_writing_streams,
                streams_used);
        }
    }
}

void NcriscWritersChecker::count_stream_usage(
    const RGBasePipe* pipe,
    std::unordered_map<tt_cxy_pair, unsigned int>& num_ncrisc_writing_streams_per_core)
{
    const INcriscWriterPipe* ncrisc_writer_pipe = dynamic_cast<const INcriscWriterPipe*>(pipe);

    if (!ncrisc_writer_pipe)
    {
        return;
    }

    for (const tt_cxy_pair& stream_location : ncrisc_writer_pipe->get_ncrisc_writer_streams_locations())
    {
        num_ncrisc_writing_streams_per_core[stream_location]++;
    }
}

} // namespace pipegen2
