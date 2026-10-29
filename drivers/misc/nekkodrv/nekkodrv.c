// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026
 * Author: Nguyen Duy Thanh <nekkochan0x0007@proton.me>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/arm-smccc.h>

#include "curve25519-lite.h"
#include "siphash-lite.h"
#include "prng.h"

#define DRIVER_NAME "nekkodrv"
#define DRIVER_VERSION "2026.10.28"
#define DRIVER_AUTHOR "Nguyen Duy Thanh <nekkochan0x0007@proton.me>"
#define DRIVER_DESC "NekkoDrv kernel module for custom hypervisor"

#define ARM_SMCCC_VENDOR_HYP_KVM_AUTH 0x1337
#define ARM_SMCCC_VENDOR_HYP_KVM_AUTH_FUNC_ID                                 \
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_64,              \
			   ARM_SMCCC_OWNER_VENDOR_HYP,                         \
			   ARM_SMCCC_VENDOR_HYP_KVM_AUTH)

// Opcode cho HCALL sub-commands
#define CMD_ECDH_EXCHANGE 1338
#define CMD_VERIFY_RESP 1339
#define CMD_PROTECTED_OP 1340

static inline uint64_t get_hardware_entropy_u64_el1(void)
{
	uint64_t cntpct;
	uint64_t cntvct;
	uint64_t sp_val;

	asm volatile("mrs %0, cntpct_el0" : "=r"(cntpct));
	asm volatile("mrs %0, cntvct_el0" : "=r"(cntvct));
	asm volatile("mov %0, sp" : "=r"(sp_val));

	uint64_t entropy = cntpct;
	entropy ^= ROTL64(cntvct, 17);
	entropy ^= ROTL64(sp_val, 31);

	return entropy;
}

static void generate_el1_ephemeral_privkey(uint8_t privkey[32])
{
	struct chacha20_pure_ctx ctx;
	uint32_t seed_key[8];

	for (int i = 0; i < 8; i += 2) {
		uint64_t ent = get_hardware_entropy_u64_el1();
		seed_key[i] = (uint32_t)ent;
		seed_key[i + 1] = (uint32_t)(ent >> 32);

		for (volatile int d = 0; d < 50; d++)
			;
	}

	uint32_t nonce[3] = { (uint32_t)get_hardware_entropy_u64_el1(),
			      (uint32_t)get_hardware_entropy_u64_el1(),
			      (uint32_t)get_hardware_entropy_u64_el1() };

	chacha20_init(&ctx, seed_key, nonce, 1);

	// FIX 1: Thêm mảng block và gọi chacha20_block()
	uint32_t block[16];
	chacha20_block(&ctx, block);

	uint32_t *p32 = (uint32_t *)privkey;
	for (int i = 0; i < 8; i++) {
		p32[i] = block[i];
	}

	// Clamp private key cho X25519
	privkey[0] &= 248;
	privkey[31] &= 127;
	privkey[31] |= 64;
}

// Module Initialization
static int __init nekkodrv_module_init(void)
{
	// FIX 3: Dùng struct arm_smccc_1_2_regs để lấy đầy đủ a0..a17
	struct arm_smccc_1_2_regs args = { 0 };
	struct arm_smccc_1_2_regs res = { 0 };

	uint8_t el1_privkey[32];
	uint8_t el1_pubkey[32];
	uint8_t el2_pubkey[32];
	uint8_t shared_secret[32];

	pr_emerg("[+] NEKKODRV: Starting up...\n");
	pr_emerg("[+] NEKKODRV: Handshaking...\n");

	generate_el1_ephemeral_privkey(el1_privkey);
	x25519_scalarmult(el1_pubkey, el1_privkey, NULL);

	uint64_t *p_el1 = (uint64_t *)el1_pubkey;

	args.a0 = ARM_SMCCC_VENDOR_HYP_KVM_AUTH_FUNC_ID;
	args.a1 = CMD_ECDH_EXCHANGE; // FIX 2: Sửa tên macro
	args.a2 = p_el1[0];
	args.a3 = p_el1[1];
	args.a4 = p_el1[2];
	args.a5 = p_el1[3];

	arm_smccc_1_2_hvc(&args, &res);

	if (res.a0 != SMCCC_RET_SUCCESS) {
		pr_emerg(
			"[-] NEKKODRV: CMD_ECDH_EXCHANGE failed with status: 0x%lx\n",
			res.a0);
		return -EPERM;
	}

	uint64_t active_nonce = res.a1;
	uint64_t *p_el2 = (uint64_t *)el2_pubkey;
	p_el2[0] = res.a2;
	p_el2[1] = res.a3;
	p_el2[2] = res.a4; // Nguồn gốc res.a4 chuẩn từ arm_smccc_1_2_regs
	p_el2[3] = res.a5; // Nguồn gốc res.a5 chuẩn từ arm_smccc_1_2_regs

	// FIX 4: Sửa format specifier từ %llx thành %lx để khớp kiểu unsigned long
	pr_emerg(
		"[+] NEKKODRV: Received Nonce=0x%lx & EL2 Public Key successfully\n",
		active_nonce);

	x25519_scalarmult(shared_secret, el1_privkey, el2_pubkey);

	for (int i = 0; i < 32; i++)
		el1_privkey[i] = 0;

	// Derive key
	uint64_t *s64 = (uint64_t *)shared_secret;
	uint64_t derived_session_key_0 = s64[0] ^ s64[1];
	uint64_t derived_session_key_1 = s64[2] ^ s64[3];

	// Calculate session key
	uint64_t client_response = siphash24_single_u64(
		active_nonce, derived_session_key_0, derived_session_key_1);

	// Verify response
	memset(&args, 0, sizeof(args));
	args.a0 = ARM_SMCCC_VENDOR_HYP_KVM_AUTH_FUNC_ID;
	args.a1 = CMD_VERIFY_RESP;
	args.a2 = client_response;

	arm_smccc_1_2_hvc(&args, &res);

	if (res.a0 != SMCCC_RET_SUCCESS) {
		pr_emerg("[-] NEKKODRV: Response Verification Failed at EL2!\n");
		return -EACCES;
	}

	uint64_t session_token = res.a1;
	pr_emerg(
		"[+] NEKKODRV: AUTHENTICATED SUCCESS! Granted Session Token: 0x%lx\n",
		session_token);

	uint64_t test_payload = 0x1122334455667788ULL;
	memset(&args, 0, sizeof(args));
	args.a0 = ARM_SMCCC_VENDOR_HYP_KVM_AUTH_FUNC_ID;
	args.a1 = CMD_PROTECTED_OP;
	args.a2 = session_token;
	args.a3 = test_payload;

	arm_smccc_1_2_hvc(&args, &res);

	if (res.a0 == SMCCC_RET_SUCCESS) {
		pr_emerg(
			"[+] NEKKODRV: PROTECTED OP SUCCESS! Input Payload: 0x%llx -> EL2 Output: 0x%lx\n",
			test_payload, res.a1);
	} else {
		pr_emerg("[-] NEKKODRV: PROTECTED OP REJECTED BY EL2!\n");
		return -EPERM;
	}

	return 0;
}

static void __exit nekkodrv_module_exit(void)
{
	pr_emerg("[-] NEKKODRV: Unloading module\n");
}

module_init(nekkodrv_module_init);
module_exit(nekkodrv_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);