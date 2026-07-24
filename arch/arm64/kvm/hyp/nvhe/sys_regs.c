// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Google LLC
 * Author: Fuad Tabba <tabba@google.com>
 */

#include <linux/irqchip/arm-gic-v3.h>

#include <asm/kvm_asm.h>
#include <asm/kvm_mmu.h>

#include <hyp/adjust_pc.h>
#include <hyp/debug-sr.h>

#include <nvhe/pkvm.h>

#include "../../sys_regs.h"

/*
 * Copies of the host's CPU features registers holding sanitized values at hyp.
 */
u64 id_aa64pfr0_el1_sys_val;
u64 id_aa64pfr1_el1_sys_val;
u64 id_aa64pfr2_el1_sys_val;
u64 id_aa64zfr0_el1_sys_val;
u64 id_aa64isar0_el1_sys_val;
u64 id_aa64isar1_el1_sys_val;
u64 id_aa64isar2_el1_sys_val;
u64 id_aa64mmfr0_el1_sys_val;
u64 id_aa64mmfr1_el1_sys_val;
u64 id_aa64mmfr2_el1_sys_val;
u64 id_aa64mmfr3_el1_sys_val;
u64 id_aa64smfr0_el1_sys_val;
u64 id_aa64dfr0_el1_sys_val;

struct pvm_ftr_bits {
	bool		sign;
	u8		shift;
	u8		width;
	u8		max_val;
	bool (*vm_supported)(const struct kvm *kvm);
};

#define __MAX_FEAT_FUNC(id, fld, max, func, sgn)				\
	{									\
		.sign = sgn,							\
		.shift = id##_##fld##_SHIFT,					\
		.width = id##_##fld##_WIDTH,					\
		.max_val = id##_##fld##_##max,					\
		.vm_supported = func,						\
	}

#define MAX_FEAT_FUNC(id, fld, max, func)					\
	__MAX_FEAT_FUNC(id, fld, max, func, id##_##fld##_SIGNED)

#define MAX_FEAT(id, fld, max)							\
	MAX_FEAT_FUNC(id, fld, max, NULL)

#define MAX_FEAT_ENUM(id, fld, max)						\
	__MAX_FEAT_FUNC(id, fld, max, NULL, false)

/* Pass the sanitised host value through unchanged, capped at the field max. */
#define MAX_FEAT_PASSTHRU(id, fld)						\
	{									\
		.sign = false,							\
		.shift = id##_##fld##_SHIFT,					\
		.width = id##_##fld##_WIDTH,					\
		.max_val = GENMASK(id##_##fld##_WIDTH - 1, 0),			\
		.vm_supported = NULL,						\
	}

#define FEAT_END {	.width = 0,	}

static bool vm_has_ptrauth(const struct kvm *kvm)
{
	if (!IS_ENABLED(CONFIG_ARM64_PTR_AUTH))
		return false;

	return (cpus_have_final_cap(ARM64_HAS_ADDRESS_AUTH) ||
		cpus_have_final_cap(ARM64_HAS_GENERIC_AUTH)) &&
		kvm_vcpu_has_feature(kvm, KVM_ARM_VCPU_PTRAUTH_GENERIC);
}

static bool vm_has_sve(const struct kvm *kvm)
{
	return system_supports_sve() && kvm_vcpu_has_feature(kvm, KVM_ARM_VCPU_SVE);
}

/*
 * Definitions for features to be allowed or restricted for protected guests.
 *
 * Each field in the masks represents the highest supported value for the
 * feature. If a feature field is not present, it is not supported. Moreover,
 * these are used to generate the guest's view of the feature registers.
 *
 * The approach for protected VMs is to at least support features that are:
 * - Needed by common Linux distributions (e.g., floating point)
 * - Trivial to support, e.g., supporting the feature does not introduce or
 * require tracking of additional state in KVM
 * - Cannot be trapped or prevent the guest from using anyway
 */

static const struct pvm_ftr_bits pvmid_aa64pfr0[] = {
	MAX_FEAT(ID_AA64PFR0_EL1, EL0, IMP),
	MAX_FEAT(ID_AA64PFR0_EL1, EL1, IMP),
	MAX_FEAT(ID_AA64PFR0_EL1, EL2, IMP),
	MAX_FEAT(ID_AA64PFR0_EL1, EL3, IMP),
	MAX_FEAT(ID_AA64PFR0_EL1, FP, FP16),
	MAX_FEAT(ID_AA64PFR0_EL1, AdvSIMD, FP16),
	MAX_FEAT(ID_AA64PFR0_EL1, GIC, IMP),
	MAX_FEAT_FUNC(ID_AA64PFR0_EL1, SVE, IMP, vm_has_sve),
	MAX_FEAT(ID_AA64PFR0_EL1, RAS, IMP),
	MAX_FEAT(ID_AA64PFR0_EL1, DIT, IMP),
	MAX_FEAT(ID_AA64PFR0_EL1, CSV2, IMP),
	MAX_FEAT(ID_AA64PFR0_EL1, CSV3, IMP),
	FEAT_END
};

static const struct pvm_ftr_bits pvmid_aa64pfr1[] = {
	MAX_FEAT(ID_AA64PFR1_EL1, BT, IMP),
	MAX_FEAT(ID_AA64PFR1_EL1, SSBS, SSBS2),
	MAX_FEAT_ENUM(ID_AA64PFR1_EL1, MTE_frac, NI),
	FEAT_END
};

static const struct pvm_ftr_bits pvmid_aa64pfr2[] = {
	MAX_FEAT(ID_AA64PFR2_EL1, FPMR, IMP),
	FEAT_END
};

static const struct pvm_ftr_bits pvmid_aa64mmfr0[] = {
	MAX_FEAT_ENUM(ID_AA64MMFR0_EL1, PARANGE, 40),
	MAX_FEAT_ENUM(ID_AA64MMFR0_EL1, ASIDBITS, 16),
	MAX_FEAT(ID_AA64MMFR0_EL1, BIGEND, IMP),
	MAX_FEAT(ID_AA64MMFR0_EL1, SNSMEM, IMP),
	MAX_FEAT(ID_AA64MMFR0_EL1, BIGENDEL0, IMP),
	MAX_FEAT_ENUM(ID_AA64MMFR0_EL1, TGRAN16, IMP),
	MAX_FEAT(ID_AA64MMFR0_EL1, EXS, IMP),
	FEAT_END
};

static const struct pvm_ftr_bits pvmid_aa64mmfr1[] = {
	MAX_FEAT(ID_AA64MMFR1_EL1, HAFDBS, DBM),
	MAX_FEAT_ENUM(ID_AA64MMFR1_EL1, VMIDBits, 16),
	MAX_FEAT(ID_AA64MMFR1_EL1, HPDS, HPDS2),
	MAX_FEAT(ID_AA64MMFR1_EL1, PAN, PAN3),
	MAX_FEAT(ID_AA64MMFR1_EL1, SpecSEI, IMP),
	MAX_FEAT(ID_AA64MMFR1_EL1, ETS, IMP),
	MAX_FEAT(ID_AA64MMFR1_EL1, CMOW, IMP),
	FEAT_END
};

static const struct pvm_ftr_bits pvmid_aa64mmfr2[] = {
	MAX_FEAT(ID_AA64MMFR2_EL1, CnP, IMP),
	MAX_FEAT(ID_AA64MMFR2_EL1, UAO, IMP),
	MAX_FEAT(ID_AA64MMFR2_EL1, IESB, IMP),
	MAX_FEAT(ID_AA64MMFR2_EL1, AT, IMP),
	MAX_FEAT_ENUM(ID_AA64MMFR2_EL1, IDS, 0x18),
	MAX_FEAT(ID_AA64MMFR2_EL1, TTL, IMP),
	MAX_FEAT(ID_AA64MMFR2_EL1, BBM, 2),
	MAX_FEAT(ID_AA64MMFR2_EL1, E0PD, IMP),
	FEAT_END
};

static const struct pvm_ftr_bits pvmid_aa64isar1[] = {
	MAX_FEAT(ID_AA64ISAR1_EL1, DPB, DPB2),
	MAX_FEAT_FUNC(ID_AA64ISAR1_EL1, APA, PAuth, vm_has_ptrauth),
	MAX_FEAT_FUNC(ID_AA64ISAR1_EL1, API, PAuth, vm_has_ptrauth),
	MAX_FEAT(ID_AA64ISAR1_EL1, JSCVT, IMP),
	MAX_FEAT(ID_AA64ISAR1_EL1, FCMA, IMP),
	MAX_FEAT(ID_AA64ISAR1_EL1, LRCPC, LRCPC3),
	MAX_FEAT(ID_AA64ISAR1_EL1, GPA, IMP),
	MAX_FEAT(ID_AA64ISAR1_EL1, GPI, IMP),
	MAX_FEAT(ID_AA64ISAR1_EL1, FRINTTS, IMP),
	MAX_FEAT(ID_AA64ISAR1_EL1, SB, IMP),
	MAX_FEAT(ID_AA64ISAR1_EL1, SPECRES, COSP_RCTX),
	MAX_FEAT(ID_AA64ISAR1_EL1, BF16, EBF16),
	MAX_FEAT(ID_AA64ISAR1_EL1, DGH, IMP),
	MAX_FEAT(ID_AA64ISAR1_EL1, I8MM, IMP),
	FEAT_END
};

static const struct pvm_ftr_bits pvmid_aa64isar2[] = {
	MAX_FEAT_FUNC(ID_AA64ISAR2_EL1, GPA3, IMP, vm_has_ptrauth),
	MAX_FEAT_FUNC(ID_AA64ISAR2_EL1, APA3, PAuth, vm_has_ptrauth),
	MAX_FEAT(ID_AA64ISAR2_EL1, MOPS, IMP),
	MAX_FEAT(ID_AA64ISAR2_EL1, ATS1A, IMP),
	FEAT_END
};

static const struct pvm_ftr_bits pvmid_aa64mmfr3[] = {
	MAX_FEAT(ID_AA64MMFR3_EL1, TCRX, IMP),
	MAX_FEAT(ID_AA64MMFR3_EL1, SCTLRX, IMP),
	MAX_FEAT(ID_AA64MMFR3_EL1, S1PIE, IMP),
	MAX_FEAT(ID_AA64MMFR3_EL1, S1POE, IMP),
	FEAT_END
};

static const struct pvm_ftr_bits pvmid_aa64zfr0[] = {
	MAX_FEAT_FUNC(ID_AA64ZFR0_EL1, SVEver, SVE2p2, vm_has_sve),
	MAX_FEAT_FUNC(ID_AA64ZFR0_EL1, AES, AES2, vm_has_sve),
	MAX_FEAT_FUNC(ID_AA64ZFR0_EL1, EltPerm, IMP, vm_has_sve),
	MAX_FEAT_FUNC(ID_AA64ZFR0_EL1, BitPerm, IMP, vm_has_sve),
	MAX_FEAT_FUNC(ID_AA64ZFR0_EL1, BF16, EBF16, vm_has_sve),
	MAX_FEAT_FUNC(ID_AA64ZFR0_EL1, B16B16, IMP, vm_has_sve),
	MAX_FEAT_FUNC(ID_AA64ZFR0_EL1, SHA3, IMP, vm_has_sve),
	MAX_FEAT_FUNC(ID_AA64ZFR0_EL1, SM4, IMP, vm_has_sve),
	MAX_FEAT_FUNC(ID_AA64ZFR0_EL1, I8MM, IMP, vm_has_sve),
	MAX_FEAT_FUNC(ID_AA64ZFR0_EL1, F16MM, IMP, vm_has_sve),
	MAX_FEAT_FUNC(ID_AA64ZFR0_EL1, F32MM, IMP, vm_has_sve),
	MAX_FEAT_FUNC(ID_AA64ZFR0_EL1, F64MM, IMP, vm_has_sve),
	FEAT_END
};

/*
 * Expose the debug registers to protected guests: the debug architecture
 * version (capped as normal KVM caps it, sanitise_id_aa64dfr0_el1()) and the
 * breakpoint/watchpoint counts. Everything else stays hidden. DoubleLock and
 * MTPMU have a non-zero Not-Implemented encoding, so carry them explicitly - a
 * field absent from the table reads 0, which for these two means implemented.
 */
static const struct pvm_ftr_bits pvmid_aa64dfr0[] = {
	MAX_FEAT(ID_AA64DFR0_EL1, DebugVer, V8P8),
	MAX_FEAT_PASSTHRU(ID_AA64DFR0_EL1, BRPs),
	MAX_FEAT_PASSTHRU(ID_AA64DFR0_EL1, WRPs),
	MAX_FEAT_PASSTHRU(ID_AA64DFR0_EL1, CTX_CMPs),
	MAX_FEAT(ID_AA64DFR0_EL1, DoubleLock, NI),
	MAX_FEAT(ID_AA64DFR0_EL1, MTPMU, NI),
	FEAT_END
};

/*
 * None of the features in ID_AA64MMFR4_EL1 are supported, but it has a
 * Not-Implemented value that is non-zero. Define it so it can be used when
 * getting the value of the register.
 */
#define ID_AA64MMFR4_EL1_NONZERO_NI					\
	SYS_FIELD_PREP_ENUM(ID_AA64MMFR4_EL1, E2H0, NI)

/*
 * Returns the value of the feature registers based on the system register
 * value, the vcpu support for the revelant features, and the additional
 * restrictions for protected VMs.
 */
static u64 get_restricted_features(const struct kvm_vcpu *vcpu,
				   u64 sys_reg_val,
				   const struct pvm_ftr_bits restrictions[])
{
	u64 val = 0UL;
	int i;

	for (i = 0; restrictions[i].width != 0; i++) {
		bool (*vm_supported)(const struct kvm *) = restrictions[i].vm_supported;
		bool sign = restrictions[i].sign;
		int shift = restrictions[i].shift;
		int width = restrictions[i].width;
		u64 min_signed = (1UL << width) - 1UL;
		u64 sign_bit = 1UL << (width - 1);
		u64 mask = GENMASK_ULL(width + shift - 1, shift);
		u64 sys_val = (sys_reg_val & mask) >> shift;
		u64 pvm_max = restrictions[i].max_val;

		if (vm_supported && !vm_supported(vcpu->kvm))
			val |= (sign ? min_signed : 0) << shift;
		else if (sign && (sys_val >= sign_bit || pvm_max >= sign_bit))
			val |= max(sys_val, pvm_max) << shift;
		else
			val |= min(sys_val, pvm_max) << shift;
	}

	return val;
}

static u64 pvm_calc_id_reg(const struct kvm_vcpu *vcpu, u32 id)
{
	switch (id) {
	case SYS_ID_AA64PFR0_EL1:
		return get_restricted_features(vcpu, id_aa64pfr0_el1_sys_val, pvmid_aa64pfr0);
	case SYS_ID_AA64PFR1_EL1:
		return get_restricted_features(vcpu, id_aa64pfr1_el1_sys_val, pvmid_aa64pfr1);
	case SYS_ID_AA64PFR2_EL1:
		return get_restricted_features(vcpu, id_aa64pfr2_el1_sys_val, pvmid_aa64pfr2);
	case SYS_ID_AA64ISAR0_EL1:
		return id_aa64isar0_el1_sys_val;
	case SYS_ID_AA64ISAR1_EL1:
		return get_restricted_features(vcpu, id_aa64isar1_el1_sys_val, pvmid_aa64isar1);
	case SYS_ID_AA64ISAR2_EL1:
		return get_restricted_features(vcpu, id_aa64isar2_el1_sys_val, pvmid_aa64isar2);
	case SYS_ID_AA64MMFR0_EL1:
		return get_restricted_features(vcpu, id_aa64mmfr0_el1_sys_val, pvmid_aa64mmfr0);
	case SYS_ID_AA64MMFR1_EL1:
		return get_restricted_features(vcpu, id_aa64mmfr1_el1_sys_val, pvmid_aa64mmfr1);
	case SYS_ID_AA64MMFR2_EL1:
		return get_restricted_features(vcpu, id_aa64mmfr2_el1_sys_val, pvmid_aa64mmfr2);
	case SYS_ID_AA64MMFR3_EL1:
		return get_restricted_features(vcpu, id_aa64mmfr3_el1_sys_val, pvmid_aa64mmfr3);
	case SYS_ID_AA64ZFR0_EL1:
		return get_restricted_features(vcpu, id_aa64zfr0_el1_sys_val, pvmid_aa64zfr0);
	case SYS_ID_AA64DFR0_EL1:
		return get_restricted_features(vcpu, id_aa64dfr0_el1_sys_val, pvmid_aa64dfr0);
	case SYS_ID_AA64MMFR4_EL1:
		return ID_AA64MMFR4_EL1_NONZERO_NI;
	default:
		/* Unhandled ID register, RAZ */
		return 0;
	}
}

/*
 * Inject an unknown/undefined exception to an AArch64 guest while most of its
 * sysregs are live.
 */
static void inject_undef64(struct kvm_vcpu *vcpu)
{
	u64 esr = (ESR_ELx_EC_UNKNOWN << ESR_ELx_EC_SHIFT);

	*vcpu_pc(vcpu) = read_sysreg_el2(SYS_ELR);
	*vcpu_cpsr(vcpu) = read_sysreg_el2(SYS_SPSR);

	/*
	 * Sync VBAR_EL1 and SCTLR_EL1, both read by enter_exception64(),
	 * as pKVM handles traps before sysregs are resync'ed.
	 */
	__vcpu_assign_sys_reg(vcpu, VBAR_EL1, read_sysreg_el1(SYS_VBAR));
	__vcpu_assign_sys_reg(vcpu, SCTLR_EL1, read_sysreg_el1(SYS_SCTLR));

	kvm_pend_exception(vcpu, EXCEPT_AA64_EL1_SYNC);

	__kvm_adjust_pc(vcpu);

	write_sysreg_el1(esr, SYS_ESR);
	write_sysreg_el1(read_sysreg_el2(SYS_ELR), SYS_ELR);
	write_sysreg_el1(read_sysreg_el2(SYS_SPSR), SYS_SPSR);
	write_sysreg_el2(*vcpu_pc(vcpu), SYS_ELR);
	write_sysreg_el2(*vcpu_cpsr(vcpu), SYS_SPSR);
}

static u64 read_id_reg(const struct kvm_vcpu *vcpu,
		       struct sys_reg_desc const *r)
{
	struct kvm *kvm = vcpu->kvm;
	u32 reg = reg_to_encoding(r);

	if (WARN_ON_ONCE(!test_bit(KVM_ARCH_FLAG_ID_REGS_INITIALIZED, &kvm->arch.flags)))
		return 0;

	if (reg >= sys_reg(3, 0, 0, 1, 0) && reg <= sys_reg(3, 0, 0, 7, 7))
		return kvm->arch.id_regs[IDREG_IDX(reg)];

	return 0;
}

/* Handler to RAZ/WI sysregs */
static bool pvm_access_raz_wi(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			      const struct sys_reg_desc *r)
{
	if (!p->is_write)
		p->regval = 0;

	return true;
}

/*
 * Accessor for AArch32 feature id registers.
 *
 * The value of these registers is "unknown" according to the spec if AArch32
 * isn't supported.
 */
static bool pvm_access_id_aarch32(struct kvm_vcpu *vcpu,
				  struct sys_reg_params *p,
				  const struct sys_reg_desc *r)
{
	if (p->is_write) {
		inject_undef64(vcpu);
		return false;
	}

	return pvm_access_raz_wi(vcpu, p, r);
}

/*
 * Accessor for AArch64 feature id registers.
 *
 * If access is allowed, set the regval to the protected VM's view of the
 * register and return true.
 * Otherwise, inject an undefined exception and return false.
 */
static bool pvm_access_id_aarch64(struct kvm_vcpu *vcpu,
				  struct sys_reg_params *p,
				  const struct sys_reg_desc *r)
{
	if (p->is_write) {
		inject_undef64(vcpu);
		return false;
	}

	p->regval = read_id_reg(vcpu, r);
	return true;
}

static bool pvm_access_unallocated(struct kvm_vcpu *vcpu,
				   struct sys_reg_params *p,
				   const struct sys_reg_desc *r)
{
	if (p->is_write) {
		inject_undef64(vcpu);
		return false;
	}

	p->regval = 0;
	return true;
}

static bool pvm_gic_read_sre(struct kvm_vcpu *vcpu,
			     struct sys_reg_params *p,
			     const struct sys_reg_desc *r)
{
	/* pVMs only support GICv3. 'nuf said. */
	if (!p->is_write)
		p->regval = ICC_SRE_EL1_DIB | ICC_SRE_EL1_DFB | ICC_SRE_EL1_SRE;

	return true;
}

/*
 * Self-hosted debug for protected guests: an EL2-private mirror of KVM's
 * debug_owner lazy switch, run at EL2 since the host must not touch a protected
 * vCPU's debug state. While DebugVer is hidden every handler falls back to
 * RAZ/WI, so the machinery is inert until ID_AA64DFR0_EL1 exposes debug.
 */
static bool pvm_debug_hidden(struct kvm_vcpu *vcpu)
{
	return !kvm_has_feat(vcpu->kvm, ID_AA64DFR0_EL1, DebugVer, IMP);
}

/* Trap the debug registers unless the guest owns them. */
static void pvm_setup_mdcr_debug(struct kvm_vcpu *vcpu)
{
	if (kvm_guest_owns_debug_regs(vcpu))
		vcpu->arch.mdcr_el2 &= ~MDCR_EL2_TDA;
	else
		vcpu->arch.mdcr_el2 |= MDCR_EL2_TDA;
}

/*
 * The guest's own MDSCR_EL1. It is the live register, except while HOST_OWNED
 * (OS lock), where the live register holds the masked external copy and the
 * guest's value is kept in the ctxt copy (ctxt_mdscr_el1()).
 */
static u64 pvm_guest_mdscr(struct kvm_vcpu *vcpu)
{
	if (kvm_host_owns_debug_regs(vcpu))
		return __vcpu_sys_reg(vcpu, MDSCR_EL1);
	return read_sysreg(mdscr_el1);
}

/* Ownership policy of kvm_vcpu_load_debug(); @mdscr is the guest's MDSCR_EL1. */
static int pvm_debug_owner(struct kvm_vcpu *vcpu, u64 mdscr)
{
	if (kvm_vcpu_os_lock_enabled(vcpu))
		return VCPU_DEBUG_HOST_OWNED;
	if (mdscr & (MDSCR_EL1_KDE | MDSCR_EL1_MDE))
		return VCPU_DEBUG_GUEST_OWNED;
	return VCPU_DEBUG_FREE;
}

/*
 * Move the live debug registers between owners mid-run. Trap handlers run
 * inside the __guest_enter() loop, past __debug_switch_to_guest() and the
 * MDCR_EL2 write in __activate_traps(), so the switch and the MDCR_EL2 update
 * have to be applied to hardware here.
 */
static void pkvm_debug_switch_owner(struct kvm_vcpu *vcpu, int old, int new)
{
	if (old != VCPU_DEBUG_FREE) {
		vcpu->arch.debug_owner = old;
		__debug_switch_to_host_common(vcpu);
	}

	vcpu->arch.debug_owner = new;
	if (new != VCPU_DEBUG_FREE)
		__debug_switch_to_guest_common(vcpu);

	/*
	 * Only crossing the HOST_OWNED boundary swaps which MDSCR_EL1 is live:
	 * entering it stashes the guest's value and loads the OS-lock-masked copy,
	 * leaving it restores the guest's value. FREE<->GUEST keeps whatever the
	 * guest just wrote.
	 */
	if (new == VCPU_DEBUG_HOST_OWNED) {
		__vcpu_assign_sys_reg(vcpu, MDSCR_EL1, read_sysreg(mdscr_el1));
		write_sysreg(vcpu->arch.external_mdscr_el1, mdscr_el1);
	} else if (old == VCPU_DEBUG_HOST_OWNED) {
		write_sysreg(__vcpu_sys_reg(vcpu, MDSCR_EL1), mdscr_el1);
	}

	pvm_setup_mdcr_debug(vcpu);
	write_sysreg(vcpu->arch.mdcr_el2, mdcr_el2);
}

/* Take guest ownership on the first trapped access while FREE. */
static void pkvm_debug_set_guest_ownership(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.debug_owner != VCPU_DEBUG_FREE)
		return;

	pkvm_debug_switch_owner(vcpu, VCPU_DEBUG_FREE, VCPU_DEBUG_GUEST_OWNED);
}

/*
 * Re-evaluate debug ownership at vcpu load, mirroring kvm_vcpu_load_debug().
 * Runs before the world switch, so it only updates the EL2-derived state that
 * __activate_traps() / __debug_switch_to_guest() / the common-state restore
 * then apply on entry; no live register writes here.
 */
void pkvm_vcpu_load_debug(struct kvm_vcpu *vcpu)
{
	int old_owner = vcpu->arch.debug_owner;
	u64 mdscr;

	if (pvm_debug_hidden(vcpu))
		return;

	mdscr = __vcpu_sys_reg(vcpu, MDSCR_EL1);
	vcpu->arch.debug_owner = pvm_debug_owner(vcpu, mdscr);

	if (kvm_host_owns_debug_regs(vcpu))
		vcpu->arch.external_mdscr_el1 =
			mdscr & ~(MDSCR_EL1_SS | MDSCR_EL1_MDE | MDSCR_EL1_KDE);

	if (vcpu->arch.debug_owner != old_owner)
		pvm_setup_mdcr_debug(vcpu);
}

/*
 * Trapped breakpoint/watchpoint register access, emulated into the guest's
 * inline debug state; taking guest ownership then loads it onto the CPU.
 */
static bool pvm_trap_dbg_wb_reg(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
				const struct sys_reg_desc *r)
{
	struct kvm_guest_debug_arch *dbg = &vcpu->arch.vcpu_debug_state;
	u64 *reg;

	if (pvm_debug_hidden(vcpu))
		return pvm_access_raz_wi(vcpu, p, r);

	switch (r->Op2) {
	case 0b100:
		reg = &dbg->dbg_bvr[r->CRm];
		break;
	case 0b101:
		reg = &dbg->dbg_bcr[r->CRm];
		break;
	case 0b110:
		reg = &dbg->dbg_wvr[r->CRm];
		break;
	case 0b111:
		reg = &dbg->dbg_wcr[r->CRm];
		break;
	default:
		return false;
	}

	if (p->is_write)
		*reg = p->regval;
	else
		p->regval = *reg;

	pkvm_debug_set_guest_ownership(vcpu);
	return true;
}

/*
 * MDSCR_EL1 is common-switched state and pKVM handles the trap before the
 * sysregs are saved, so a FREE guest's MDSCR is emulated against the live
 * register (a memory copy would be overwritten at exit). While HOST_OWNED the
 * live register holds the masked copy, so the guest's value lives in ctxt.
 */
static bool pvm_trap_mdscr(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	if (pvm_debug_hidden(vcpu))
		return pvm_access_raz_wi(vcpu, p, r);

	if (kvm_host_owns_debug_regs(vcpu)) {
		if (p->is_write) {
			/*
			 * Keep the guest's value in ctxt, and re-derive the masked
			 * live copy so its non-OS-lock bits take effect immediately.
			 */
			__vcpu_assign_sys_reg(vcpu, MDSCR_EL1, p->regval);
			vcpu->arch.external_mdscr_el1 = p->regval &
				~(MDSCR_EL1_SS | MDSCR_EL1_MDE | MDSCR_EL1_KDE);
			write_sysreg(vcpu->arch.external_mdscr_el1, mdscr_el1);
		} else {
			p->regval = __vcpu_sys_reg(vcpu, MDSCR_EL1);
		}
	} else if (p->is_write) {
		write_sysreg(p->regval, mdscr_el1);
	} else {
		p->regval = read_sysreg(mdscr_el1);
	}

	pkvm_debug_set_guest_ownership(vcpu);
	return true;
}

/*
 * On FEAT_FGT hardware MDCR_EL2.TDCC keeps trapping MDCCINT_EL1 after TDA is
 * dropped (pvm_init_traps_mdcr sets TDCC because a pVM never sees FGT), unlike
 * the breakpoint registers. Emulate where the switched debug state lives: on
 * the CPU while in use, in the ctxt copy otherwise.
 */
static bool pvm_trap_mdccint(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			     const struct sys_reg_desc *r)
{
	if (pvm_debug_hidden(vcpu))
		return pvm_access_raz_wi(vcpu, p, r);

	if (kvm_debug_regs_in_use(vcpu)) {
		if (p->is_write)
			write_sysreg(p->regval, mdccint_el1);
		else
			p->regval = read_sysreg(mdccint_el1);
	} else if (p->is_write) {
		__vcpu_assign_sys_reg(vcpu, MDCCINT_EL1, p->regval);
	} else {
		p->regval = __vcpu_sys_reg(vcpu, MDCCINT_EL1);
	}

	pkvm_debug_set_guest_ownership(vcpu);
	return true;
}

/* OS Lock: emulate OSLAR_EL1.OSLK, then re-evaluate ownership. */
static bool pvm_trap_oslar(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	int old_owner = vcpu->arch.debug_owner;
	int new_owner;
	u64 mdscr;

	if (pvm_debug_hidden(vcpu))
		return pvm_access_raz_wi(vcpu, p, r);

	/* OSLAR_EL1 is write-only. */
	if (!p->is_write) {
		inject_undef64(vcpu);
		return false;
	}

	mdscr = pvm_guest_mdscr(vcpu);

	if (p->regval & OSLAR_EL1_OSLK)
		__vcpu_rmw_sys_reg(vcpu, OSLSR_EL1, |=, OSLSR_EL1_OSLK);
	else
		__vcpu_rmw_sys_reg(vcpu, OSLSR_EL1, &=, ~OSLSR_EL1_OSLK);

	new_owner = pvm_debug_owner(vcpu, mdscr);
	if (new_owner == VCPU_DEBUG_HOST_OWNED)
		vcpu->arch.external_mdscr_el1 =
			mdscr & ~(MDSCR_EL1_SS | MDSCR_EL1_MDE | MDSCR_EL1_KDE);

	if (new_owner != old_owner)
		pkvm_debug_switch_owner(vcpu, old_owner, new_owner);
	return true;
}

/* OSLSR_EL1: OS Lock is implemented; OSLM reads 0b10. */
static bool pvm_trap_oslsr(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	if (pvm_debug_hidden(vcpu))
		return pvm_access_raz_wi(vcpu, p, r);

	if (p->is_write)
		return false;

	p->regval = OSLSR_EL1_OSLM_IMPLEMENTED |
		    (__vcpu_sys_reg(vcpu, OSLSR_EL1) & OSLSR_EL1_OSLK);
	return true;
}

/*
 * DBGAUTHSTATUS_EL1 is read-only: report the live authentication state on read
 * and ignore writes, as the host's trap_dbgauthstatus_el1() does.
 */
static bool pvm_trap_dbgauthstatus(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
				   const struct sys_reg_desc *r)
{
	if (p->is_write)
		return true;

	p->regval = read_sysreg(dbgauthstatus_el1);
	return true;
}

/* Mark the specified system register as an AArch32 feature id register. */
#define AARCH32(REG) { SYS_DESC(REG), .access = pvm_access_id_aarch32 }

/* Mark the specified system register as an AArch64 feature id register. */
#define AARCH64(REG) { SYS_DESC(REG), .access = pvm_access_id_aarch64 }

/*
 * sys_reg_desc initialiser for architecturally unallocated cpufeature ID
 * register with encoding Op0=3, Op1=0, CRn=0, CRm=crm, Op2=op2
 * (1 <= crm < 8, 0 <= Op2 < 8).
 */
#define ID_UNALLOCATED(crm, op2) {			\
	Op0(3), Op1(0), CRn(0), CRm(crm), Op2(op2),	\
	.access = pvm_access_unallocated,		\
}

/*
 * sys_reg_desc initialiser for known ID registers that we hide from guests.
 * For now, these are handled just like unallocated ID regs.
 */
#define ID_HIDDEN(REG) {			\
	SYS_DESC(REG),				\
	.access = pvm_access_unallocated,	\
}

/* Mark the specified system register as Read-As-Zero/Write-Ignored */
#define RAZ_WI(REG) { SYS_DESC(REG), .access = pvm_access_raz_wi }

/* Breakpoint/watchpoint value and control registers for index n. */
#define DBG_WB(n)							\
	{ SYS_DESC(SYS_DBGBVRn_EL1(n)), .access = pvm_trap_dbg_wb_reg },	\
	{ SYS_DESC(SYS_DBGBCRn_EL1(n)), .access = pvm_trap_dbg_wb_reg },	\
	{ SYS_DESC(SYS_DBGWVRn_EL1(n)), .access = pvm_trap_dbg_wb_reg },	\
	{ SYS_DESC(SYS_DBGWCRn_EL1(n)), .access = pvm_trap_dbg_wb_reg }

/* Mark the specified system register as not being handled in hyp. */
#define HOST_HANDLED(REG) { SYS_DESC(REG), .access = NULL }

/*
 * Architected system registers.
 * Important: Must be sorted ascending by Op0, Op1, CRn, CRm, Op2
 *
 * NOTE: Anything not explicitly listed here is *restricted by default*, i.e.,
 * it will lead to injecting an exception into the guest.
 */
static const struct sys_reg_desc pvm_sys_reg_descs[] = {
	/* Cache maintenance by set/way operations are restricted. */

	/*
	 * Self-hosted debug: breakpoint/watchpoint registers and the debug
	 * control registers are emulated with lazy guest ownership. The handlers
	 * fall back to RAZ/WI while ID_AA64DFR0_EL1 hides DebugVer.
	 */
	DBG_WB(0),
	DBG_WB(1),
	{ SYS_DESC(SYS_MDCCINT_EL1), .access = pvm_trap_mdccint },
	{ SYS_DESC(SYS_MDSCR_EL1), .access = pvm_trap_mdscr },
	DBG_WB(2),
	DBG_WB(3),
	DBG_WB(4),
	DBG_WB(5),
	DBG_WB(6),
	DBG_WB(7),
	DBG_WB(8),
	DBG_WB(9),
	DBG_WB(10),
	DBG_WB(11),
	DBG_WB(12),
	DBG_WB(13),
	DBG_WB(14),
	DBG_WB(15),
	RAZ_WI(SYS_MDRAR_EL1),
	{ SYS_DESC(SYS_OSLAR_EL1), .access = pvm_trap_oslar },
	{ SYS_DESC(SYS_OSLSR_EL1), .access = pvm_trap_oslsr },
	RAZ_WI(SYS_OSDLR_EL1),
	/*
	 * Auxiliary debug registers trapped by MDCR_EL2.{TDA,TDCC,TDOSA}, RAZ/WI
	 * as the host does; DBGAUTHSTATUS_EL1 reports the live auth state.
	 */
	RAZ_WI(SYS_DBGPRCR_EL1),
	RAZ_WI(SYS_DBGCLAIMSET_EL1),
	RAZ_WI(SYS_DBGCLAIMCLR_EL1),
	{ SYS_DESC(SYS_DBGAUTHSTATUS_EL1), .access = pvm_trap_dbgauthstatus },
	RAZ_WI(SYS_MDCCSR_EL0),
	RAZ_WI(SYS_DBGDTR_EL0),
	RAZ_WI(SYS_DBGDTRTX_EL0),

	/* Group 1 ID registers */
	HOST_HANDLED(SYS_REVIDR_EL1),

	/* AArch64 mappings of the AArch32 ID registers */
	/* CRm=1 */
	AARCH32(SYS_ID_PFR0_EL1),
	AARCH32(SYS_ID_PFR1_EL1),
	AARCH32(SYS_ID_DFR0_EL1),
	AARCH32(SYS_ID_AFR0_EL1),
	AARCH32(SYS_ID_MMFR0_EL1),
	AARCH32(SYS_ID_MMFR1_EL1),
	AARCH32(SYS_ID_MMFR2_EL1),
	AARCH32(SYS_ID_MMFR3_EL1),

	/* CRm=2 */
	AARCH32(SYS_ID_ISAR0_EL1),
	AARCH32(SYS_ID_ISAR1_EL1),
	AARCH32(SYS_ID_ISAR2_EL1),
	AARCH32(SYS_ID_ISAR3_EL1),
	AARCH32(SYS_ID_ISAR4_EL1),
	AARCH32(SYS_ID_ISAR5_EL1),
	AARCH32(SYS_ID_MMFR4_EL1),
	AARCH32(SYS_ID_ISAR6_EL1),

	/* CRm=3 */
	AARCH32(SYS_MVFR0_EL1),
	AARCH32(SYS_MVFR1_EL1),
	AARCH32(SYS_MVFR2_EL1),
	ID_UNALLOCATED(3,3),
	AARCH32(SYS_ID_PFR2_EL1),
	AARCH32(SYS_ID_DFR1_EL1),
	AARCH32(SYS_ID_MMFR5_EL1),
	ID_UNALLOCATED(3,7),

	/* AArch64 ID registers */
	/* CRm=4 */
	AARCH64(SYS_ID_AA64PFR0_EL1),
	AARCH64(SYS_ID_AA64PFR1_EL1),
	AARCH64(SYS_ID_AA64PFR2_EL1),
	ID_UNALLOCATED(4,3),
	AARCH64(SYS_ID_AA64ZFR0_EL1),
	ID_HIDDEN(SYS_ID_AA64SMFR0_EL1),
	ID_UNALLOCATED(4,6),
	ID_UNALLOCATED(4,7),
	AARCH64(SYS_ID_AA64DFR0_EL1),
	AARCH64(SYS_ID_AA64DFR1_EL1),
	ID_UNALLOCATED(5,2),
	ID_UNALLOCATED(5,3),
	ID_HIDDEN(SYS_ID_AA64AFR0_EL1),
	ID_HIDDEN(SYS_ID_AA64AFR1_EL1),
	ID_UNALLOCATED(5,6),
	ID_UNALLOCATED(5,7),
	AARCH64(SYS_ID_AA64ISAR0_EL1),
	AARCH64(SYS_ID_AA64ISAR1_EL1),
	AARCH64(SYS_ID_AA64ISAR2_EL1),
	ID_UNALLOCATED(6,3),
	ID_UNALLOCATED(6,4),
	ID_UNALLOCATED(6,5),
	ID_UNALLOCATED(6,6),
	ID_UNALLOCATED(6,7),
	AARCH64(SYS_ID_AA64MMFR0_EL1),
	AARCH64(SYS_ID_AA64MMFR1_EL1),
	AARCH64(SYS_ID_AA64MMFR2_EL1),
	AARCH64(SYS_ID_AA64MMFR3_EL1),
	ID_UNALLOCATED(7,4),
	ID_UNALLOCATED(7,5),
	ID_UNALLOCATED(7,6),
	ID_UNALLOCATED(7,7),

	HOST_HANDLED(SYS_ICC_PMR_EL1),

	RAZ_WI(SYS_ERRIDR_EL1),
	RAZ_WI(SYS_ERRSELR_EL1),
	RAZ_WI(SYS_ERXFR_EL1),
	RAZ_WI(SYS_ERXCTLR_EL1),
	RAZ_WI(SYS_ERXSTATUS_EL1),
	RAZ_WI(SYS_ERXADDR_EL1),
	RAZ_WI(SYS_ERXMISC0_EL1),
	RAZ_WI(SYS_ERXMISC1_EL1),

	/* Performance Monitoring Registers are restricted. */

	/* Limited Ordering Regions Registers are restricted. */

	HOST_HANDLED(SYS_ICC_DIR_EL1),
	HOST_HANDLED(SYS_ICC_RPR_EL1),
	HOST_HANDLED(SYS_ICC_SGI1R_EL1),
	HOST_HANDLED(SYS_ICC_ASGI1R_EL1),
	HOST_HANDLED(SYS_ICC_SGI0R_EL1),
	HOST_HANDLED(SYS_ICC_CTLR_EL1),
	{ SYS_DESC(SYS_ICC_SRE_EL1), .access = pvm_gic_read_sre, },

	HOST_HANDLED(SYS_CCSIDR_EL1),
	HOST_HANDLED(SYS_CLIDR_EL1),
	HOST_HANDLED(SYS_AIDR_EL1),
	HOST_HANDLED(SYS_CSSELR_EL1),
	HOST_HANDLED(SYS_CTR_EL0),

	/* Performance Monitoring Registers are restricted. */

	/* Activity Monitoring Registers are restricted. */

	HOST_HANDLED(SYS_CNTP_TVAL_EL0),
	HOST_HANDLED(SYS_CNTP_CTL_EL0),
	HOST_HANDLED(SYS_CNTP_CVAL_EL0),

	/* Performance Monitoring Registers are restricted. */
};

/* A structure to track reset values for system registers in protected vcpus. */
struct sys_reg_desc_reset {
	/* Index into sys_reg[]. */
	int reg;

	/* Reset function. */
	void (*reset)(struct kvm_vcpu *, const struct sys_reg_desc_reset *);

	/* Reset value. */
	u64 value;
};

static void reset_actlr(struct kvm_vcpu *vcpu, const struct sys_reg_desc_reset *r)
{
	__vcpu_assign_sys_reg(vcpu, r->reg, read_sysreg(actlr_el1));
}

static void reset_amair_el1(struct kvm_vcpu *vcpu, const struct sys_reg_desc_reset *r)
{
	__vcpu_assign_sys_reg(vcpu, r->reg, read_sysreg(amair_el1));
}

static void reset_mpidr(struct kvm_vcpu *vcpu, const struct sys_reg_desc_reset *r)
{
	__vcpu_assign_sys_reg(vcpu, r->reg, calculate_mpidr(vcpu));
}

static void reset_value(struct kvm_vcpu *vcpu, const struct sys_reg_desc_reset *r)
{
	__vcpu_assign_sys_reg(vcpu, r->reg, r->value);
}

/* Specify the register's reset value. */
#define RESET_VAL(REG, RESET_VAL) {  REG, reset_value, RESET_VAL }

#define RESET_ZERO(REG) RESET_VAL(REG, 0)

#define RESET_UNKNOWN(REG) RESET_VAL(REG, 0x1de7ec7edbadc0deULL)

/* Specify a function that calculates the register's reset value. */
#define RESET_FUNC(REG, RESET_FUNC) {  REG, RESET_FUNC, 0 }

/*
 * Architected system registers reset values for Protected VMs.
 * Important: Must be sorted ascending by REG (index into sys_reg[])
 */
static const struct sys_reg_desc_reset pvm_sys_reg_reset_vals[] = {
	RESET_FUNC(MPIDR_EL1, reset_mpidr),
	RESET_UNKNOWN(TPIDR_EL0),
	RESET_UNKNOWN(TPIDRRO_EL0),
	RESET_UNKNOWN(TPIDR_EL1),
	RESET_ZERO(CNTKCTL_EL1),
	RESET_UNKNOWN(PAR_EL1),
	RESET_ZERO(MDCCINT_EL1),
	RESET_ZERO(OSLSR_EL1),
	RESET_ZERO(DISR_EL1),
	RESET_ZERO(PMCCFILTR_EL0),
	RESET_ZERO(PMUSERENR_EL0),
	RESET_ZERO(CPACR_EL1),
	RESET_VAL(CONTEXTIDR_EL1, 0x00000000dbadc0deULL),
	RESET_VAL(SCTLR_EL1, 0x00C50078ULL),
	RESET_FUNC(ACTLR_EL1, reset_actlr),
	RESET_ZERO(TCR_EL1),
	RESET_UNKNOWN(AFSR0_EL1),
	RESET_UNKNOWN(AFSR1_EL1),
	RESET_UNKNOWN(ESR_EL1),
	RESET_UNKNOWN(MAIR_EL1),
	RESET_FUNC(AMAIR_EL1, reset_amair_el1),
	RESET_ZERO(MDSCR_EL1),
	RESET_ZERO(ZCR_EL1),
	RESET_UNKNOWN(TTBR0_EL1),
	RESET_UNKNOWN(TTBR1_EL1),
	RESET_UNKNOWN(FAR_EL1),
	RESET_VAL(VBAR_EL1, 0x1de7ec7edbadc000ULL),
	RESET_UNKNOWN(PIRE0_EL1),
	RESET_UNKNOWN(PIR_EL1),
};

/*
 * Sets system registers to reset value
 *
 * This function finds the right entry and sets the registers on the protected
 * vcpu to their architecturally defined reset values.
 */
void kvm_reset_pvm_sys_regs(struct kvm_vcpu *vcpu)
{
	unsigned long i;

	for (i = 0; i < ARRAY_SIZE(pvm_sys_reg_reset_vals); i++) {
		const struct sys_reg_desc_reset *r = &pvm_sys_reg_reset_vals[i];

		r->reset(vcpu, r);
	}
}

/*
 * Initializes feature registers for protected vms.
 */
void kvm_init_pvm_id_regs(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_arch *ka = &kvm->arch;
	u32 r;

	if (test_bit(KVM_ARCH_FLAG_ID_REGS_INITIALIZED, &kvm->arch.flags))
		return;

	/*
	 * Initialize only AArch64 id registers since AArch32 isn't supported
	 * for protected VMs.
	 */
	for (r = sys_reg(3, 0, 0, 4, 0); r <= sys_reg(3, 0, 0, 7, 7); r += sys_reg(0, 0, 0, 0, 1))
		ka->id_regs[IDREG_IDX(r)] = pvm_calc_id_reg(vcpu, r);

	set_bit(KVM_ARCH_FLAG_ID_REGS_INITIALIZED, &kvm->arch.flags);
}

/*
 * Checks that the sysreg tables are unique and in-order.
 *
 * Returns 0 if the table is consistent, or 1 otherwise.
 */
int kvm_check_pvm_sysreg_table(void)
{
	unsigned int i;

	for (i = 1; i < ARRAY_SIZE(pvm_sys_reg_descs); i++) {
		if (cmp_sys_reg(&pvm_sys_reg_descs[i-1], &pvm_sys_reg_descs[i]) >= 0)
			return 1;
	}

	for (i = 1; i < ARRAY_SIZE(pvm_sys_reg_reset_vals); i++) {
		if (pvm_sys_reg_reset_vals[i-1].reg >= pvm_sys_reg_reset_vals[i].reg)
			return 1;
	}

	return 0;
}

/*
 * Handler for protected VM MSR, MRS or System instruction execution.
 *
 * Returns true if the hypervisor has handled the exit, and control should go
 * back to the guest, or false if it hasn't, to be handled by the host.
 */
bool kvm_handle_pvm_sysreg(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	const struct sys_reg_desc *r;
	struct sys_reg_params params;
	unsigned long esr = kvm_vcpu_get_esr(vcpu);
	int Rt = kvm_vcpu_sys_get_rt(vcpu);

	params = esr_sys64_to_params(esr);
	params.regval = vcpu_get_reg(vcpu, Rt);

	r = find_reg(&params, pvm_sys_reg_descs, ARRAY_SIZE(pvm_sys_reg_descs));

	/* Undefined (RESTRICTED). */
	if (r == NULL) {
		inject_undef64(vcpu);
		return true;
	}

	/* Handled by the host (HOST_HANDLED) */
	if (r->access == NULL)
		return false;

	/* Handled by hyp: skip instruction if instructed to do so. */
	if (r->access(vcpu, &params, r))
		__kvm_skip_instr(vcpu);

	if (!params.is_write)
		vcpu_set_reg(vcpu, Rt, params.regval);

	return true;
}

/*
 * Handler for protected VM restricted exceptions.
 *
 * Inject an undefined exception into the guest and return true to indicate that
 * the hypervisor has handled the exit, and control should go back to the guest.
 */
bool kvm_handle_pvm_restricted(struct kvm_vcpu *vcpu, u64 *exit_code)
{
	inject_undef64(vcpu);
	return true;
}
