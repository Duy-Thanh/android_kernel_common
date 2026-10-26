// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026
 * Author: Nguyen Duy Thanh <nekkochan0x0007@proton.me>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/arm-smccc.h>
#include "siphash-lite.h"
#include "prng.h"

#define ARM_SMCCC_VENDOR_HYP_KVM_AUTH         0x1337
#define ARM_SMCCC_VENDOR_HYP_KVM_AUTH_FUNC_ID               \
        ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,             \
                           ARM_SMCCC_SMC_64,                \
                           ARM_SMCCC_OWNER_VENDOR_HYP,      \
                           ARM_SMCCC_VENDOR_HYP_KVM_AUTH)

// Opcode for HCALL sub-commands
#define CMD_GET_CHALLENGE       1338
#define CMD_VERIFY_RESP         1339
#define CMD_PROTECTED_OP        1340

static uint64_t shared_key_0 = 0;
static uint64_t shared_key_1 = 0;
static uint64_t my_session_token = 0;

static struct chacha20_pure_ctx chacha_ctx;

static void init_chacha_prng(void) {
    uint64_t sp_reg;
    uint64_t x30_reg;

    asm volatile("mov %0, sp" : "=r" (sp_reg));
    asm volatile("mov %0, x30" : "=r" (x30_reg));

    uint32_t seed_key[8] =
        {
            (uint32_t)sp_reg, (uint32_t)(sp_reg >> 32),
            (uint32_t)x30_reg, (uint32_t)(x30_reg >> 32),
            0xDEADBEEF, 0xCAFEBABE, 0x13371337, 0x90909090
        };
    uint32_t nonce[3] = {0x01020304, 0x05060708, 0x090A0B0C};

    chacha20_init(&chacha_ctx, seed_key, nonce, 1);
}

static void derive_keys(void) {
    uint32_t block[16];
    chacha20_block(&chacha_ctx, block);

    shared_key_0 = ((uint64_t)block[0] << 32) | block[1];
    shared_key_1 = ((uint64_t)block[2] << 32) | block[3];
}

static int __init nekkodrv_module_init(void) {
    struct arm_smccc_res res;

    init_chacha_prng();

    arm_smccc_hvc(ARM_SMCCC_VENDOR_HYP_KVM_AUTH_FUNC_ID, CMD_GET_CHALLENGE, 0, 0, 0, 0, 0, 0, &res);

    if (res.a0 != SMCCC_RET_SUCCESS) {
        pr_err("[-] NEKKODRV: Challenge failed!\n");
        return -EPERM;
    }

    uint64_t nonce = res.a1;

    derive_keys();

    uint64_t response = siphash24_single_u64(nonce, shared_key_0, shared_key_1);

    arm_smccc_hvc(ARM_SMCCC_VENDOR_HYP_KVM_AUTH_FUNC_ID, CMD_VERIFY_RESP, response, 0, 0, 0, 0, 0, &res);

    if (res.a0 == SMCCC_RET_SUCCESS) {
        my_session_token = res.a1;
        pr_info("[+] NEKKODRV: DONE! Token: 0x%llx\n", my_session_token);
    } else {
        pr_err("[-] NEKKODRV: Handshake failed!");
        return -EACCES;
    }

    return 0;
}

static void __exit nekkodrv_module_exit(void) {
    pr_info("[-] NEKKODRV: Unload module\n");
}

module_init(nekkodrv_module_init);
module_exit(nekkodrv_module_exit);
MODULE_LICENSE("GPL");
