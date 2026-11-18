// Luma P1: LD_PRELOAD shim for Limelight's visionserver (and the v4l2-ctl it shells out to).
//
// /dev/video0 is the rp1-cfe CSI receiver. Two problems make it a poor "USB camera":
//  1. It *accepts* any pixel format on the video node and only fails at STREAMON if it doesn't
//     match the sensor bus format, so OpenCV picks BGR24 and every grab fails.
//  2. It has no controls at all; exposure/gain live on the sensor subdev (/dev/v4l-subdev2).
// This shim makes the node look like a simple UVC camera: GREY only at the sensor's native size,
// plus UVC-style controls. exposure_time_absolute (100 us units) and gain are forwarded to the
// sensor; the rest (brightness, white balance, ...) are stored so read-backs match.
// Control state is shared between processes through small files in P1SHIM_STATE.
//
// Env: P1SHIM_DEV (/dev/video0), P1SHIM_SUBDEV (/dev/v4l-subdev2), P1SHIM_W/H (1280x800),
//      P1SHIM_VBLANK (1022, default sensor vblank), P1SHIM_STATE (/run/p1cam/ctl),
//      P1SHIM_TRACE (1 = log control writes to stderr)
//
// Build: aarch64-systemcore2027-linux-gnu-gcc -O2 -shared -fPIC -o p1-v4l2-shim.so p1-v4l2-shim.c -ldl
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int (*real_ioctl)(int, unsigned long, ...);
static const char *dev = "/dev/video0";
static const char *subdev = "/dev/v4l-subdev2";
static const char *state_dir = "/run/p1cam/ctl";
static unsigned W = 1280, H = 800;
static int VBLANK_DEF = 1022, trace = 0;
static double line_us = 1530.0 / 160.0;  // (width + hblank) / pixel_rate[MHz]

// Sensor CIDs on the subdev
#define SD_EXPOSURE V4L2_CID_EXPOSURE
#define SD_GAIN V4L2_CID_ANALOGUE_GAIN
#define SD_VBLANK V4L2_CID_VBLANK

enum kind { STORED, EXPO, GAIN };
struct ctl {
  uint32_t id;
  const char *name;
  uint32_t type;
  int32_t min, max, step, def;
  enum kind kind;
};
// Sorted by id (required for NEXT_CTRL enumeration).
static struct ctl ctls[] = {
    {V4L2_CID_BRIGHTNESS, "Brightness", V4L2_CTRL_TYPE_INTEGER, 0, 255, 1, 128, STORED},
    {V4L2_CID_CONTRAST, "Contrast", V4L2_CTRL_TYPE_INTEGER, 0, 255, 1, 128, STORED},
    {V4L2_CID_SATURATION, "Saturation", V4L2_CTRL_TYPE_INTEGER, 0, 255, 1, 128, STORED},
    {V4L2_CID_AUTO_WHITE_BALANCE, "White Balance, Automatic", V4L2_CTRL_TYPE_BOOLEAN, 0, 1, 1, 0,
     STORED},
    {V4L2_CID_GAMMA, "Gamma", V4L2_CTRL_TYPE_INTEGER, 0, 255, 1, 128, STORED},
    // gain: 0..255 maps linearly onto the OV9281 analogue gain 16..255 (1x..~16x)
    {V4L2_CID_GAIN, "Gain", V4L2_CTRL_TYPE_INTEGER, 0, 255, 1, 0, GAIN},
    {V4L2_CID_WHITE_BALANCE_TEMPERATURE, "White Balance Temperature", V4L2_CTRL_TYPE_INTEGER,
     2800, 6500, 1, 4600, STORED},
    {V4L2_CID_SHARPNESS, "Sharpness", V4L2_CTRL_TYPE_INTEGER, 0, 255, 1, 128, STORED},
    {V4L2_CID_EXPOSURE_AUTO, "Auto Exposure", V4L2_CTRL_TYPE_MENU, 0, 3, 1, 1, STORED},
    // 100 us units. Max 33.3 ms; above the frame time the shim stretches vblank (lower fps).
    {V4L2_CID_EXPOSURE_ABSOLUTE, "Exposure Time, Absolute", V4L2_CTRL_TYPE_INTEGER, 1, 333, 1,
     10, EXPO},
    {V4L2_CID_EXPOSURE_AUTO_PRIORITY, "Exposure, Dynamic Framerate", V4L2_CTRL_TYPE_BOOLEAN, 0,
     1, 1, 0, STORED},
};
#define NCTLS (sizeof ctls / sizeof ctls[0])

static struct ctl *find(uint32_t id) {
  for (unsigned i = 0; i < NCTLS; i++)
    if (ctls[i].id == id) return &ctls[i];
  return NULL;
}

static struct ctl *next_after(uint32_t id) {
  for (unsigned i = 0; i < NCTLS; i++)
    if (ctls[i].id > id) return &ctls[i];
  return NULL;
}

static int sd_get(uint32_t id, int32_t *v) {
  int fd = open(subdev, O_RDWR | O_CLOEXEC);
  if (fd < 0) return -1;
  struct v4l2_control c = {.id = id};
  int r = real_ioctl(fd, VIDIOC_G_CTRL, &c);
  close(fd);
  if (r == 0) *v = c.value;
  return r;
}

static int sd_set(uint32_t id, int32_t v) {
  int fd = open(subdev, O_RDWR | O_CLOEXEC);
  if (fd < 0) return -1;
  struct v4l2_control c = {.id = id, .value = v};
  int r = real_ioctl(fd, VIDIOC_S_CTRL, &c);
  close(fd);
  return r;
}

static int32_t state_get(struct ctl *c) {
  char p[128], b[32];
  snprintf(p, sizeof p, "%s/%08x", state_dir, c->id);
  int fd = open(p, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return c->def;
  ssize_t n = read(fd, b, sizeof b - 1);
  close(fd);
  if (n <= 0) return c->def;
  b[n] = 0;
  return atoi(b);
}

static void state_put(struct ctl *c, int32_t v) {
  char p[128], b[32];
  mkdir(state_dir, 0755);
  snprintf(p, sizeof p, "%s/%08x", state_dir, c->id);
  int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) return;
  int n = snprintf(b, sizeof b, "%d", v);
  if (write(fd, b, n) < 0) { /* best effort */ }
  close(fd);
}

static int32_t clampv(struct ctl *c, int32_t v) {
  return v < c->min ? c->min : v > c->max ? c->max : v;
}

static int32_t ctl_get(struct ctl *c) {
  int32_t v;
  switch (c->kind) {
    case EXPO:  // report what was asked for, so read-back "settle" checks match
    case GAIN:
    case STORED:
      return state_get(c);
  }
  (void)v;
  return c->def;
}

static int ctl_set(struct ctl *c, int32_t v) {
  v = clampv(c, v);
  if (trace) fprintf(stderr, "[p1shim] set %s = %d\n", c->name, v);
  state_put(c, v);
  if (c->kind == EXPO) {
    int lines = (int)(v * 100.0 / line_us + 0.5);
    if (lines < 1) lines = 1;
    int vblank = lines + 25 - (int)H;
    if (vblank < VBLANK_DEF) vblank = VBLANK_DEF;
    int32_t cur_vb = VBLANK_DEF;
    sd_get(SD_VBLANK, &cur_vb);
    if (vblank > cur_vb) sd_set(SD_VBLANK, vblank);  // grow frame first, then exposure
    sd_set(SD_EXPOSURE, lines);
    if (vblank < cur_vb) sd_set(SD_VBLANK, vblank);
    if (trace) fprintf(stderr, "[p1shim]   -> exposure %d lines, vblank %d\n", lines, vblank);
  } else if (c->kind == GAIN) {
    int ag = 16 + (v * (255 - 16) + 127) / 255;
    sd_set(SD_GAIN, ag);
    if (trace) fprintf(stderr, "[p1shim]   -> analogue_gain %d\n", ag);
  }
  return 0;
}

static void fill_query(struct ctl *c, struct v4l2_queryctrl *q) {
  memset(q, 0, sizeof *q);
  q->id = c->id;
  q->type = c->type;
  snprintf((char *)q->name, sizeof q->name, "%s", c->name);
  q->minimum = c->min;
  q->maximum = c->max;
  q->step = c->step;
  q->default_value = c->def;
}

static void fill_query_ext(struct ctl *c, struct v4l2_query_ext_ctrl *q) {
  memset(q, 0, sizeof *q);
  q->id = c->id;
  q->type = c->type;
  snprintf(q->name, sizeof q->name, "%s", c->name);
  q->minimum = c->min;
  q->maximum = c->max;
  q->step = c->step;
  q->default_value = c->def;
  q->elem_size = 4;
  q->elems = 1;
  q->nr_of_dims = 0;
}

static const char *menu_name(uint32_t id, uint32_t idx) {
  if (id != V4L2_CID_EXPOSURE_AUTO) return NULL;
  if (idx == 1) return "Manual Mode";
  if (idx == 3) return "Aperture Priority Mode";
  return NULL;
}

__attribute__((constructor)) static void init(void) {
  real_ioctl = dlsym(RTLD_NEXT, "ioctl");
  const char *e;
  if ((e = getenv("P1SHIM_DEV"))) dev = e;
  if ((e = getenv("P1SHIM_SUBDEV"))) subdev = e;
  if ((e = getenv("P1SHIM_STATE"))) state_dir = e;
  if ((e = getenv("P1SHIM_W"))) W = atoi(e);
  if ((e = getenv("P1SHIM_H"))) H = atoi(e);
  if ((e = getenv("P1SHIM_VBLANK"))) VBLANK_DEF = atoi(e);
  if ((e = getenv("P1SHIM_TRACE"))) trace = atoi(e);
  int32_t hb;
  if (sd_get(V4L2_CID_HBLANK, &hb) == 0) line_us = (W + hb) / 160.0;  // pixel_rate 160 MHz
}

static int is_target(int fd) {
  char p[48], l[64];
  snprintf(p, sizeof p, "/proc/self/fd/%d", fd);
  ssize_t n = readlink(p, l, sizeof l - 1);
  if (n < 0) return 0;
  l[n] = 0;
  return strcmp(l, dev) == 0;
}

static void force_fmt(struct v4l2_format *f) {
  if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) return;
  f->fmt.pix.pixelformat = V4L2_PIX_FMT_GREY;
  f->fmt.pix.width = W;
  f->fmt.pix.height = H;
  f->fmt.pix.field = V4L2_FIELD_NONE;
  f->fmt.pix.bytesperline = 0;
  f->fmt.pix.sizeimage = 0;
}

static int fail(int e) {
  errno = e;
  return -1;
}

static int frame_fps(void) {
  int32_t vb = VBLANK_DEF;
  sd_get(SD_VBLANK, &vb);
  double fps = 1e6 / (line_us * (H + vb));
  return fps < 1 ? 1 : (int)(fps + 0.5);
}

static int handle_ctrls(unsigned long req, void *arg) {
  switch (req) {
    case VIDIOC_QUERYCTRL: {
      struct v4l2_queryctrl *q = arg;
      uint32_t id = q->id;
      struct ctl *c = (id & V4L2_CTRL_FLAG_NEXT_CTRL)
                          ? next_after(id & ~(V4L2_CTRL_FLAG_NEXT_CTRL |
                                              V4L2_CTRL_FLAG_NEXT_COMPOUND))
                          : find(id);
      if (!c) return fail(EINVAL);
      fill_query(c, q);
      return 0;
    }
    case VIDIOC_QUERY_EXT_CTRL: {
      struct v4l2_query_ext_ctrl *q = arg;
      uint32_t id = q->id;
      struct ctl *c = (id & V4L2_CTRL_FLAG_NEXT_CTRL)
                          ? next_after(id & ~(V4L2_CTRL_FLAG_NEXT_CTRL |
                                              V4L2_CTRL_FLAG_NEXT_COMPOUND))
                          : find(id);
      if (!c) return fail(EINVAL);
      fill_query_ext(c, q);
      return 0;
    }
    case VIDIOC_QUERYMENU: {
      struct v4l2_querymenu *m = arg;
      const char *n = menu_name(m->id, m->index);
      if (!n) return fail(EINVAL);
      memset(m->name, 0, sizeof m->name);
      snprintf((char *)m->name, sizeof m->name, "%s", n);
      return 0;
    }
    case VIDIOC_G_CTRL:
    case VIDIOC_S_CTRL: {
      struct v4l2_control *k = arg;
      struct ctl *c = find(k->id);
      if (!c) return fail(EINVAL);
      if (req == VIDIOC_S_CTRL) ctl_set(c, k->value);
      k->value = ctl_get(c);
      return 0;
    }
    case VIDIOC_G_EXT_CTRLS:
    case VIDIOC_S_EXT_CTRLS:
    case VIDIOC_TRY_EXT_CTRLS: {
      struct v4l2_ext_controls *x = arg;
      for (uint32_t i = 0; i < x->count; i++) {
        struct ctl *c = find(x->controls[i].id);
        if (!c) {
          x->error_idx = i;
          return fail(EINVAL);
        }
      }
      for (uint32_t i = 0; i < x->count; i++) {
        struct ctl *c = find(x->controls[i].id);
        if (req == VIDIOC_S_EXT_CTRLS) ctl_set(c, x->controls[i].value);
        if (req != VIDIOC_TRY_EXT_CTRLS) x->controls[i].value = ctl_get(c);
      }
      return 0;
    }
  }
  return 1;  // not a control ioctl
}

int ioctl(int fd, unsigned long req, ...) {
  va_list ap;
  va_start(ap, req);
  void *arg = va_arg(ap, void *);
  va_end(ap);
  if (!real_ioctl) real_ioctl = dlsym(RTLD_NEXT, "ioctl");

  switch (req) {
    case VIDIOC_QUERYCTRL:
    case VIDIOC_QUERY_EXT_CTRL:
    case VIDIOC_QUERYMENU:
    case VIDIOC_G_CTRL:
    case VIDIOC_S_CTRL:
    case VIDIOC_G_EXT_CTRLS:
    case VIDIOC_S_EXT_CTRLS:
    case VIDIOC_TRY_EXT_CTRLS:
      if (is_target(fd)) return handle_ctrls(req, arg);
      break;
    case VIDIOC_ENUM_FMT:
    case VIDIOC_S_FMT:
    case VIDIOC_TRY_FMT:
    case VIDIOC_ENUM_FRAMESIZES:
    case VIDIOC_ENUM_FRAMEINTERVALS:
    case VIDIOC_G_PARM:
    case VIDIOC_S_PARM:
      if (!is_target(fd)) break;
      switch (req) {
        case VIDIOC_ENUM_FMT: {
          struct v4l2_fmtdesc *d = arg;
          if (d->type != V4L2_BUF_TYPE_VIDEO_CAPTURE || d->index != 0) return fail(EINVAL);
          memset(&d->flags, 0, sizeof *d - offsetof(struct v4l2_fmtdesc, flags));
          snprintf((char *)d->description, sizeof d->description, "8-bit Greyscale");
          d->pixelformat = V4L2_PIX_FMT_GREY;
          return 0;
        }
        case VIDIOC_ENUM_FRAMESIZES: {
          struct v4l2_frmsizeenum *s = arg;
          if (s->index != 0 || s->pixel_format != V4L2_PIX_FMT_GREY) return fail(EINVAL);
          s->type = V4L2_FRMSIZE_TYPE_DISCRETE;
          s->discrete.width = W;
          s->discrete.height = H;
          return 0;
        }
        case VIDIOC_ENUM_FRAMEINTERVALS: {
          struct v4l2_frmivalenum *i = arg;
          if (i->index != 0 || i->pixel_format != V4L2_PIX_FMT_GREY || i->width != W ||
              i->height != H)
            return fail(EINVAL);
          i->type = V4L2_FRMIVAL_TYPE_DISCRETE;
          i->discrete.numerator = 1;
          i->discrete.denominator = frame_fps();
          return 0;
        }
        case VIDIOC_G_PARM:
        case VIDIOC_S_PARM: {  // CFE has no frame-rate control; report the sensor's rate
          struct v4l2_streamparm *p = arg;
          if (p->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) break;
          memset(&p->parm.capture, 0, sizeof p->parm.capture);
          p->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
          p->parm.capture.timeperframe.numerator = 1;
          p->parm.capture.timeperframe.denominator = frame_fps();
          return 0;
        }
        default:  // S_FMT / TRY_FMT
          force_fmt(arg);
          break;
      }
      break;
  }
  return real_ioctl(fd, req, arg);
}
