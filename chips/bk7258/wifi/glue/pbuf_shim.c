/* Armino pbuf ABI implementation backed by the NuttX kernel heap.
 * This is a vendor-packet object, never a NuttX netpkt/iob. */

#include <nuttx/kmalloc.h>
#include <errno.h>
#include <string.h>

#include "lwip/pbuf.h"
#include "os/mem.h"

/* Headroom in front of the payload.
 *
 * Upstream lwIP semantics: the pbuf_layer enum value IS the headroom, and
 * PBUF_RAW_TX already equals PBUF_LINK_ENCAPSULATION_HLEN. Adding the reserve
 * on top of the layer would therefore count it twice (PBUF_RAW_TX would get
 * 1416 bytes), which wastes ~708 bytes per TX packet on a ~195 KiB CP.
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
 * keep PBUF_LINK_ENCAPSULATION_HLEN as a floor for every layer. That is the
 * same single 708-byte private reserve bk7258_wifi_packet.c uses, so both
 * containers now describe one contract instead of two.
 */

static size_t pbuf_headroom(pbuf_layer layer)
{
  size_t headroom = (size_t)layer;

  return headroom > PBUF_LINK_ENCAPSULATION_HLEN ?
         headroom : (size_t)PBUF_LINK_ENCAPSULATION_HLEN;
}

struct pbuf *pbuf_alloc(pbuf_layer layer, u16_t length, pbuf_type type)
{
  struct pbuf *p;
  size_t reserve = pbuf_headroom(layer);
  size_t total = sizeof(*p) + reserve + length;

  p = kmm_zalloc(total);
  if (p == NULL)
    {
      return NULL;
    }

  p->next = NULL;
  p->payload = (uint8_t *)p + sizeof(*p) + reserve;
  p->tot_len = length;
  p->len = length;
  p->type_internal = (u8_t)type;
  p->flags = 0;
  p->ref = 1;
  p->if_idx = 0;
  return p;
}

void pbuf_ref(struct pbuf *p)
{
  if (p != NULL && p->ref != 0xff)
    {
      p->ref++;
    }
}

u8_t pbuf_free(struct pbuf *p)
{
  u8_t freed = 0;

  while (p != NULL)
    {
      struct pbuf *next = p->next;
      p->next = NULL;
      if (p->ref > 0)
        {
          p->ref--;
        }
      if (p->ref == 0)
        {
          kmm_free(p);
          freed++;
        }
      p = next;
    }
  return freed;
}

u8_t pbuf_header(struct pbuf *p, s16_t header_size)
{
  if (p == NULL)
    {
      return 1;
    }

  if (header_size > 0)
    {
      p->payload = (uint8_t *)p->payload - header_size;
      p->len = (u16_t)(p->len + header_size);
      p->tot_len = (u16_t)(p->tot_len + header_size);
    }
  else if (header_size < 0)
    {
      u16_t remove = (u16_t)-header_size;
      if (remove > p->len || remove > p->tot_len)
        {
          return 1;
        }
      p->payload = (uint8_t *)p->payload + remove;
      p->len = (u16_t)(p->len - remove);
      p->tot_len = (u16_t)(p->tot_len - remove);
    }
  return 0;
}

void pbuf_cat(struct pbuf *head, struct pbuf *tail)
{
  struct pbuf *p;
  if (head == NULL || tail == NULL)
    {
      return;
    }
  for (p = head; p->next != NULL; p = p->next)
    {
    }
  p->next = tail;
  head->tot_len = (u16_t)(head->len + tail->tot_len);
}

err_t pbuf_copy(struct pbuf *dst, const struct pbuf *src)
{
  const struct pbuf *s = src;
  struct pbuf *d = dst;
  u16_t remaining = dst != NULL ? dst->tot_len : 0;

  if (dst == NULL || src == NULL)
    {
      return ERR_ARG;
    }
  while (s != NULL && d != NULL && remaining != 0)
    {
      u16_t n = s->len < d->len ? s->len : d->len;
      if (n > remaining)
        {
          n = remaining;
        }
      memcpy(d->payload, s->payload, n);
      remaining -= n;
      s = s->next;
      d = d->next;
    }
  return remaining == 0 ? ERR_OK : ERR_MEM;
}

struct pbuf *pbuf_coalesce(struct pbuf *p, pbuf_layer layer)
{
  struct pbuf *flat;
  struct pbuf *q;
  uint16_t total;
  uint8_t *dst;

  if (p == NULL || p->next == NULL)
    {
      return p;
    }
  total = p->tot_len;
  flat = pbuf_alloc(layer, total, PBUF_RAM);
  if (flat == NULL)
    {
      return p;
    }
  dst = flat->payload;
  for (q = p; q != NULL; q = q->next)
    {
      memcpy(dst, q->payload, q->len);
      dst += q->len;
    }
  pbuf_free(p);
  return flat;
}

void *bk_pbuf_alloc_wrapper(int layer, uint16_t length, int type)
{
  return pbuf_alloc((pbuf_layer)layer, length, (pbuf_type)type);
}

void bk_pbuf_free_wrapper(void *p)
{
  pbuf_free((struct pbuf *)p);
}

void bk_pbuf_ref_wrapper(void *p)
{
  pbuf_ref((struct pbuf *)p);
}

void bk_pbuf_header_wrapper(void *p, int16_t len)
{
  (void)pbuf_header((struct pbuf *)p, len);
}

void bk_pbuf_cat_wrapper(void *p, void *q)
{
  pbuf_cat((struct pbuf *)p, (struct pbuf *)q);
}

void *bk_pbuf_coalesce_wrapper(void *p)
{
  return pbuf_coalesce((struct pbuf *)p, PBUF_RAW);
}

int bk_get_rx_pbuf_type_wrapper(void)
{
  return PBUF_RAM_RX;
}

int bk_get_pbuf_pool_size_wrapper(void)
{
  return 0;
}
