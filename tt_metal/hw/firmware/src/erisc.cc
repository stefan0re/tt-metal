// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "ethernet/dataflow_api.h"
#include "ethernet/tunneling.h"
#include "firmware_common.h"
#include "noc_parameters.h"
#include "risc_attribs.h"
#include "dataflow_api.h"
#include "tools/profiler/kernel_profiler.hpp"
#include "debug/watcher_common.h"

#if defined(PROFILE_KERNEL)
namespace kernel_profiler {
    uint32_t wIndex __attribute__((used));
    uint32_t stackSize __attribute__((used));
    uint32_t sums[SUM_COUNT] __attribute__((used));
    uint32_t sumIDs[SUM_COUNT] __attribute__((used));
}
#endif

uint8_t noc_index = 0;  // TODO: remove hardcoding
uint8_t my_x[NUM_NOCS] __attribute__((used));
uint8_t my_y[NUM_NOCS] __attribute__((used));

uint32_t noc_reads_num_issued[NUM_NOCS] __attribute__((used));
uint32_t noc_nonposted_writes_num_issued[NUM_NOCS] __attribute__((used));
uint32_t noc_nonposted_writes_acked[NUM_NOCS] __attribute__((used));
uint32_t noc_nonposted_atomics_acked[NUM_NOCS] __attribute__((used));
uint32_t noc_posted_writes_num_issued[NUM_NOCS] __attribute__((used));

uint32_t tt_l1_ptr *rta_l1_base __attribute__((used));
uint32_t tt_l1_ptr *crta_l1_base __attribute__((used));
uint32_t tt_l1_ptr *sem_l1_base[ProgrammableCoreType::COUNT] __attribute__((used));

// These arrays are stored in local memory of FW, but primarily used by the kernel which shares
// FW symbols. Hence mark these as 'used' so that FW compiler doesn't optimize it out.
uint16_t dram_bank_to_noc_xy[NUM_NOCS][NUM_DRAM_BANKS] __attribute__((used));
uint16_t l1_bank_to_noc_xy[NUM_NOCS][NUM_L1_BANKS] __attribute__((used));
int32_t bank_to_dram_offset[NUM_DRAM_BANKS] __attribute__((used));
int32_t bank_to_l1_offset[NUM_L1_BANKS] __attribute__((used));

void __attribute__((noinline)) Application(void) {
    WAYPOINT("I");

    // Not using do_crt1 since it is copying to registers???
    // bss already cleared in entry code.
    // TODO: need to find free space that routing FW is not using

    rtos_context_switch_ptr = (void (*)())RtosTable[0];

    noc_bank_table_init(eth_l1_mem::address_map::ERISC_MEM_BANK_TO_NOC_SCRATCH);

    risc_init();
    noc_init(MEM_NOC_ATOMIC_RET_VAL_ADDR);

    for (uint32_t n = 0; n < NUM_NOCS; n++) {
        noc_local_state_init(n);
    }
    ncrisc_noc_full_sync();
    WAYPOINT("REW");
    uint32_t count = 0;
    while (routing_info->routing_enabled != 1) {
        volatile uint32_t *ptr = (volatile uint32_t *)0xffb2010c;
        count++;
        *ptr = 0xAABB0000 | (count & 0xFFFF);
        internal_::risc_context_switch();
    }
    WAYPOINT("RED");

    mailboxes->launch_msg_rd_ptr = 0; // Initialize the rdptr to 0
    while (routing_info->routing_enabled) {
        // FD: assume that no more host -> remote writes are pending
        uint8_t go_message_signal = mailboxes->go_message.signal;
        if (go_message_signal == RUN_MSG_GO) {
            // Only include this iteration in the device profile if the launch message is valid. This is because all workers get a go signal regardless of whether
            // they're running a kernel or not. We don't want to profile "invalid" iterations.
            DeviceZoneScopedMainN("ERISC-FW");
            uint32_t launch_msg_rd_ptr = mailboxes->launch_msg_rd_ptr;
            launch_msg_t* launch_msg_address = &(mailboxes->launch[launch_msg_rd_ptr]);
            DeviceValidateProfiler(launch_msg_address->kernel_config.enables);
            DeviceZoneSetCounter(launch_msg_address->kernel_config.host_assigned_id);
            // Note that a core may get "GO" w/ enable false to keep its launch_msg's in sync
            enum dispatch_core_processor_masks enables = (enum dispatch_core_processor_masks)launch_msg_address->kernel_config.enables;
            if (enables & DISPATCH_CLASS_MASK_ETH_DM0) {
                WAYPOINT("R");
                firmware_config_init(mailboxes, ProgrammableCoreType::ACTIVE_ETH, DISPATCH_CLASS_ETH_DM0);
                kernel_init(0);
                WAYPOINT("D");
            }
            mailboxes->go_message.signal = RUN_MSG_DONE;

            if (launch_msg_address->kernel_config.mode == DISPATCH_MODE_DEV) {
                launch_msg_address->kernel_config.enables = 0;
                uint64_t dispatch_addr = NOC_XY_ADDR(
                    NOC_X(mailboxes->go_message.master_x),
                    NOC_Y(mailboxes->go_message.master_y),
                    DISPATCH_MESSAGE_ADDR + mailboxes->go_message.dispatch_message_offset);
                CLEAR_PREVIOUS_LAUNCH_MESSAGE_ENTRY_FOR_WATCHER();
                internal_::notify_dispatch_core_done(dispatch_addr);
                mailboxes->launch_msg_rd_ptr = (launch_msg_rd_ptr + 1) & (launch_msg_buffer_num_entries - 1);
                // Only executed if watcher is enabled. Ensures that we don't report stale data due to invalid launch
                // messages in the ring buffer
            }

        } else if (go_message_signal == RUN_MSG_RESET_READ_PTR) {
            // Reset the launch message buffer read ptr
            mailboxes->launch_msg_rd_ptr = 0;
            uint64_t dispatch_addr = NOC_XY_ADDR(
                NOC_X(mailboxes->go_message.master_x),
                NOC_Y(mailboxes->go_message.master_y),
                DISPATCH_MESSAGE_ADDR + mailboxes->go_message.dispatch_message_offset);
            mailboxes->go_message.signal = RUN_MSG_DONE;
            internal_::notify_dispatch_core_done(dispatch_addr);
        } else {
            internal_::risc_context_switch();
        }
    }
    internal_::disable_erisc_app();
}
