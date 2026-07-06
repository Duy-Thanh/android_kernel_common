/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Statistics for NFS server.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */
#ifndef _NFSD_STATS_H
#define _NFSD_STATS_H

#include <uapi/linux/nfsd/stats.h>

<<<<<<< HEAD   (fcc0b186bb86829d3c9ff713b613f23109fb23c4 Revert "Bluetooth: hci_core: Fix use-after-free in vhci_flus)
||||||| BASE   (a207ca90e87581dc57e401db8ffbeff6a7c537a3 HID: core: Add printk_ratelimited variants to hid_warn() etc)
int nfsd_percpu_counters_init(struct percpu_counter *counters, int num);
void nfsd_percpu_counters_reset(struct percpu_counter *counters, int num);
void nfsd_percpu_counters_destroy(struct percpu_counter *counters, int num);
int nfsd_stat_counters_init(struct nfsd_net *nn);
void nfsd_stat_counters_destroy(struct nfsd_net *nn);
void nfsd_proc_stat_init(struct net *net);
void nfsd_proc_stat_shutdown(struct net *net);
=======
int nfsd_percpu_counters_init(struct percpu_counter *counters, int num);
void nfsd_percpu_counters_reset(struct percpu_counter *counters, int num);
void nfsd_percpu_counters_destroy(struct percpu_counter *counters, int num);
int nfsd_stat_counters_init(struct nfsd_net *nn);
void nfsd_stat_counters_destroy(struct nfsd_net *nn);
struct proc_dir_entry *nfsd_proc_stat_init(struct net *net);
void nfsd_proc_stat_shutdown(struct net *net);
>>>>>>> BRANCH (3602bdb5607e07a29786a56fd77450b62b60fb41 rds: mark snapshot pages dirty in rds_info_getsockopt())

struct nfsd_stats {
	unsigned int	rchits;		/* repcache hits */
	unsigned int	rcmisses;	/* repcache hits */
	unsigned int	rcnocache;	/* uncached reqs */
	unsigned int	fh_stale;	/* FH stale error */
	unsigned int	fh_lookup;	/* dentry cached */
	unsigned int	fh_anon;	/* anon file dentry returned */
	unsigned int	fh_nocache_dir;	/* filehandle not found in dcache */
	unsigned int	fh_nocache_nondir;	/* filehandle not found in dcache */
	unsigned int	io_read;	/* bytes returned to read requests */
	unsigned int	io_write;	/* bytes passed in write requests */
	unsigned int	th_cnt;		/* number of available threads */
	unsigned int	th_usage[10];	/* number of ticks during which n perdeciles
					 * of available threads were in use */
	unsigned int	th_fullcnt;	/* number of times last free thread was used */
	unsigned int	ra_size;	/* size of ra cache */
	unsigned int	ra_depth[11];	/* number of times ra entry was found that deep
					 * in the cache (10percentiles). [10] = not found */
#ifdef CONFIG_NFSD_V4
	unsigned int	nfs4_opcount[LAST_NFS4_OP + 1];	/* count of individual nfsv4 operations */
#endif

};


extern struct nfsd_stats	nfsdstats;
extern struct svc_stat		nfsd_svcstats;

void	nfsd_stat_init(void);
void	nfsd_stat_shutdown(void);

#endif /* _NFSD_STATS_H */
