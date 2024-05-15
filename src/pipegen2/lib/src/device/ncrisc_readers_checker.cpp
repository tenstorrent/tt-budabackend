#include "device/ncrisc_readers_checker.h"

#include "device/core_resources_constants.h"
#include "model/rational_graph/pipes/ncrisc_reader_pipe_interface.h"
#include "model/rational_graph/pipes/base_rg_pipe.h"
#include "pipegen2_exceptions.h"

namespace pipegen2
{

void NcriscReadersChecker::check(const RationalGraph* rational_graph)
{
    std::unordered_map<tt_cxy_pair, unsigned int> num_ncrisc_reading_streams_per_core;

    for (const std::unique_ptr<RGBasePipe>& pipe : rational_graph->get_pipes())
    {
        count_stream_usage(pipe.get(), num_ncrisc_reading_streams_per_core);
    }

    for (const auto& [stream_location, streams_used] : num_ncrisc_reading_streams_per_core)
    {
        if (streams_used > core_resources_constants::max_num_ncrisc_reading_streams)
        {
            throw OutOfCoreResourcesException(
                "Core " + stream_location.str() + " uses too many streams reading from DRAM or PCIe (" +
                std::to_string(streams_used) + " out of " +
                std::to_string(core_resources_constants::max_num_ncrisc_reading_streams) + ").",
                stream_location,
                OutOfCoreResourcesException::CoreResourceType::kNcriscReaderStreams,
                core_resources_constants::max_num_ncrisc_reading_streams,
                streams_used);
        }
    }
}

void NcriscReadersChecker::count_stream_usage(
    const RGBasePipe* pipe,
    std::unordered_map<tt_cxy_pair, unsigned int>& num_ncrisc_reading_streams_per_core)
{
    const INcriscReaderPipe* ncrisc_reader_pipe = dynamic_cast<const INcriscReaderPipe*>(pipe);

    if (!ncrisc_reader_pipe)
    {
        return;
    }

    for (const tt_cxy_pair& stream_location : ncrisc_reader_pipe->get_ncrisc_reader_streams_locations())
    {
        num_ncrisc_reading_streams_per_core[stream_location]++;
    }
}

} // namespace pipegen2
