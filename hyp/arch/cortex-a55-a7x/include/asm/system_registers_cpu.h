// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// AArch64 System Register Encoding (CPU Implementation Defined Registers)
//
// This list is not exhaustive, it contains mostly registers likely to be
// trapped and accessed indirectly.

// RAS registers
#define ISS_MRS_MSR_ERXPFGFR_EL1   ISS_OP0_OP1_CRN_CRM_OP2(3, 0, 15, 2, 0)
#define ISS_MRS_MSR_ERXPFGCTLR_EL1 ISS_OP0_OP1_CRN_CRM_OP2(3, 0, 15, 2, 1)
#define ISS_MRS_MSR_ERXPFGCDNR_EL1 ISS_OP0_OP1_CRN_CRM_OP2(3, 0, 15, 2, 2)

#define ISS_MRS_MSR_CPUACTLR_EL1 ISS_OP0_OP1_CRN_CRM_OP2(3, 0, 15, 1, 0)
// CPUACTLR2_EL1 does not exist on A55
#define ISS_MRS_MSR_A7X_CPUACTLR2_EL1 ISS_OP0_OP1_CRN_CRM_OP2(3, 0, 15, 1, 1)
#define ISS_MRS_MSR_CPUECTLR_EL1      ISS_OP0_OP1_CRN_CRM_OP2(3, 0, 15, 1, 4)
#define ISS_MRS_MSR_CPUPWRCTLR_EL1    ISS_OP0_OP1_CRN_CRM_OP2(3, 0, 15, 2, 7)
