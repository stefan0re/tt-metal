// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "ttnn/cpp/ttnn/operations/ccl/kernels/edm_fabric/fabric_edm_packet_header.hpp"
#include "ttnn/cpp/ttnn/operations/ccl/common/interpreter_backends/kernel_common/fabric_connection_manager.hpp"
#include "ttnn/cpp/ttnn/operations/ccl/common/interpreter_backends/kernel_common/noc_addr.hpp"
#include "dataflow_api.h"

#include "ttnn/cpp/ttnn/operations/ccl/kernels/edm_fabric/fabric_edm_packet_transmission.hpp"

#include <cstdint>
#include <cstddef>

static constexpr bool enable_start_synchronization = get_compile_time_arg_val(0) != 0;
static constexpr bool enable_finish_synchronization = get_compile_time_arg_val(1) != 0;
static constexpr bool enable_any_synchronization = enable_start_synchronization || enable_finish_synchronization;

FORCE_INLINE void line_sync(
    FabricConnectionManager& fabric_connection,
    volatile PACKET_HEADER_TYPE* mcast_fwd_packet_header,
    volatile PACKET_HEADER_TYPE* mcast_bwd_packet_header,
    size_t sync_bank_addr,
    size_t sync_noc_x,
    size_t sync_noc_y,
    size_t sync_val) {
    using namespace tt::fabric;

    auto dest_noc_addr =
        safe_get_noc_addr(static_cast<uint8_t>(sync_noc_x), static_cast<uint8_t>(sync_noc_y), sync_bank_addr, 0);
    if (fabric_connection.has_forward_connection()) {
        mcast_fwd_packet_header->to_noc_unicast_atomic_inc(NocUnicastAtomicIncCommandHeader{dest_noc_addr, 1, 128});
        fabric_connection.get_forward_connection().wait_for_empty_write_slot();
        print_pkt_header(mcast_fwd_packet_header);
        fabric_connection.get_forward_connection().send_payload_flush_non_blocking_from_address(
            (uint32_t)mcast_fwd_packet_header, sizeof(PACKET_HEADER_TYPE));
    }

    if (fabric_connection.has_backward_connection()) {
        mcast_bwd_packet_header->to_noc_unicast_atomic_inc(NocUnicastAtomicIncCommandHeader{dest_noc_addr, 1, 128});
        fabric_connection.get_backward_connection().wait_for_empty_write_slot();
        print_pkt_header(mcast_bwd_packet_header);
        fabric_connection.get_backward_connection().send_payload_flush_non_blocking_from_address(
            (uint32_t)mcast_bwd_packet_header, sizeof(PACKET_HEADER_TYPE));
    }
    noc_semaphore_inc(get_noc_addr(sync_noc_x, sync_noc_y, sync_bank_addr), 1);
    if (sync_noc_x == my_x[0] && sync_noc_y == my_y[0]) {
        noc_semaphore_wait_min(reinterpret_cast<volatile uint32_t*>(sync_bank_addr), sync_val);
    }
}

void kernel_main() {
    using namespace tt::fabric;
    size_t arg_idx = 0;

    const size_t dest_bank_addr = get_arg_val<uint32_t>(arg_idx++);
    const size_t packet_payload_size_bytes = get_arg_val<uint32_t>(arg_idx++);
    const size_t dest_noc_x = get_arg_val<uint32_t>(arg_idx++);
    const size_t dest_noc_y = get_arg_val<uint32_t>(arg_idx++);

    const size_t num_mcasts = get_arg_val<uint32_t>(arg_idx++);
    const size_t mcast_fwd_hops = get_arg_val<uint32_t>(arg_idx++);
    const size_t mcast_bwd_hops = get_arg_val<uint32_t>(arg_idx++);

    const size_t num_unicasts = get_arg_val<uint32_t>(arg_idx++);
    const size_t unicast_hops = get_arg_val<uint32_t>(arg_idx++);
    const bool unicast_is_fwd = get_arg_val<uint32_t>(arg_idx++) != 0;

    const size_t source_l1_cb_index = get_arg_val<uint32_t>(arg_idx++);
    const size_t packet_header_cb = get_arg_val<uint32_t>(arg_idx++);
    const size_t packet_header_size_in_headers = get_arg_val<uint32_t>(arg_idx++);

    auto fabric_connection = FabricConnectionManager::build_from_args(arg_idx);

    ASSERT(fabric_connection.is_logically_connected());

    if (!fabric_connection.is_logically_connected()) {
        return;
    }
    size_t sync_noc_x = 0;
    size_t sync_noc_y = 0;
    size_t sync_bank_addr = 0;
    size_t total_workers_per_sync = 0;
    if (enable_any_synchronization) {
        sync_noc_x = get_arg_val<uint32_t>(arg_idx++);
        sync_noc_y = get_arg_val<uint32_t>(arg_idx++);
        sync_bank_addr = get_arg_val<uint32_t>(arg_idx++);
        total_workers_per_sync = get_arg_val<uint32_t>(arg_idx++);
    }

    const size_t start_sync_val = total_workers_per_sync;
    const size_t finish_sync_val = 3 * total_workers_per_sync;

    fabric_connection.open();

    cb_reserve_back(source_l1_cb_index, 1);
    cb_reserve_back(packet_header_cb, packet_header_size_in_headers);
    const auto source_l1_buffer_address = get_write_ptr(source_l1_cb_index);
    const auto packet_header_buffer_address = get_write_ptr(packet_header_cb);

    auto* mcast_fwd_packet_header = reinterpret_cast<PACKET_HEADER_TYPE*>(packet_header_buffer_address);
    auto* mcast_bwd_packet_header =
        reinterpret_cast<PACKET_HEADER_TYPE*>(packet_header_buffer_address + sizeof(PACKET_HEADER_TYPE));
    auto* unicast_packet_header =
        reinterpret_cast<PACKET_HEADER_TYPE*>(packet_header_buffer_address + sizeof(PACKET_HEADER_TYPE) * 2);
    mcast_fwd_packet_header->to_chip_multicast(MulticastRoutingCommandHeader{1, static_cast<uint8_t>(mcast_fwd_hops)});
    mcast_bwd_packet_header->to_chip_multicast(MulticastRoutingCommandHeader{1, static_cast<uint8_t>(mcast_bwd_hops)});

    if (enable_start_synchronization) {
        line_sync(
            fabric_connection,
            mcast_fwd_packet_header,
            mcast_bwd_packet_header,
            sync_bank_addr,
            sync_noc_x,
            sync_noc_y,
            start_sync_val);
        noc_async_writes_flushed();
        line_sync(
            fabric_connection,
            mcast_fwd_packet_header,
            mcast_bwd_packet_header,
            sync_bank_addr,
            sync_noc_x,
            sync_noc_y,
            2 * start_sync_val);
    }

    mcast_fwd_packet_header->to_chip_multicast(MulticastRoutingCommandHeader{1, static_cast<uint8_t>(mcast_fwd_hops)});
    mcast_bwd_packet_header->to_chip_multicast(MulticastRoutingCommandHeader{1, static_cast<uint8_t>(mcast_bwd_hops)});
    unicast_packet_header->to_chip_unicast(static_cast<uint8_t>(unicast_hops));

    {
        DeviceZoneScopedN("MAIN-WRITE-MCAST-ZONE");
        for (size_t i = 0; i < num_mcasts; i++) {
            auto noc0_dest_addr = safe_get_noc_addr(
                static_cast<uint8_t>(dest_noc_x), static_cast<uint8_t>(dest_noc_y), dest_bank_addr, 0);
            auto dest_addr =
                safe_get_noc_addr(static_cast<uint8_t>(dest_noc_x), static_cast<uint8_t>(dest_noc_y), dest_bank_addr);
            noc_async_write(source_l1_buffer_address, dest_addr, packet_payload_size_bytes);
            if (fabric_connection.has_forward_connection()) {
                mcast_fwd_packet_header->to_noc_unicast_write(
                    NocUnicastCommandHeader{noc0_dest_addr}, packet_payload_size_bytes);
                fabric_connection.get_forward_connection().wait_for_empty_write_slot();
                print_pkt_header(mcast_fwd_packet_header);
                fabric_connection.get_forward_connection().send_payload_without_header_non_blocking_from_address(
                    source_l1_buffer_address, packet_payload_size_bytes);
                fabric_connection.get_forward_connection().send_payload_flush_non_blocking_from_address(
                    (uint32_t)mcast_fwd_packet_header, sizeof(PACKET_HEADER_TYPE));
            }

            if (fabric_connection.has_backward_connection()) {
                mcast_bwd_packet_header->to_noc_unicast_write(
                    NocUnicastCommandHeader{noc0_dest_addr}, packet_payload_size_bytes);
                fabric_connection.get_backward_connection().wait_for_empty_write_slot();
                print_pkt_header(mcast_bwd_packet_header);
                fabric_connection.get_backward_connection().send_payload_without_header_non_blocking_from_address(
                    source_l1_buffer_address, packet_payload_size_bytes);
                fabric_connection.get_backward_connection().send_payload_flush_non_blocking_from_address(
                    (uint32_t)mcast_bwd_packet_header, sizeof(PACKET_HEADER_TYPE));
            }
            {
                noc_async_writes_flushed();
            }
        }
    }

    {
        DeviceZoneScopedN("MAIN-WRITE-UNICAST-ZONE");
        for (size_t i = 0; i < num_unicasts; i++) {
            auto noc0_dest_addr =
                safe_get_noc_addr(static_cast<uint8_t>(dest_noc_x), static_cast<uint8_t>(dest_noc_y), dest_bank_addr, 0);
            auto& fabric_conn =
                unicast_is_fwd ? fabric_connection.get_forward_connection() : fabric_connection.get_backward_connection();
            unicast_packet_header->to_noc_unicast_write(NocUnicastCommandHeader{noc0_dest_addr}, packet_payload_size_bytes);
            fabric_conn.wait_for_empty_write_slot();
            fabric_conn.send_payload_without_header_non_blocking_from_address(
                source_l1_buffer_address, packet_payload_size_bytes);
            fabric_conn.send_payload_blocking_from_address((uint32_t)unicast_packet_header, sizeof(PACKET_HEADER_TYPE));
        }
    }

    if (enable_finish_synchronization) {
        line_sync(
            fabric_connection,
            mcast_fwd_packet_header,
            mcast_bwd_packet_header,
            sync_bank_addr,
            sync_noc_x,
            sync_noc_y,
            finish_sync_val);

        if (sync_noc_x == my_x[0] && sync_noc_y == my_y[0]) {
            // reset the global semaphore in case it is used in a op/kernel
            // invocation
            *reinterpret_cast<volatile uint32_t*>(sync_bank_addr) = 0;
            ;
        }
    }

    {
        DeviceZoneScopedN("WR-CLOSE");
        fabric_connection.close();
    }
    noc_async_write_barrier();
}
