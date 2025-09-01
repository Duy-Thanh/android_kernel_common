// SPDX-License-Identifier: GPL-2.0

#include <linux/security.h>

#ifndef CONFIG_SECURITY
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
__rust_helper void rust_helper_security_cred_getsecid(const struct cred *c,
						      u32 *secid)
{
	security_cred_getsecid(c, secid);
}

__rust_helper int rust_helper_security_secid_to_secctx(u32 secid, char **secdata, u32 *seclen)
{
	return security_secid_to_secctx(secid, secdata, seclen);
}

__rust_helper void rust_helper_security_release_secctx(char *secdata, u32 seclen)
{
	security_release_secctx(secdata, seclen);
}

__rust_helper
int rust_helper_security_binder_set_context_mgr(const struct cred *mgr)
{
	return security_binder_set_context_mgr(mgr);
}

__rust_helper
int rust_helper_security_binder_transaction(const struct cred *from,
					    const struct cred *to)
{
	return security_binder_transaction(from, to);
}

__rust_helper
int rust_helper_security_binder_transfer_binder(const struct cred *from,
						const struct cred *to)
{
	return security_binder_transfer_binder(from, to);
}

__rust_helper
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
void rust_helper_security_cred_getsecid(const struct cred *c, u32 *secid)
{
	security_cred_getsecid(c, secid);
}

int rust_helper_security_secid_to_secctx(u32 secid, char **secdata, u32 *seclen)
{
	return security_secid_to_secctx(secid, secdata, seclen);
}

void rust_helper_security_release_secctx(char *secdata, u32 seclen)
{
	security_release_secctx(secdata, seclen);
}

int rust_helper_security_binder_set_context_mgr(const struct cred *mgr)
{
	return security_binder_set_context_mgr(mgr);
}

int rust_helper_security_binder_transaction(const struct cred *from,
					    const struct cred *to)
{
	return security_binder_transaction(from, to);
}

int rust_helper_security_binder_transfer_binder(const struct cred *from,
						const struct cred *to)
{
	return security_binder_transfer_binder(from, to);
}

>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
int rust_helper_security_binder_transfer_file(const struct cred *from,
					      const struct cred *to,
					      const struct file *file)
{
	return security_binder_transfer_file(from, to, file);
}
#endif
