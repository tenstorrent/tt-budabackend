// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// Main command loop:   command_server_loop
// Commands (requests): REQ_TYPE
// Use LOGGER_LEVEL=Trace environment variable to see the detailed logs
#include "debuda_server.hpp"
#include "utils/logger.hpp"
#include "loader/tt_cluster.hpp"
#include "runtime/runtime.hpp"
#include "tt_cluster.hpp"
#include <zmq.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "model/tile.hpp"
#include <thread>
#include <stdexcept>

template<typename ... Args>
std::string string_format( const std::string& format, Args ... args )
{
    int size_s = std::snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
    if( size_s <= 0 ){ throw std::runtime_error( "Error during formatting." ); }
    auto size = static_cast<size_t>( size_s );
    std::unique_ptr<char[]> buf( new char[ size ] );
    std::snprintf( buf.get(), size, format.c_str(), args ... );
    return std::string( buf.get(), buf.get() + size - 1 ); // We don't want the '\0' inside
}


enum REQ_TYPE {
  INVALID_REQ_TYPE   = 0, // Arguments below:
  PING               = 1, // ()
  PCI_READ_XY        = 2, // (chip_id, noc_x, noc_y, addr)
  DMA_BUFF_READ      = 3, // (addr)
  PCI_WRITE_XY       = 4, // (chip_id, noc_x, noc_y, addr, data)
  PCI_READ_TILE      = 5, // (chip_id, noc_x, noc_y, addr, data) // data_format = data >> 24, msg_size = data && 0x00FFFFFF
  PCI_READ_RAW       = 6, // (chip_id, addr) - Raw read from PCI address
  PCI_WRITE_RAW      = 7, // (chip_id, addr, data) - Raw write to PCI address
  GET_RUNTIME_DATA   = 8, // Retreives runtime data as yaml text
  GET_CLUSTER_DESC   = 9, // Retreives cluster_desc as yaml text
  GET_HARVESTED_COORD_TRANSLATION = 10, // Retreives harvested coord translation
};

struct BUDA_TRANSFER_REQ {
  uint8_t  req_type;
  uint8_t  chip_id;
  uint8_t  noc_x;
  uint8_t  noc_y;
  uint8_t  noc_id;
  uint32_t addr;
  uint32_t data;
};

tt::DataFormat uint16_to_data_format(uint16_t i) {
  switch (i)
  {
    case 0:  return tt::DataFormat::Float32;
    case 1:  return tt::DataFormat::Float16;
    case 2:  return tt::DataFormat::Bfp8;
    case 3:  return tt::DataFormat::Bfp4;
    case 4:  return tt::DataFormat::Tf32;
    case 5:  return tt::DataFormat::Float16_b;
    case 6:  return tt::DataFormat::Bfp8_b;
    case 7:  return tt::DataFormat::Bfp4_b;
    case 10: return tt::DataFormat::Lf8;
    case 11: return tt::DataFormat::Bfp2;
    case 12: return tt::DataFormat::UInt16;
    case 14: return tt::DataFormat::Int8;
  }
  return tt::DataFormat::Invalid;
}

// reads data vector from device
std::vector<uint32_t> read_vector(tt_cluster* cluster, const BUDA_TRANSFER_REQ& req, uint32_t size_in_bytes) {
  std::vector<uint32_t> mem_vector;
  tt_cxy_pair target(req.chip_id, req.noc_x, req.noc_y);
  std::uint32_t address = req.addr;

  bool small_access = false;
  bool register_txn = true; // FIX: This should not be register access, actually
  cluster->read_dram_vec (mem_vector, target, address, size_in_bytes, small_access, register_txn);
  return mem_vector;
}

// Converts tile in packed format into string
std::string dump_tile(const std::vector<uint32_t>& mem_vector, tt::DataFormat df) {
  tt::tt_tile tile;
  tile.packed_data = mem_vector;
  tile.set_data_format(df);
  tile.packed_data_to_tile();
  return tile.get_string();
}

std::string get_runtime_data_yaml(tt_runtime *runtime) {
  tt_cluster *cluster = runtime->cluster.get();
  tt_cluster_description *desc = cluster->get_cluster_desc();

  std::unordered_map<chip_id_t, chip_id_t> chips_with_mmio = desc->get_chips_with_mmio();
  std::unordered_set<chip_id_t> all_chips = desc->get_all_chips();

  std::string s_all;
  for (const auto& elem: all_chips) {
     s_all += string_format("{},", elem);
  }

  std::string s_mmio;
  for (const auto& pair: chips_with_mmio) {
     s_mmio += string_format("{},", pair.first);
  }

  return string_format("all_chips: [%s]\nchips_with_mmio: [%s]\n%s", s_all.c_str(), s_mmio.c_str(), runtime->runtime_data_to_yaml().c_str());
}

std::string get_cluster_desc_path(tt_runtime *runtime) {
  tt_cluster *cluster = runtime->cluster.get();
  tt_cluster_description *desc = cluster->get_cluster_desc();
  return cluster->get_cluster_desc_path(tt::buda_home());
}

// Main server loop
int command_server_loop (tt_runtime *runtime, void * zmq_context, void * responder)
{
    tt_cluster *cluster = runtime->cluster.get();
    BUDA_TRANSFER_REQ transfer_req;
    char str_buffer[10];
    while (1) {
      int bytes = zmq_recv (responder, (char*)(&transfer_req),  sizeof (transfer_req), 0);
      log_trace(tt::LogDebuda, "Received {} bytes, sizeof (transfer_req) = {} bytes", bytes, sizeof (transfer_req));
      log_assert (bytes <= sizeof (transfer_req), "Unexpected transfer size");
      log_trace(tt::LogDebuda, "  req_type={} chip_id={} noc={}-{} noc_id={} addr=0x{:x} data=0x{:x}", transfer_req.req_type, transfer_req.chip_id, transfer_req.noc_x, 
                                  transfer_req.noc_y, transfer_req.noc_id, transfer_req.addr, transfer_req.data );

      try  {
        switch (transfer_req.req_type) {
          case PING:
            zmq_send (responder, "PONG", 5, 0);
            break;
          case PCI_READ_XY:
            {
              std::vector<std::uint32_t> mem_vector;
              tt_cxy_pair target(transfer_req.chip_id, transfer_req.noc_x, transfer_req.noc_y);
              std::uint32_t address = transfer_req.addr;
              std::uint32_t size_in_bytes = 4;

              bool small_access = false;
              bool register_txn = true;
              cluster->read_dram_vec(mem_vector, target, address, size_in_bytes, small_access, register_txn);
              log_trace(tt::LogDebuda, "PCI_READ_XY from {}-{} 0x{:x} data: 0x{:x}", transfer_req.noc_x, transfer_req.noc_y, address, mem_vector[0]);
              zmq_send (responder, (char*)&mem_vector[0], 4, 0);
            }
            break;
          case PCI_WRITE_XY:
            {
              std::vector<std::uint32_t> mem_vector;
              mem_vector.push_back (transfer_req.data);
              tt_cxy_pair target(transfer_req.chip_id, transfer_req.noc_x, transfer_req.noc_y);
              std::uint32_t address = transfer_req.addr;
              cluster->write_dram_vec (mem_vector, target, address);
              log_trace(tt::LogDebuda, "PCI_WRITE_XY to {}-{} 0x{:x} data: 0x{:x}", transfer_req.noc_x, transfer_req.noc_y, address, mem_vector[0]);
              zmq_send (responder, (char*)&mem_vector[0], 4, 0); // Send the data back
            }
            break;
          case DMA_BUFF_READ:
            {
              std::vector<std::uint32_t> mem_vector;
              tt_cxy_pair target(transfer_req.chip_id, 0 /* UNUSED */, 0 /* UNUSED */);
              std::uint32_t address = transfer_req.addr;
              std::uint16_t channel = transfer_req.data;
              std::uint32_t size_in_bytes = 4;
              cluster->read_sysmem_vec (mem_vector, address, channel, size_in_bytes, target.chip);
              log_debug(tt::LogDebuda, "DMA_BUFF_READ from 0x{:x} bytes: {} data: 0x{:x} 0x{:x} ...", address, size_in_bytes, mem_vector[0], mem_vector[1]);

              zmq_send (responder, (char*)&mem_vector[0], 4, 0);
            }
            break;
          case PCI_READ_TILE:
            {
              uint16_t udf = (transfer_req.data & 0xFFFF0000) >> 16;
              uint16_t size = (transfer_req.data & 0x0000FFFF) ;
              tt::DataFormat df = uint16_to_data_format(udf);
              if (df != tt::DataFormat::Invalid) {
                std::string tile_as_string = dump_tile(read_vector(cluster, transfer_req, size), df);
                zmq_send(responder, tile_as_string.c_str(), tile_as_string.length(), 0);
              }
              else {
                const string bad_format = "BAD_DATA_FORMAT";
                zmq_send(responder, bad_format.c_str(), bad_format.length(), 0 );
              }
            }
            break;
          case PCI_READ_RAW:
            {
              // TODO: finish this
              DebudaIFC difc(cluster);
              if (difc.is_chip_mmio_capable(transfer_req.chip_id)) {
                uint32_t val = difc.bar_read32 (transfer_req.chip_id, transfer_req.addr);
                // log_debug(tt::LogDebuda, "PCI_READ_RAW from 0x{:x} data: 0x{:x}", transfer_req.addr, val);
                zmq_send (responder, (char*)&val, 4, 0); // Send the data back
              } else {
                const string bad_format = "CHIP_HAS_NO_MMIO";
                zmq_send(responder, bad_format.c_str(), bad_format.length(), 0 );
              }
            }
            break;
          case PCI_WRITE_RAW:
            {
              // TODO: finish this
              DebudaIFC difc(cluster);
              if (difc.is_chip_mmio_capable(transfer_req.chip_id)) {
                // log_debug(tt::LogDebuda, "PCI_WRITE_RAW to 0x{:x} data: 0x{:x}", transfer_req.addr, transfer_req.data);
                difc.bar_write32 (transfer_req.chip_id, transfer_req.addr, transfer_req.data);
                zmq_send (responder, (char*)&transfer_req.data, 4, 0); // Send the data back
              } else {
                const string bad_format = "CHIP_HAS_NO_MMIO";
                zmq_send(responder, bad_format.c_str(), bad_format.length(), 0 );
              }
            }
            break;
          case GET_RUNTIME_DATA:
              {
                const string runtime_data_yaml = get_runtime_data_yaml(runtime);
                zmq_send(responder, runtime_data_yaml.c_str(), runtime_data_yaml.length(), 0 );
              }
            break;
          case GET_CLUSTER_DESC:
              {
                const string cluster_desc_path = get_cluster_desc_path(runtime);
                zmq_send(responder, cluster_desc_path.c_str(), cluster_desc_path.length(), 0 );
              }
              break;
          case GET_HARVESTED_COORD_TRANSLATION:
              {
                DebudaIFC difc(cluster);
                const string harvested_coord_translation = difc.get_harvested_coord_translation(transfer_req.chip_id);
                zmq_send(responder, harvested_coord_translation.c_str(), harvested_coord_translation.length(), 0 );
              }
              break;
          default:
            log_error ("Unhandled request type {}", (int)transfer_req.req_type);
            break;
        }
      } catch (const std::exception& e) {
        // Reply with an error
        const string exception_string = "DEBUDA-SERVER EXCEPTION: " + string(e.what()) + "\n";
        zmq_send(responder, exception_string.c_str(), exception_string.length(), 0 );
        log_error ("{}", exception_string);
      }
    }
    return 0;
}

tt_debuda_server::tt_debuda_server (tt_runtime* runtime) : m_runtime(runtime), m_connection_addr() {
  auto TT_DEBUG_SERVER_PORT_STR = std::getenv("TT_DEBUG_SERVER_PORT");
  if (TT_DEBUG_SERVER_PORT_STR != nullptr) {
    int TT_DEBUG_SERVER_PORT = atoi(TT_DEBUG_SERVER_PORT_STR);
    if (TT_DEBUG_SERVER_PORT != 0) {
      if (TT_DEBUG_SERVER_PORT > 1024 && TT_DEBUG_SERVER_PORT < 65536) {
        m_connection_addr = std::string("tcp://*:") + std::to_string(TT_DEBUG_SERVER_PORT);
        log_info(tt::LogDebuda, "Debug server starting on {}...", m_connection_addr);

        void *zmq_context = zmq_ctx_new ();
        void *responder = zmq_socket (zmq_context, ZMQ_REP);
        int rc = zmq_bind (responder, m_connection_addr.c_str());
        if (rc != 0) {
          log_info(tt::LogDebuda, "Debug server cannot start on {}. An instance of debug server might already be running.", m_connection_addr);
          return;
        } else {
          log_info(tt::LogDebuda, "Debug server started on {}.", m_connection_addr);
        }

        std::thread t1(command_server_loop, runtime, zmq_context, responder);
        t1.detach();
      } else {
        log_error("TT_DEBUG_SERVER_PORT should be between 1024 and 65535 (inclusive)");
      }
    }
  }
}

tt_debuda_server::~tt_debuda_server () {
  log_info(tt::LogDebuda, "Debug server ended on {}", m_connection_addr);
}

void tt_debuda_server::wait_terminate () {
  // iF m_connection_addr is an empty string, we did not start a server, so we do not need to wait
  if (m_connection_addr.empty()) {
    return;
  }
  log_info(tt::LogDebuda, "The debug server is running. Press ENTER to resume execution...");
  std::string user_input;
  std::cin >> user_input;
}