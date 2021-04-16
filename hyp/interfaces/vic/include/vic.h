// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Interfaces for managing virtual interrupt controllers.
//
// Virtual IRQs are delivered by a virtual interrupt controller (VIC), which is
// a first-class object with defined relationships to a set of VCPUs. The VIC
// may implement a hypercall-based VM interface and/or emulate a hardware
// interrupt controller's register interface for delivering these interrupts.
//
// Each VIRQ uses one of two two routing types, specified by the VIRQ source
// when registering it with the controller, to select a VCPU to deliver to.
// These routing types are:
//
// - Private: targeted at a pre-determined VCPU, and with assertion state that
//   is specific ta that VCPU.
//
// - Shared: there is a single common state shared by all VCPUs. A target VCPU
//   is selected at runtime, based on implementation-defined criteria which may
//   be modified by the VM. Note that this may include affinity to a particular
//   VCPU, but unlike private routing this affinity is not an inherent property
//   of the VIRQ: it may be modifiable at runtime, may target more than one
//   VCPU, and may be influenced by the current state of the target VCPU.
//
// Note that the implementation may restrict shared and/or private VIRQ numbers
// to specific ranges which may or may not overlap. This interface does not
// provide any means of querying those ranges; the caller must either know them,
// or obtain VIRQ numbers from a resource manager VM that knows them.
//
// To register a VIRQ, call one of the vic_bind_* functions with a source
// structure owner by the caller, specifying an interrupt controller object,
// a VIRQ number in an appropriate range, and a triggering type defined by the
// caller. The type value will be used as a selector for the virq_check_pending
// event, which is triggered when a level-sensitive interrupt is synchronised.
//
// The caller is responsible for calling vic_unbind() on the source structure
// in the cleanup handler of the object containing the source structure.
//
// The caller must either hold references to all specified object(s) (including
// the object that contains the VIRQ source structure), or else be in an RCU
// read-side critical section.

// Exclusively claim a shared VIRQ on the specified VIC.
//
// This prevents the VIRQ being claimed by any other source, and allows calls
// to virq_assert() and virq_clear().
//
// Note that this does not take a reference to the VIC. If the VIC is later
// freed, calls to virq_assert() will fail.
//
// This function should be used in preference to vic_bind_private_*() for any
// VIRQ that is not inherently bound to a VCPU. This includes nearly all VIRQs
// generated by first class objects other than the VCPU itself.
//
// Returns ERROR_VIRQ_BOUND if the specified source object has previously been
// bound to VIRQ, and not subsequently unbound by calling vic_unbind_sync().
// Returns ERROR_BUSY if the specified VIRQ has already been claimed. Returns
// ERROR_ARGUMENT_INVALID if the specified VIRQ number is out of range.
error_t
vic_bind_shared(virq_source_t *source, vic_t *vic, virq_t virq,
		virq_trigger_t trigger);

// Exclusively claim a private VIRQ on the specified VCPU.
//
// There are two variants of this function which differ only in the way the
// target is specified: one accepts a VCPU object pointer, the other a VIC
// object pointer and a VCPU attachment index.
//
// This operates the same way as vic_bind_shared(), but for private
// (VCPU-local) VIRQs. Note that it must be called for each VCPU that will
// receive the interrupt, with separate source objects. It is strongly
// recommended to repeat this call for every VCPU in the VM, using the same VIRQ
// number each time.
//
// Normally this function should only be used for VIRQs that are inherently
// associated with a particular VCPU and can only reasonably be handled by that
// VCPU; e.g. local timers or performance monitors. Anything else should use the
// shared version instead.
//
// Returns ERROR_VIRQ_BOUND if the specified source object has previously been
// bound to VIRQ, and not subsequently unbound by calling vic_unbind_sync().
// Returns ERROR_BUSY if the specified VIRQ has already been claimed. Returns
// ERROR_ARGUMENT_INVALID if the specified VIRQ number is out of range. Returns
// ERROR_OBJECT_CONFIG if the specified index does not have an associated VCPU
// or the attachment between the VCPU and VIC is concurrently broken.
error_t
vic_bind_private_vcpu(virq_source_t *source, thread_t *vcpu, virq_t virq,
		      virq_trigger_t trigger);
error_t
vic_bind_private_index(virq_source_t *source, vic_t *vic, index_t index,
		       virq_t virq, virq_trigger_t trigger);

// Release an exclusive claim to a VIRQ.
//
// Note that if the VIRQ source is currently pending, it will be cleared, as if
// virq_clear() was called. However, like virq_clear(), this function does not
// wait for cancellation of the specified VIRQ on every registered VCPU. If the
// VIRQ is currently asserted and routed to a VCPU that is active on a remote
// physical CPU, the interrupt may be spuriously delivered to the VM shortly
// after this function returns.
//
// The caller must ensure that an RCU grace period elapses between the return
// of this function and the deallocation of the storage containing the source
// structure. Note that this requirement is satisfied by calling this function
// from the enclosing first-class object's deactivate event handler.
//
// Any attempt to reuse the source structure for a new vic_bind_*() call is
// permitted to fail as if this function had not been called, even if an RCU
// grace period has elapsed.
//
// If the source has not claimed a VIRQ, or was claimed for a VIC or VCPU that
// has since been destroyed, this function has no effect.
void
vic_unbind(virq_source_t *source);

// Release an exclusive claim to a VIRQ and make the source ready for reuse.
//
// This function performs the same operation as vic_unbind(); additionally, it
// waits for the implicit virq_clear() operation to complete, and then resets
// the source so that it may be used by a subsequent vic_bind_*() call.
//
// This function may call the scheduler, and therefore must not be called from
// an RCU read-side critical section or a spinlock.
void
vic_unbind_sync(virq_source_t *source);
