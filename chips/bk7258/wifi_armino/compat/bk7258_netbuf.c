/****************************************************************************
 * chips/bk7258/wifi_armino/compat/bk7258_netbuf.c
 *
 * The Armino Wi-Fi driver's network packet buffer: lwIP's `struct pbuf` and
 * the operations on it, plus the byte-order, address and statistics helpers
 * its adapters call -- five of the six lwIP units the driver used.
 *
 * Its memory comes from compat/bk7258_netheap.c, which owns the SRAM arena
 * and lwIP's mem.c API on top of it. The split follows ownership: netheap.c
 * decides where packet memory comes from and how much of it exists, this
 * file decides how many bytes one frame costs. That cost is 724 B of header
 * and headroom per pbuf, and a TX frame takes two pbufs (rwnx_start_xmit()
 * allocates a second one and copies the frame into it), so a full-size frame
 * in flight is ~4.5 KiB -- the number CONFIG_BK7258_NETBUF_ARENA_SIZE is
 * sized against.
 *
 * Why SRAM and not the NuttX heap: the CP's kernel heap spans the 5 MiB PSRAM
 * region, with a small SRAM fallback that Armino's own os_malloc() calls
 * consume at boot. A pbuf allocated from it can therefore land in PSRAM, and
 * the MAC's TX DMA cannot read PSRAM -- on the wire that was one
 * `ac3_tx_dma_dead` per frame and no EAPOL 4-way completion. A static arena
 * in .bss keeps packet memory in SRAM by construction.
 *
 * History, because it constrains the code below: with the container on the
 * NuttX heap the board still failed (PSRAM); with lwIP's own pbuf.c and only
 * mem.c on the NuttX heap it failed the same way (same reason); with lwIP's
 * pbuf.c on this arena the board ran association, 4-WAY, DHCP and ping, and
 * then ran out of arena on TCP because rwnx_start_xmit() allocates a second
 * pbuf and copies each frame into it (~2.7 KiB per frame in flight) -- which
 * is why the arena is 64 KiB and not 32. That history is also why the arena
 * and the mem.c API stay in one file (netheap.c) and the container in
 * another: the arena was the part that had to change first, and it did.
 *
 * What replaces what (chips/bk7258/wifi_armino/CMakeLists.txt no longer
 * compiles any of these):
 *
 *   lwip src/core/pbuf.c          pbuf_alloc/free/ref/header/cat/copy/coalesce
 *   lwip src/core/mem.c           the arena in bk7258_netheap.c (mem_* API
 *                                 kept)
 *   lwip src/core/memp.c          memp_init() as an empty step -- there are no
 *                                 pools, so memp_malloc()/memp_free() are
 *                                 deliberately absent: PBUF_POOL requests come
 *                                 back as one arena block (see pbuf_alloc)
 *   lwip src/core/def.c           lwip_htons()/lwip_htonl()
 *   lwip src/core/ipv4/ip4_addr.c ip4addr_aton()/ip4addr_ntoa()/_r()
 *   lwip src/core/stats.c         lwip_stats, a zero-initialised struct
 *
 * The struct layout is still lwIP's, taken from lwip/pbuf.h rather than
 * restated here: the Armino glue and the closed libraries read
 * payload/len/tot_len/next/ref/flags directly and compute their private area
 * from sizeof(struct pbuf) (rwnx_tx.c places sk_buff at
 * `(uint8_t *)p + sizeof(struct pbuf)`).
 ****************************************************************************/

#include <nuttx/config.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lwip/pbuf.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/def.h"
#include "lwip/ip_addr.h"
#include "lwip/stats.h"
#include "bk7258_netbuf_arena.h"

/****************************************************************************
 * The arena
 *
 * compat/bk7258_netheap.c owns it: a static array in .bss, first fit, an
 * address-ordered free list with adjacent blocks merged, and lwIP's mem.c API
 * on top of that. This file asks it for blocks and gives them back; the
 * alignment below is the part of that contract the pbuf container relies on.
 ****************************************************************************/

#define NETBUF_ALIGN      4u
#define NETBUF_ALIGN_UP(n) \
  (((size_t)(n) + (NETBUF_ALIGN - 1u)) & ~((size_t)NETBUF_ALIGN - 1u))

/****************************************************************************
 * Pools
 *
 * memp_init() is all that memp.c still contributes, and it is an empty step.
 ****************************************************************************/

void memp_init(void)
{
  /* There are no memp pools. memp_malloc()/memp_free() are deliberately not
   * provided: a pool allocation needs memp.c's per-pool size table, and
   * re-deriving that table is the code this file exists to delete. The only
   * memp_malloc() caller was pbuf.c's PBUF_POOL path, which pbuf_alloc()
   * answers from the arena, so an absent symbol fails at the link, loudly,
   * instead of handing back a buffer of the wrong size. */
}

/****************************************************************************
 * pbuf container
 ****************************************************************************/

/* Headroom in front of the payload.
 *
 * Upstream lwIP semantics: the pbuf_layer enum value IS the headroom, and
 * PBUF_RAW_TX already equals PBUF_LINK_ENCAPSULATION_HLEN. Adding the reserve
 * on top of the layer would therefore count it twice (PBUF_RAW_TX would get
 * 1416 bytes), which wastes ~708 bytes per TX packet.
 *
 * The reserve cannot simply be dropped either, because the two allocation
 * conventions in the compiled vendor set disagree on sk_buff ownership:
 *
 *   rwnx_start_xmit()  places sk_buff at `p + sizeof(struct pbuf)` and the TX
 *                      descriptor right after it, i.e. embedded in this
 *                      headroom (rwnx_tx.c). It allocates with PBUF_RAW_TX.
 *   alloc_skb()        allocates sk_buff separately and only uses the pbuf for
 *                      payload (skbuff.c). It allocates with PBUF_RAW == 0.
 *
 * Honouring the layer alone would give the PBUF_RAW callers zero headroom, so
 * keep PBUF_LINK_ENCAPSULATION_HLEN as a floor for every layer. That reserve
 * is CONFIG_MSDU_RESV_HEAD_LENGTH + CONFIG_MSDU_RESV_DESC_LENGTH (108 + 600),
 * the single private reserve the vendor MAC documents for a TX frame.
 */

#define PKTBUF_SIZEOF_STRUCT NETBUF_ALIGN_UP(sizeof(struct pbuf))

static size_t pktbuf_headroom(pbuf_layer layer)
{
  size_t headroom = (size_t)layer;

  return headroom > PBUF_LINK_ENCAPSULATION_HLEN ?
         headroom : (size_t)PBUF_LINK_ENCAPSULATION_HLEN;
}

/* Payload alignment.
 *
 * The authority sets MEM_ALIGNMENT 4 (lwip-2.1.2/port/lwipopts.h) and upstream
 * pbuf_alloc() runs every payload pointer through LWIP_MEM_ALIGN, so a
 * payload there is always a multiple of 4. The arena hands out 4-byte aligned
 * blocks, and the offset below is aligned up, so this keeps the same promise.
 *
 * This is NOT where the MAC's 2 mod 4 payload address came from. That comes
 * from CPU2HW(p->payload + sizeof(ETH_HDR_T)) and 14 mod 4 == 2, and it is
 * identical on the authority, which also has ETH_PAD_SIZE 0 -- ETH_PAD_SIZE is
 * the knob that exists to absorb those two bytes and neither side turns it on.
 *
 * What aligning removes is a silent trap. With sizeof(struct pbuf) == 16 and
 * the reserve at 108 + 600, the two layers with callers land on 724, aligned
 * by coincidence rather than by construction; PBUF_LINK, PBUF_IP and
 * PBUF_TRANSPORT already compute to 2 mod 4 and would hand the DMA an odd
 * address the moment they gained a caller. Changing either MSDU_RESV_*
 * length by an odd amount would do the same to the TX path itself, with no
 * diagnostic anywhere.
 *
 * Aligning up only ever grows the headroom, so the sk_buff and TX descriptor
 * that rwnx_start_xmit() embeds at `p + sizeof(struct pbuf)` keep at least the
 * room they have today.
 */

/* Every field is written explicitly, in this order. The arena hands out
 * uninitialised memory, so none of them may be left to chance; type_internal
 * in particular, because the release path and the vendor glue both read it
 * (pbuf_match_type(), cif's RX copy, rwnx's TX path), and with
 * MEM_TRX_DYNAMIC_EN=1 the RX buffers are PBUF_RAM_RX, a different pbuf_type
 * value from PBUF_RAM.
 */

struct pbuf *pbuf_alloc(pbuf_layer layer, u16_t length, pbuf_type type)
{
  struct pbuf *p;
  size_t reserve = pktbuf_headroom(layer);
  size_t offset = NETBUF_ALIGN_UP(PKTBUF_SIZEOF_STRUCT + reserve);

  /* One arena block for the header, the headroom and the payload: the payload
   * of a pbuf freed with pbuf_free() must be the same block as its header, and
   * the headroom check in pbuf_header() counts on the block starting at `p`.
   *
   * PBUF_POOL is treated as PBUF_RAM here. Upstream would return a chain of
   * PBUF_POOL_BUFSIZE segments taken from memp.c's static pool; with the pool
   * gone there is nothing to draw segments from, so the request becomes one
   * block of the full length. That is safe for this build because
   * MEM_TRX_DYNAMIC_EN=1 makes the RX path ask for PBUF_RAM_RX
   * (bk_get_rx_pbuf_type_wrapper), so nothing asks for PBUF_POOL at runtime --
   * the branch exists only so a caller that does still gets a usable pbuf.
   */

  p = netbuf_alloc(offset + length);
  if (p == NULL)
    {
      return NULL;
    }

  p->next = NULL;
  p->payload = (uint8_t *)p + offset;
  p->tot_len = length;
  p->len = length;
  p->type_internal = (u8_t)type;
  p->flags = 0;
  p->ref = 1;
  p->if_idx = 0;                /* NETIF_NO_INDEX in this lwip copy */
  return p;
}

/* No lock here, deliberately. lwIP guards these with SYS_ARCH_PROTECT only
 * when SYS_LIGHTWEIGHT_PROT is on, and this build's callers -- the Armino
 * wpa/ke tasks, the macif TX path and the NuttX netdev lower half -- are all
 * task context that already serialises pbuf ownership through the driver's
 * own queues. Taking a mutex per reference change instead puts a scheduler
 * call on the TX completion path, which is where the first attempt at this
 * lost frames.
 */

void pbuf_ref(struct pbuf *p)
{
  if (p != NULL && p->ref != 0xff)
    {
      p->ref++;
    }
}

u8_t pbuf_free(struct pbuf *p)
{
  u8_t count = 0;

  /* Refcount walk, aligned with lwIP's pbuf_free() (lwip-2.1.2
   * src/core/pbuf.c:767-830), which is what Armino actually linked: its
   * bk_wifi/src/pbuf.c fallback is dead code here because that file is
   * entirely inside `#if (CONFIG_FULLY_HOSTED || CONFIG_SEMI_HOSTED)` and
   * both builds are CONFIG_NO_HOSTED=1.
   *
   * Two divergences were fixed, both only observable on CHAINED pbufs (single
   * pbufs behaved identically, which is why the EAPOL M2 path never showed
   * it):
   *
   *   1. Stop the walk when the reference count does not reach zero. lwIP only
   *      descends into p->next after freeing p, because a pbuf owns one
   *      reference on its successor. A loop that keeps walking decrements
   *      every following pbuf's ref, so freeing a chain head that another
   *      owner still holds can free the tail underneath them.
   *
   *   2. Never touch p->next unless p is actually freed. Clearing it
   *      unconditionally truncates a chain that the remaining owner still
   *      references -- its tot_len then describes data no longer reachable.
   *
   * Entry with ref == 0 means a double free somewhere else. lwIP asserts in
   * debug and, with assertions compiled out, wraps the counter and leaks. We
   * stop instead: leaking one pbuf keeps the arena intact and leaves the real
   * bug findable, whereas falling through to netbuf_free() corrupts it.
   *
   * Every pbuf this build creates comes from pbuf_alloc() and owns its
   * payload, so the free is one netbuf_free() per pbuf: no PBUF_ROM/PBUF_REF
   * payload to leave alone, and no custom pbuf (PBUF_FLAG_IS_CUSTOM) is ever
   * built -- nothing in the compiled Armino set calls pbuf_alloced_custom()
   * or pbuf_alloc_reference().
   */

  while (p != NULL)
    {
      struct pbuf *next;

      if (p->ref == 0)
        {
          /* A double free somewhere else. Leak rather than corrupt. */

          break;
        }

      if (--p->ref != 0)
        {
          /* Still referenced: the rest of the chain stays owned by whoever
           * holds that reference. */

          break;
        }

      next = p->next;
      netbuf_free(p);
      count++;
      p = next;
    }

  return count;
}

/* pbuf_header() with lwIP's checks, on a payload that this file allocated.
 *
 * Upstream splits this into pbuf_add_header_impl() and pbuf_remove_header();
 * the checks are the same ones, against the same bound: the payload of a
 * struct-data-contiguous pbuf may grow towards `p + PKTBUF_SIZEOF_STRUCT` (the
 * whole reserve is available) and no further, and a pbuf that only refers to
 * external data may not grow at all. PBUF_ROM and PBUF_REF pbufs never occur
 * here (see pbuf_free()), but the branch is kept so a caller that passes one
 * gets lwIP's answer -- a failure -- instead of writing in front of a buffer
 * it does not own.
 */

u8_t pbuf_header(struct pbuf *p, s16_t header_size_increment)
{
  void *payload;
  u16_t increment;

  if (p == NULL)
    {
      return 1;
    }

  if (header_size_increment == 0)
    {
      return 0;
    }

  if (header_size_increment < 0)
    {
      increment = (u16_t)-header_size_increment;

      /* Check that we aren't going to move off the end of the pbuf. */

      if (increment > p->len)
        {
          return 1;
        }

      p->payload = (u8_t *)p->payload + increment;
      p->len = (u16_t)(p->len - increment);
      p->tot_len = (u16_t)(p->tot_len - increment);
      return 0;
    }

  increment = (u16_t)header_size_increment;

  /* Do not allow tot_len to wrap as a result. */

  if ((u16_t)(increment + p->tot_len) < increment)
    {
      return 1;
    }

  if ((p->type_internal & PBUF_TYPE_FLAG_STRUCT_DATA_CONTIGUOUS) == 0)
    {
      /* Cannot expand a payload that lives outside the pbuf. */

      return 1;
    }

  payload = (u8_t *)p->payload - increment;
  if ((u8_t *)payload < (u8_t *)p + PKTBUF_SIZEOF_STRUCT)
    {
      /* Not enough headroom left in front of the payload. */

      return 1;
    }

  p->payload = payload;
  p->len = (u16_t)(p->len + increment);
  p->tot_len = (u16_t)(p->tot_len + increment);
  return 0;
}

void pbuf_cat(struct pbuf *head, struct pbuf *tail)
{
  struct pbuf *p;

  if (head == NULL || tail == NULL)
    {
      return;
    }

  /* tot_len of the head pbuf counts the whole chain, so every pbuf up to the
   * last one grows by the length of the tail chain. */

  for (p = head; p->next != NULL; p = p->next)
    {
      p->tot_len = (u16_t)(p->tot_len + tail->tot_len);
    }

  p->tot_len = (u16_t)(p->tot_len + tail->tot_len);

  /* p->next now references tail, but the caller drops its own reference to
   * it, so tail's reference count does not change. */

  p->next = tail;
}

err_t pbuf_copy(struct pbuf *p_to, const struct pbuf *p_from)
{
  size_t offset_to = 0;
  size_t offset_from = 0;
  size_t len;

  if (p_to == NULL || p_from == NULL || p_to->tot_len < p_from->tot_len)
    {
      return ERR_ARG;
    }

  do
    {
      if ((p_to->len - offset_to) >= (p_from->len - offset_from))
        {
          len = p_from->len - offset_from;
        }
      else
        {
          len = p_to->len - offset_to;
        }

      memcpy((u8_t *)p_to->payload + offset_to,
             (const u8_t *)p_from->payload + offset_from, len);
      offset_to += len;
      offset_from += len;

      if (offset_from >= p_from->len)
        {
          offset_from = 0;
          p_from = p_from->next;
        }

      if (offset_to == p_to->len)
        {
          offset_to = 0;
          p_to = p_to->next;
          if (p_to == NULL && p_from != NULL)
            {
              return ERR_ARG;
            }
        }
    }
  while (p_from != NULL);

  return ERR_OK;
}

/* Upstream returns p unchanged when it cannot allocate the flat copy, and so
 * does this: a caller that got its pbuf back keeps its frame, where a NULL
 * would lose it.
 *
 * ponytail: upstream goes through pbuf_clone(), which copies the per-pbuf
 * metadata as well. Nothing in this build clones anything but the payload,
 * so pbuf_copy() into a fresh PBUF_RAM pbuf is the whole of it.
 */

struct pbuf *pbuf_coalesce(struct pbuf *p, pbuf_layer layer)
{
  struct pbuf *flat;

  if (p == NULL || p->next == NULL)
    {
      return p;
    }

  flat = pbuf_alloc(layer, p->tot_len, PBUF_RAM);
  if (flat == NULL)
    {
      return p;
    }

  if (pbuf_copy(flat, p) != ERR_OK)
    {
      pbuf_free(flat);
      return p;
    }

  pbuf_free(p);
  return flat;
}

/****************************************************************************
 * Statistics
 *
 * lwip_stats is one zero-initialised struct: nothing in this build counts
 * into it any more (the counters were incremented by lwIP's protocol code
 * and by pbuf.c's lifetime instrumentation, both gone), but cli_wifi.c and
 * the Armino command table reference the symbol, and a reader that dumps it
 * gets zeros rather than a crash.
 ****************************************************************************/

struct stats_ lwip_stats;

void stats_init(void)
{
  /* stats.c only filled in debug name pointers here. */
}

/****************************************************************************
 * Byte order and addresses
 *
 * def.c's two byte-swap functions and ip4_addr.c's aton/ntoa. bk_wifi's
 * adapter calls all four (bk_lwip_htons_wrapper, ip4addr_aton for a static
 * IP, ip4addr_ntoa for the CLI and iperf), and they are pure computation with
 * no lwIP state behind them.
 *
 * lwip_ntohs()/lwip_ntohl() need no definition: lwip/def.h makes them
 * references to lwip_htons()/lwip_htonl().
 ****************************************************************************/

u16_t lwip_htons(u16_t n)
{
  return PP_HTONS(n);
}

u32_t lwip_htonl(u32_t n)
{
  return PP_HTONL(n);
}

int ip4addr_aton(const char *cp, ip4_addr_t *addr)
{
  u32_t val;
  u8_t base;
  char c;
  u32_t parts[4];
  u32_t *pp = parts;

  c = *cp;
  for (;;)
    {
      /* Collect a number up to a '.', with C's conventions: 0x hex,
       * 0 octal, 1-9 decimal.
       */

      if (!lwip_isdigit(c))
        {
          return 0;
        }

      val = 0;
      base = 10;
      if (c == '0')
        {
          c = *++cp;
          if (c == 'x' || c == 'X')
            {
              base = 16;
              c = *++cp;
            }
          else
            {
              base = 8;
            }
        }

      for (;;)
        {
          if (lwip_isdigit(c))
            {
              val = (val * base) + (u32_t)(c - '0');
              c = *++cp;
            }
          else if (base == 16 && lwip_isxdigit(c))
            {
              val = (val << 4) | (u32_t)(c + 10 -
                                         (lwip_islower(c) ? 'a' : 'A'));
              c = *++cp;
            }
          else
            {
              break;
            }
        }

      if (c != '.')
        {
          break;
        }

      /* Internet format, all of these are valid:
       *   a.b.c.d
       *   a.b.c   (with c treated as 16 bits)
       *   a.b     (with b treated as 24 bits)
       */

      if (pp >= parts + 3)
        {
          return 0;
        }

      *pp++ = val;
      c = *++cp;
    }

  /* Check for trailing characters. */

  if (c != '\0' && !lwip_isspace(c))
    {
      return 0;
    }

  /* Concoct the address according to the number of parts specified. */

  switch (pp - parts + 1)
    {
    case 0:
      return 0;                 /* initial nondigit */

    case 1:                     /* a -- 32 bits */
      break;

    case 2:                     /* a.b -- 8.24 bits */
      if (val > 0xffffffUL || parts[0] > 0xff)
        {
          return 0;
        }

      val |= parts[0] << 24;
      break;

    case 3:                     /* a.b.c -- 8.8.16 bits */
      if (val > 0xffff || parts[0] > 0xff || parts[1] > 0xff)
        {
          return 0;
        }

      val |= (parts[0] << 24) | (parts[1] << 16);
      break;

    case 4:                     /* a.b.c.d -- 8.8.8.8 bits */
      if (val > 0xff || parts[0] > 0xff || parts[1] > 0xff ||
          parts[2] > 0xff)
        {
          return 0;
        }

      val |= (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8);
      break;

    default:
      return 0;
    }

  if (addr != NULL)
    {
      ip4_addr_set_u32(addr, lwip_htonl(val));
    }

  return 1;
}

char *ip4addr_ntoa_r(const ip4_addr_t *addr, char *buf, int buflen)
{
  u32_t s_addr;
  char inv[3];
  char *rp;
  u8_t *ap;
  u8_t rem;
  u8_t n;
  u8_t i;
  int len = 0;

  s_addr = ip4_addr_get_u32(addr);

  rp = buf;
  ap = (u8_t *)&s_addr;
  for (n = 0; n < 4; n++)
    {
      i = 0;
      do
        {
          rem = *ap % (u8_t)10;
          *ap /= (u8_t)10;
          inv[i++] = (char)('0' + rem);
        }
      while (*ap);

      while (i--)
        {
          if (len++ >= buflen)
            {
              return NULL;
            }

          *rp++ = inv[i];
        }

      if (len++ >= buflen)
        {
          return NULL;
        }

      *rp++ = '.';
      ap++;
    }

  *--rp = 0;
  return buf;
}

char *ip4addr_ntoa(const ip4_addr_t *addr)
{
  static char str[IP4ADDR_STRLEN_MAX];

  return ip4addr_ntoa_r(addr, str, IP4ADDR_STRLEN_MAX);
}
