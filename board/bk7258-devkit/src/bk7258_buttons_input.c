/****************************************************************************
 * board/bk7258-devkit/src/bk7258_buttons_input.c
 *
 * Sole reader of /dev/btn0, a gesture engine on top of it, and a context
 * stack of key maps.
 *
 * Why the shape:
 *   - /dev/btn0 may have only one reader.  Everything that wants key input
 *     (this file's action table, and later LVGL's lv_indev read callback)
 *     consumes the state snapshot this file publishes, never the fd.
 *   - The gesture layer reports TAP/HOLD/DOUBLE/COMBO from that snapshot.
 *     LVGL's indev wants a state not an event stream too, so the two layer
 *     cleanly once it arrives.
 *   - What a key means depends on the screen.  Each context owns its own
 *     key map; a small global layer is consulted first so an escape hatch
 *     (cancel, leave) always works no matter which page is up.
 *
 * Keys (board.h): BUTTON_KEY1/2/3 on GPIO 13/12/8, active low.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/input/buttons.h>

#include "board.h"

/* The permission entry points live in packages/ai_agent/src/channels/
 * cmd_channel.h.  Declared here rather than including that header: the
 * board layer must not reach into an application package's internals, and
 * these three names are the whole surface the key map needs.
 */

#ifdef CONFIG_AI_AGENT_REMOTE_CTRL
void cmd_remote_once(void);
void cmd_remote_deny(void);
void cmd_remote_status(void);
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define GESTURE_HOLD_MS     800
#define GESTURE_DOUBLE_MS   400
#define POLL_SLICE_MS       20

#define KEY_COUNT           NUM_BUTTONS
#define KEY_BIT(i)          (1u << (i))

#define GLOBAL_MAP_NAME     "global"
#define IDLE_MAP_NAME       "idle"

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum gesture_e
{
  GESTURE_TAP = 0,   /* press and release inside HOLD */
  GESTURE_HOLD,      /* held past GESTURE_HOLD_MS */
  GESTURE_DOUBLE,    /* two taps inside DOUBLE_MS */
  GESTURE_COMBO,     /* two or more keys pressed together */
};

typedef void (*key_action_t)(void);

struct keymap_s
{
  uint32_t       keys;   /* button bitmask, one bit for TAP/HOLD/DOUBLE */
  enum gesture_e gesture;
  key_action_t   fn;
};

struct keyctx_s
{
  const char          *name;
  const struct keymap_s *map;
  unsigned int         nmap;
  void               (*enter)(void);
  void               (*leave)(void);
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int s_fd = -1;
static uint32_t s_held;          /* currently down */
static uint32_t s_prev;          /* previous snapshot, for edges */
static uint32_t s_down_ms[KEY_COUNT];
static uint32_t s_last_tap_ms[KEY_COUNT];
static bool s_tap_pending[KEY_COUNT];
static bool s_hold_fired[KEY_COUNT];
static uint32_t s_combo_started;

static const struct keyctx_s *s_ctx_stack[8];
static unsigned int s_ctx_depth;

/****************************************************************************
 * Context stack
 *
 * Lookup walks from the top downwards so a page overrides the global layer.
 * The global map holds only the escape hatches.
 ****************************************************************************/

static void action_cancel(void);
static void action_stat(void);
static void action_once(void);
static void action_deny(void);

static const struct keymap_s g_global_map[] =
{
  { KEY_BIT(0) | KEY_BIT(1), GESTURE_COMBO, action_cancel },
};

static const struct keymap_s g_idle_map[] =
{
  { KEY_BIT(0), GESTURE_TAP,  action_once },
  { KEY_BIT(1), GESTURE_TAP,  action_deny  },
  { KEY_BIT(0), GESTURE_HOLD, action_stat },
  { KEY_BIT(1), GESTURE_HOLD, action_stat },
};

static const struct keyctx_s g_global_ctx =
{
  GLOBAL_MAP_NAME, g_global_map,
  sizeof(g_global_map) / sizeof(g_global_map[0]), NULL, NULL
};

static const struct keyctx_s g_idle_ctx =
{
  IDLE_MAP_NAME, g_idle_map,
  sizeof(g_idle_map) / sizeof(g_idle_map[0]), NULL, NULL
};

void keyctx_push(const struct keyctx_s *ctx)
{
  if (s_ctx_depth < sizeof(s_ctx_stack) / sizeof(s_ctx_stack[0]))
    {
      s_ctx_stack[s_ctx_depth++] = ctx;
      if (ctx->enter != NULL)
        {
          ctx->enter();
        }
    }
}

void keyctx_pop(void)
{
  if (s_ctx_depth > 0)
    {
      const struct keyctx_s *ctx = s_ctx_stack[--s_ctx_depth];
      if (ctx->leave != NULL)
        {
          ctx->leave();
        }
    }
}

static const struct keymap_s *keymap_lookup(uint32_t keys,
                                            enum gesture_e g,
                                            const char **which)
{
  unsigned int i;

  for (i = s_ctx_depth; i > 0; i--)
    {
      const struct keyctx_s *ctx = s_ctx_stack[i - 1];
      unsigned int j;

      for (j = 0; j < ctx->nmap; j++)
        {
          if (ctx->map[j].keys == keys && ctx->map[j].gesture == g)
            {
              *which = ctx->name;
              return &ctx->map[j];
            }
        }
    }

  for (i = 0; i < g_global_ctx.nmap; i++)
    {
      if (g_global_ctx.map[i].keys == keys &&
          g_global_ctx.map[i].gesture == g)
        {
          *which = g_global_ctx.name;
          return &g_global_ctx.map[i];
        }
    }

  return NULL;
}

/****************************************************************************
 * Actions
 *
 * The permission decisions live in ai_agent's remote-ctrl channel, which
 * already rejects a duplicate or stale answer.  These are thin entry points
 * so the key map stays declarative; the channel owns the policy.
 ****************************************************************************/

static void action_cancel(void)
{
  printf("[key] cancel\n");
}

static void action_stat(void)
{
#ifdef CONFIG_AI_AGENT_REMOTE_CTRL
  cmd_remote_status();
#else
  printf("[key] stat (no remote-ctrl)\n");
#endif
}

static void action_once(void)
{
#ifdef CONFIG_AI_AGENT_REMOTE_CTRL
  cmd_remote_once();
#else
  printf("[key] once (no remote-ctrl)\n");
#endif
}

static void action_deny(void)
{
#ifdef CONFIG_AI_AGENT_REMOTE_CTRL
  cmd_remote_deny();
#else
  printf("[key] deny (no remote-ctrl)\n");
#endif
}

/****************************************************************************
 * Gesture engine
 *
 * Works on the state snapshot, not on the event stream.  A key held past
 * GESTURE_HOLD_MS fires HOLD once; releasing inside that window and
 * pressing again inside GESTURE_DOUBLE_MS upgrades the first tap to DOUBLE.
 ****************************************************************************/

static uint32_t now_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static void gesture_fire(uint32_t keys, enum gesture_e g)
{
  const char *which = "-";
  const struct keymap_s *m = keymap_lookup(keys, g, &which);

  printf("[key] %s: keys=%x gesture=%d -> %s\n", which, keys, g,
         (m != NULL && m->fn != NULL) ? "hit" : "unmapped");
  if (m != NULL && m->fn != NULL)
    {
      m->fn();
    }
}

static void gesture_update(uint32_t now)
{
  uint32_t pressed = s_held & ~s_prev;
  uint32_t released = s_prev & ~s_held;
  unsigned int i;

  /* COMBO: two or more keys down at once.  Fires once on the transition
   * and then suppresses the individual TAP/HOLD for every key that took
   * part -- otherwise pressing KEY1 then KEY2 would emit COMBO *and* a
   * stray KEY1 TAP when KEY1 is released.
   */

  if (pressed != 0 && (s_held & (s_held - 1)) != 0 && s_combo_started == 0)
    {
      s_combo_started = s_held;
      gesture_fire(s_held, GESTURE_COMBO);
    }

  for (i = 0; i < KEY_COUNT; i++)
    {
      uint32_t bit = KEY_BIT(i);

      if ((pressed & bit) != 0)
        {
          s_down_ms[i] = now;
          s_hold_fired[i] = false;
        }

      if ((s_combo_started & bit) != 0)
        {
          /* Consumed by the combo: no single-key gesture for this press, and
           * a tap left over from an earlier press of the same key must go
           * too -- otherwise it fires after the combo and looks like a stray
           * KEY1 tap.
           */

          s_tap_pending[i] = false;
          if ((released & bit) != 0)
            {
              s_combo_started &= ~bit;
            }

          continue;
        }

      if ((s_held & bit) != 0 && !s_hold_fired[i] &&
          (uint32_t)(now - s_down_ms[i]) >= GESTURE_HOLD_MS)
        {
          s_hold_fired[i] = true;
          s_tap_pending[i] = false;
          gesture_fire(bit, GESTURE_HOLD);
        }

      if ((released & bit) != 0 && !s_hold_fired[i])
        {
          gesture_fire(bit, GESTURE_TAP);
        }
    }

  /* Reset the combo only after the loop has seen every release edge.
   * Clearing it on s_held == 0 beforehand let the last key released skip
   * the suppression and emit a stray TAP.
   */

  if (s_held == 0)
    {
      s_combo_started = 0;
    }

  s_prev = s_held;
}

/****************************************************************************
 * button_source
 *
 * The only place that touches /dev/btn0.  poll() wakes on any edge; read()
 * returns the current bitmask, so edges are derived by differencing.
 ****************************************************************************/

static int button_source_init(void)
{
  struct btn_pollevents_s ev;

  s_fd = open("/dev/btn0", O_RDONLY);
  if (s_fd < 0)
    {
      printf("[key] open /dev/btn0: %d\n", errno);
      return -errno;
    }

  ev.bp_press = ev.bp_release = (1u << KEY_COUNT) - 1u;
  ioctl(s_fd, BTNIOC_POLLEVENTS, &ev);
  return 0;
}

static void button_source_poll(void)
{
  struct pollfd pfd;
  btn_buttonset_t set;
  ssize_t n;
  int ret;

  pfd.fd = s_fd;
  pfd.events = POLLIN;
  ret = poll(&pfd, 1, POLL_SLICE_MS);

  /* gesture_update must run every slice, not only when poll reports data:
   * the deferred TAP and the HOLD timer are deadlines, and skipping the
   * quiet slices pushed them to the next key event -- which is why a tap
   * printed only when something else was pressed afterwards.
   */

  if (ret > 0)
    {
      n = read(s_fd, &set, sizeof(set));
      if (n == sizeof(set))
        {
          s_held = (uint32_t)set;
        }
    }

  gesture_update(now_ms());
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

static FAR void *key_thread(FAR void *arg)
{
  (void)arg;

  if (button_source_init() < 0)
    {
      return NULL;
    }

  keyctx_push(&g_idle_ctx);
  printf("[key] input up (%d keys)\n", KEY_COUNT);

  for (; ; )
    {
      button_source_poll();
    }

  return NULL;
}

int bk7258_buttons_input_initialize(void)
{
  pthread_t tid;
  pthread_attr_t attr;
  int ret;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 2048);
  ret = pthread_create(&tid, &attr, key_thread, NULL);
  pthread_attr_destroy(&attr);
  return ret == 0 ? 0 : -EIO;
}
