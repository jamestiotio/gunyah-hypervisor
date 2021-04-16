// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <atomic.h>
#include <compiler.h>
#include <cpulocal.h>
#include <log.h>
#include <panic.h>
#include <preempt.h>
#include <scheduler.h>
#include <trace.h>
#include <trace_helpers.h>

#include "etm.h"
#include "event_handlers.h"

#if defined(VERBOSE_TRACE) && VERBOSE_TRACE
#define DEBUG_VETM_TRACES 1
#else
#define DEBUG_VETM_TRACES 0
#endif

void
vetm_handle_boot_hypervisor_start(void)
{
#if !defined(NDEBUG) && DEBUG_VETM_TRACES
	register_t flags = 0U;
	TRACE_SET_CLASS(flags, VETM);
	trace_set_class_flags(flags);
#endif
}

void
vetm_handle_boot_cpu_cold_init(void)
{
	ID_AA64DFR0_EL1_t aa64dfr = register_ID_AA64DFR0_EL1_read();

	// Trace version must be 0 (no system register based trace)
	assert(ID_AA64DFR0_EL1_get_TraceVer(&aa64dfr) == 0U);

	// Trace buffer version must be 0 (no system register trace buffer)
	assert(ID_AA64DFR0_EL1_get_TraceFilt(&aa64dfr) == 0U);
}

static bool
vetm_access_allowed(size_t size, size_t offset)
{
	bool ret;

	// First check if the access is size-aligned
	if ((offset & (size - 1U)) != 0UL) {
		ret = false;
	} else if (size == sizeof(uint32_t)) {
		ret = (offset <= (ETM_SIZE_PERCPU - size));
	} else if (size == sizeof(uint64_t)) {
		ret = (offset <= (ETM_SIZE_PERCPU - size));
	} else {
		// Invalid access size
		ret = false;
	}

	return ret;
}

static ETM_TRCVI_CTLR_t
vetm_protect_trcvi_ctlr(ETM_TRCVI_CTLR_t trcvi_ctlr)
{
	ETM_TRCVI_CTLR_EXLEVEL_NS_t exlevel_ns = ETM_TRCVI_CTLR_EXLEVEL_NS_cast(
		ETM_TRCVI_CTLR_get_exlevel_ns(&trcvi_ctlr));

	// disable hlos hypervisor tracing
	if (ETM_TRCVI_CTLR_EXLEVEL_NS_get_el2(&exlevel_ns)) {
		ETM_TRCVI_CTLR_EXLEVEL_NS_set_el2(&exlevel_ns, false);

		ETM_TRCVI_CTLR_set_exlevel_ns(
			&trcvi_ctlr, ETM_TRCVI_CTLR_EXLEVEL_NS_raw(exlevel_ns));
	}

	// remove secure world tracing
	ETM_TRCVI_CTLR_set_exlevel_s(&trcvi_ctlr, 0xf);

	return trcvi_ctlr;
}

static bool
vetm_vdevice_write(thread_t *vcpu, cpu_index_t pcpu, size_t offset,
		   register_t val, size_t access_size)
{
	assert(vcpu != NULL);

	register_t write_val = val;

	if (offset == offsetof(etm_t, trcprgctlr)) {
		vcpu->vetm_enabled = ((val & 0x1U) != 0U);
	} else if (offset == offsetof(etm_t, trcvictlr)) {
		ETM_TRCVI_CTLR_t trcvi_ctlr =
			ETM_TRCVI_CTLR_cast((uint32_t)write_val);
		vcpu->vetm_trcvi_ctlr = vetm_protect_trcvi_ctlr(trcvi_ctlr);
		write_val = ETM_TRCVI_CTLR_raw(vcpu->vetm_trcvi_ctlr);
	}

	etm_set_reg(pcpu, offset, write_val, access_size);

	return true;
}

static bool
vetm_vdevice_read(thread_t *vcpu, cpu_index_t pcpu, size_t offset,
		  register_t *val, size_t access_size)
{
	(void)vcpu;

	etm_get_reg(pcpu, offset, val, access_size);

	return true;
}

bool
vetm_handle_vdevice_access(vmaddr_t ipa, size_t access_size, register_t *value,
			   bool is_write)
{
	bool ret;

	cpulocal_begin();

	thread_t *vcpu = thread_get_self();
	assert(vcpu != NULL);

	cpu_index_t pcpu = cpulocal_get_index();
	thread_t *  hlos = scheduler_get_primary_vcpu(pcpu);
	if (hlos != thread_get_self()) {
		ret = false;
		goto out;
	}

	if ((ipa >= PLATFORM_ETM_BASE) &&
	    (ipa < PLATFORM_ETM_BASE + ETM_STRIDE * PLATFORM_MAX_CORES)) {
		size_t	    base_offset = (size_t)(ipa - PLATFORM_ETM_BASE);
		cpu_index_t access_pcpu =
			(cpu_index_t)(base_offset / ETM_STRIDE);
		size_t offset = ipa - PLATFORM_ETM_BASE - pcpu * ETM_STRIDE;

		if ((pcpu == access_pcpu) &&
		    vetm_access_allowed(access_size, offset)) {
			if (is_write) {
				ret = vetm_vdevice_write(vcpu, pcpu, offset,
							 *value, access_size);
			} else {
				ret = vetm_vdevice_read(vcpu, pcpu, offset,
							value, access_size);
			}
		} else {
			ret = false;
		}
	} else {
		ret = false;
	}
out:
	cpulocal_end();

	return ret;
}

void
vetm_handle_thread_load_state(void)
{
	thread_t *vcpu = thread_get_self();
	assert(vcpu != NULL);

	cpu_index_t pcpu = cpulocal_get_index();
	if (vcpu == scheduler_get_primary_vcpu(pcpu)) {
		etm_set_reg(pcpu, offsetof(etm_t, trcvictlr),
			    ETM_TRCVI_CTLR_raw(vcpu->vetm_trcvi_ctlr),
			    sizeof(vcpu->vetm_trcvi_ctlr));
	}
}

error_t
vetm_handle_thread_context_switch_pre(void)
{
	thread_t *vcpu = thread_get_self();
	assert(vcpu != NULL);

	cpu_index_t pcpu = cpulocal_get_index();
	if (vcpu == scheduler_get_primary_vcpu(pcpu)) {
		// clear trcvi_ctlr
		ETM_TRCVI_CTLR_t trcvi_ctlr = ETM_TRCVI_CTLR_default();
		etm_set_reg(pcpu, offsetof(etm_t, trcvictlr),
			    ETM_TRCVI_CTLR_raw(trcvi_ctlr), sizeof(trcvi_ctlr));
	}

	return OK;
}
