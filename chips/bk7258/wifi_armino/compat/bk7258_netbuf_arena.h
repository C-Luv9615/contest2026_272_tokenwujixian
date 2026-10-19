/****************************************************************************
 * chips/bk7258/wifi_armino/compat/bk7258_netbuf_arena.h
 *
 * The interface between the SRAM packet-buffer arena
 * (compat/bk7258_netheap.c) and the pbuf container built on it
 * (compat/bk7258_netbuf.c). The container is the arena's only consumer, so
 * the contract is two calls.
 *
 * Blocks are 4-byte aligned and their size is rounded up to a multiple of 4,
 * because lwIP's MEM_ALIGNMENT is 4 (lwipopts.h) and an unaligned payload
 * hands the MAC's DMA a pointer it cannot use. A NULL return means the arena
 * is full -- the caller drops the frame, as upstream lwIP does.
 ****************************************************************************/

#ifndef __BK7258_WIFI_ARMINO_NETBUF_ARENA_H
#define __BK7258_WIFI_ARMINO_NETBUF_ARENA_H

#include <stddef.h>

void *netbuf_alloc(size_t size);
void netbuf_free(void *mem);

#endif /* __BK7258_WIFI_ARMINO_NETBUF_ARENA_H */
