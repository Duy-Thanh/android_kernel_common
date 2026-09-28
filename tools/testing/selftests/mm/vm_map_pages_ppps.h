/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __VM_MAP_PAGES_PPPS_H
#define __VM_MAP_PAGES_PPPS_H

/* High vm_pgoff values select vm_insert_pages() edge cases in the fixture. */
#define VM_MAP_PAGES_PPPS_ZERO		0x100UL
#define VM_MAP_PAGES_PPPS_BEFORE	0x101UL
#define VM_MAP_PAGES_PPPS_AFTER		0x102UL
#define VM_MAP_PAGES_PPPS_TOO_MANY	0x103UL
#define VM_MAP_PAGES_PPPS_BUSY		0x104UL
#define VM_MAP_PAGES_PPPS_NATIVE_PARTIAL	0x105UL

#endif /* __VM_MAP_PAGES_PPPS_H */
