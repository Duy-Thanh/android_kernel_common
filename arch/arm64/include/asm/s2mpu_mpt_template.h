/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 - Google LLC
 */

/*
 * Template for S2MPU functions.
 * Caller should define:
 * ARG_PROT_BITS: Number of bits for a page in SMPT.
 * ARG_ACCESS_SHIFT: Shift of protection bits in SMPT.
 * ARG_GRAN_MASK: Granularity mask in L1 table.
 * ARG_LUT_PROT: Look up table for protection dword.
 * T_FUNC(x): Macro function to instantiate/call a function.
 */

#define SMPT_NUM_TO_BYTE(x)		((x) / SMPT_GRAN / SMPT_ELEMS_PER_BYTE(ARG_PROT_BITS))
#define BYTE_TO_SMPT_INDEX(x)	((x) / SMPT_WORD_BYTE_RANGE(ARG_PROT_BITS))

static inline int T_FUNC(pte_from_addr_smpt)(u32 *smpt, u64 addr)
{
	u32 word_idx, idx, pte, val;

	word_idx = BYTE_TO_SMPT_INDEX(addr);
	val = READ_ONCE(smpt[word_idx]);
	idx  = (addr / SMPT_GRAN) % SMPT_ELEMS_PER_WORD(ARG_PROT_BITS);

	pte  = (val >> (idx * ARG_PROT_BITS)) & ((1 << ARG_PROT_BITS)-1);
	return pte;
}

/* Set protection bits of SMPT in a given range without using memset. */
static void T_FUNC(__set_smpt_range_slow)(u32 *smpt, size_t start_gb_byte,
					  size_t end_gb_byte, enum mpt_prot prot)
{
	size_t i, start_word_byte, end_word_byte, word_idx, first_elem, last_elem;
	u32 val;

	/* Iterate over u32 words. */
	start_word_byte = start_gb_byte;
	while (start_word_byte < end_gb_byte) {
		/* Determine the range of bytes covered by this word. */
		word_idx = BYTE_TO_SMPT_INDEX(start_word_byte);
		end_word_byte = min(
			ALIGN(start_word_byte + 1, SMPT_WORD_BYTE_RANGE(ARG_PROT_BITS)),
			end_gb_byte);

		/* Identify protection bit offsets within the word. */
		first_elem = (start_word_byte / SMPT_GRAN) % SMPT_ELEMS_PER_WORD(ARG_PROT_BITS);
		last_elem =
			((end_word_byte - 1) / SMPT_GRAN) % SMPT_ELEMS_PER_WORD(ARG_PROT_BITS);

		/* Modify the corresponding word. */
		val = READ_ONCE(smpt[word_idx]);
		for (i = first_elem; i <= last_elem; i++) {
			val &= ~(MPT_PROT_MASK << (i * ARG_PROT_BITS + ARG_ACCESS_SHIFT));
			val |= prot << (i * ARG_PROT_BITS + ARG_ACCESS_SHIFT);
		}
		WRITE_ONCE(smpt[word_idx], val);

		start_word_byte = end_word_byte;
	}
}

/* Set protection bits of SMPT in a given range. */
static void T_FUNC(__set_smpt_range)(u32 *smpt, size_t start_gb_byte,
				     size_t end_gb_byte, enum mpt_prot prot)
{
	size_t interlude_start, interlude_end, interlude_bytes, word_idx;

	char prot_byte = (char)ARG_LUT_PROT[prot];

	if (start_gb_byte >= end_gb_byte)
		return;

	/* Check if range spans at least one full u32 word. */
	interlude_start = ALIGN(start_gb_byte, SMPT_WORD_BYTE_RANGE(ARG_PROT_BITS));
	interlude_end = ALIGN_DOWN(end_gb_byte, SMPT_WORD_BYTE_RANGE(ARG_PROT_BITS));

	/*
	 * If not, fall back to editing bits in the given range.
	 * sets bit for PTEs that are in less than 32 bits (can't be done by memset)
	 */
	if (interlude_start >= interlude_end) {
		T_FUNC(__set_smpt_range_slow)(smpt, start_gb_byte, end_gb_byte, prot);
		return;
	}

	/* Use bit-editing for prologue/epilogue, memset for interlude. */
	word_idx = BYTE_TO_SMPT_INDEX(interlude_start);
	interlude_bytes = SMPT_NUM_TO_BYTE(interlude_end - interlude_start);

	/*
	 * These are pages in the start and at then end that are
	 * not part of full 32 bit SMPT word.
	 */
	T_FUNC(__set_smpt_range_slow)(smpt, start_gb_byte, interlude_start, prot);
	memset(&smpt[word_idx], prot_byte, interlude_bytes);
	T_FUNC(__set_smpt_range_slow)(smpt, interlude_end, end_gb_byte, prot);
}

/* Returns true if all SMPT protection bits match 'prot'. */
static bool T_FUNC(__is_smpt_uniform)(u32 *smpt, enum mpt_prot prot)
{
	size_t i;
	u64 *doublewords = (u64 *)smpt;

	for (i = 0; i < SMPT_NUM_WORDS(ARG_PROT_BITS) / 2; i++) {
		if (doublewords[i] != ARG_LUT_PROT[prot])
			return false;
	}
	return true;
}

/*
 * Set protection bits of FMPT/SMPT in a given range.
 * Returns flags specifying whether L1/L2 changes need to be made visible
 * to the device.
 */
static void T_FUNC(__set_fmpt_range)(struct fmpt *fmpt, size_t start_gb_byte,
				     size_t end_gb_byte, enum mpt_prot prot)
{
	if (start_gb_byte == 0 && end_gb_byte >= SZ_1G) {
		/* Update covers the entire GB region. */
		if (fmpt->gran_1g && fmpt->prot == prot) {
			fmpt->flags = 0;
			return;
		}

		fmpt->gran_1g = true;
		fmpt->prot = prot;
		fmpt->flags = MPT_UPDATE_L1;
		return;
	}

	if (fmpt->gran_1g) {
		/* GB region currently uses 1G mapping. */
		if (fmpt->prot == prot) {
			fmpt->flags = 0;
			return;
		}

		/*
		 * Range has different mapping than the rest of the GB.
		 * Convert to PAGE_SIZE mapping.
		 */
		fmpt->gran_1g = false;
		T_FUNC(__set_smpt_range)(fmpt->smpt, 0, start_gb_byte, fmpt->prot);
		T_FUNC(__set_smpt_range)(fmpt->smpt, start_gb_byte, end_gb_byte, prot);
		T_FUNC(__set_smpt_range)(fmpt->smpt, end_gb_byte, SZ_1G, fmpt->prot);
		fmpt->flags = MPT_UPDATE_L1 | MPT_UPDATE_L2;
		return;
	}

	/* GB region currently uses PAGE_SIZE mapping. */
	T_FUNC(__set_smpt_range)(fmpt->smpt, start_gb_byte, end_gb_byte, prot);

	/* Check if the entire GB region has the same prot bits. */
	if (!T_FUNC(__is_smpt_uniform)(fmpt->smpt, prot)) {
		fmpt->flags = MPT_UPDATE_L2;
		return;
	}

	fmpt->gran_1g = true;
	fmpt->prot = prot;
	fmpt->flags = MPT_UPDATE_L1;
}
/* Don't export these functions to selftests. */
#ifndef KSTM_MODULE_GLOBALS

static u32 T_FUNC(smpt_size)(void)
{
	return SMPT_SIZE(ARG_PROT_BITS);
}

static void T_FUNC(__set_l1entry_attr_with_fmpt)(void *dev_va, unsigned int gb,
						 unsigned int vid, struct fmpt *fmpt)
{
	if (fmpt->gran_1g) {
		__set_l1entry_attr_with_prot(dev_va, gb, vid, fmpt->prot);
	} else {
		/* Order against writes to the SMPT. */
		writel(ARG_GRAN_MASK | L1ENTRY_ATTR_L2TABLE_EN,
		       dev_va + REG_NS_L1ENTRY_ATTR(vid, gb));
	}
}

static void T_FUNC(init_with_mpt)(void *dev_va, struct mpt *mpt)
{
	unsigned int gb, vid;
	struct fmpt *fmpt;

	for_each_gb_and_vid(gb, vid) {
		fmpt = &mpt->fmpt[gb];
		__set_l1entry_l2table_addr(dev_va, gb, vid, __hyp_pa(fmpt->smpt));
		T_FUNC(__set_l1entry_attr_with_fmpt)(dev_va, gb, vid, fmpt);
	}
}

static void T_FUNC(apply_range)(void *dev_va, struct mpt *mpt, u32 first_gb, u32 last_gb)
{
	unsigned int gb, vid;
	struct fmpt *fmpt;

	for_each_gb_in_range(gb, first_gb, last_gb) {
		fmpt = &mpt->fmpt[gb];
		if (fmpt->flags & MPT_UPDATE_L1) {
			for_each_vid(vid)
				T_FUNC(__set_l1entry_attr_with_fmpt)(dev_va, gb, vid, fmpt);
		}
	}
}

static void T_FUNC(prepare_range)(struct mpt *mpt, phys_addr_t first_byte,
				  phys_addr_t last_byte, enum mpt_prot prot)
{
	unsigned int first_gb = first_byte / SZ_1G;
	unsigned int last_gb = last_byte / SZ_1G;
	size_t start_gb_byte, end_gb_byte;
	unsigned int gb;
	struct fmpt *fmpt;

	for_each_gb_in_range(gb, first_gb, last_gb) {
		fmpt = &mpt->fmpt[gb];
		start_gb_byte = (gb == first_gb) ? first_byte % SZ_1G : 0;
		end_gb_byte = (gb == last_gb) ? (last_byte % SZ_1G) + 1 : SZ_1G;

		T_FUNC(__set_fmpt_range)(fmpt, start_gb_byte, end_gb_byte, prot);

		if (fmpt->flags & MPT_UPDATE_L2)
			kvm_flush_dcache_to_poc(fmpt->smpt, T_FUNC(smpt_size)());
	}
}
#endif /* KSTM_MODULE_GLOBALS */

#undef SMPT_NUM_TO_BYTE
#undef BYTE_TO_SMPT_INDEX
