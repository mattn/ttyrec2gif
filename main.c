#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <unistd.h>
#include <gd.h>
#include <gdfontl.h>
#include "vterm.h"

#define SWAP_ENDIAN(val) ((unsigned int) ( \
      (((unsigned int) (val) & (unsigned int) 0x000000ffU) << 24) | \
      (((unsigned int) (val) & (unsigned int) 0x0000ff00U) <<  8) | \
      (((unsigned int) (val) & (unsigned int) 0x00ff0000U) >>  8) | \
      (((unsigned int) (val) & (unsigned int) 0xff000000U) >> 24)))

static char *f = "VL-Gothic-Regular";
static double fs = 8.0;
static int brect[8] = {0};
static int w, h, dx, dy;

typedef struct header {
  int diff;
  int len;
} Header;

static int
is_little_endian() {
  static int retval = -1;
  if (retval == -1) {
    int n = 1;
    char *p = (char *)&n;
    char x[] = {1, 0, 0, 0};
    retval = memcmp(p, x, 4) == 0 ? 1 : 0;
  }
  return retval;
}

static int
convert_to_little_endian(int x) {
  if (is_little_endian())
    return x;
  return SWAP_ENDIAN(x);
}

static int
read_header(FILE *fp, Header *h) {
  static unsigned long old = 0;
  unsigned long cur;
  int buf[3];
  if (fread(buf, sizeof(int), 3, fp) == 0) {
    return 0;
  }
  cur = convert_to_little_endian(buf[0]) * 1000000
    + convert_to_little_endian(buf[1]);

  h->diff = cur - old;
  h->len  = convert_to_little_endian(buf[2]);
  old = cur;
  return 1;
}

static int
ttyread(FILE *fp, Header *h, char **buf) {
  if (read_header(fp, h) == 0)
    return 0;
  *buf = malloc(h->len);
  if (*buf == NULL)
    perror("malloc");
  if (fread(*buf, 1, h->len, fp) == 0)
    perror("fread");
  return 1;
}

static int
utf_char2bytes(int c, char *buf) {
  if (c < 0x80) {
    buf[0] = c;
    return 1;
  }
  if (c < 0x800) {
    buf[0] = 0xc0 + ((unsigned)c >> 6);
    buf[1] = 0x80 + (c & 0x3f);
    return 2;
  }
  if (c < 0x10000) {
    buf[0] = 0xe0 + ((unsigned)c >> 12);
    buf[1] = 0x80 + (((unsigned)c >> 6) & 0x3f);
    buf[2] = 0x80 + (c & 0x3f);
    return 3;
  }
  if (c < 0x200000) {
    buf[0] = 0xf0 + ((unsigned)c >> 18);
    buf[1] = 0x80 + (((unsigned)c >> 12) & 0x3f);
    buf[2] = 0x80 + (((unsigned)c >> 6) & 0x3f);
    buf[3] = 0x80 + (c & 0x3f);
    return 4;
  }
  if (c < 0x4000000) {
    buf[0] = 0xf8 + ((unsigned)c >> 24);
    buf[1] = 0x80 + (((unsigned)c >> 18) & 0x3f);
    buf[2] = 0x80 + (((unsigned)c >> 12) & 0x3f);
    buf[3] = 0x80 + (((unsigned)c >> 6) & 0x3f);
    buf[4] = 0x80 + (c & 0x3f);
    return 5;
  }
  buf[0] = 0xfc + ((unsigned)c >> 30);
  buf[1] = 0x80 + (((unsigned)c >> 24) & 0x3f);
  buf[2] = 0x80 + (((unsigned)c >> 18) & 0x3f);
  buf[3] = 0x80 + (((unsigned)c >> 12) & 0x3f);
  buf[4] = 0x80 + (((unsigned)c >> 6) & 0x3f);
  buf[5] = 0x80 + (c & 0x3f);
  return 6;
}

static void
usage(void) {
  printf("Usage: ttyrec2gif [OPTION] [FILE]\n");
  printf("  -o FILE  Set output file \n");
  printf("  -f FONT  Set font file \n");
  exit(EXIT_FAILURE);
}

static void
write_schene(FILE* out, VTerm* vt, int duration) {
  int color;
  VTermScreen* screen;
  VTermPos pos;
  VTermScreenCell cell;
  gdImagePtr fimg;

  screen = vterm_obtain_screen(vt);

  /* render terminal */
  fimg = gdImageCreateTrueColor(w, h);
  for (pos.row = 0; pos.row < 24; pos.row++) {
    for (pos.col = 0; pos.col < 80; pos.col++) {
      char b[7] = {0};
      vterm_screen_get_cell(screen, pos, &cell);
      if (cell.chars[0] == 0) continue;
      b[utf_char2bytes((int) *cell.chars, b)] = 0;

      color = gdImageColorResolve(
          fimg,
          cell.bg.red,
          cell.bg.green,
          cell.bg.blue);
      gdImageFilledRectangle(
          fimg,
          pos.col * dx,
          pos.row * dy,
          (pos.col + cell.width) * dx,
          (pos.row + cell.width) * dy,
          color);
      color = gdImageColorResolve(
          fimg,
          cell.fg.red,
          cell.fg.green,
          cell.fg.blue);
      puts(gdImageStringFT(
            fimg, NULL, color, f,
            fs, 0.0,
            pos.col * dx - brect[6],
            pos.row * dy - brect[7],
            b));
      pos.col += cell.width - 1;
    }
  }

  /* render cursor */
  vterm_state_get_cursorpos(vterm_obtain_state(vt), &pos);
  vterm_screen_get_cell(screen, pos, &cell);
  color = gdImageColorResolve(fimg, 255, 255, 255);
  gdImageFilledRectangle(
      fimg,
      pos.col * dx,
      pos.row * dy,
      (pos.col + cell.width) * dx,
      (pos.row + 1) * dy,
      color);

  /* add frame with delay */
  gdImageTrueColorToPalette(fimg, 1, gdMaxColors);
  gdImageGifAnimAdd(fimg, out, 1, 0, 0, duration, gdDisposalNone, NULL);
  gdImageDestroy(fimg);
}

int
main(int argc, char* argv[]) {
  char *buf;
  Header header;
  VTerm* vt;
  VTermScreen* screen;
  char* fname = "animated.gif";
  FILE* in = NULL;
  FILE* out = NULL;

  gdImagePtr img;

  while (1) {
    int ch = getopt(argc, argv, "o:f:");
    if (ch == EOF) break;
    switch (ch) {
      case 'o':
        if (optarg == NULL) usage();
        fname = optarg;
        break;
      case 'f':
        if (optarg == NULL) usage();
        f = optarg;
        break;
      default:
        usage();
    }
  }

  if (optind >= argc) usage();

  in = fopen(argv[optind], "rb");
  if (!in) {
    perror("fopen");
    return EXIT_FAILURE;
  }
  out = fopen(fname, "wb");
  if (!out) {
    perror("fopen");
    fclose(in);
    return EXIT_FAILURE;
  }

  /* calculate cell size */
  gdImageStringFT(NULL, brect, 0, f, fs, 0.0, 0, 0, "\u25a0");
  dx = (brect[4] - brect[6]) / 2;
  dy = brect[1] - brect[7];
  w = 80 * dx;
  h = 24 * dy;

  img = gdImageCreate(w, h);
  if (!img) {
    perror("gdImageCreate");
    fclose(in);
    fclose(out);
    return EXIT_FAILURE;
  }
  gdImageGifAnimBegin(img, out, 0, -1);

  /* setup terminal */
  vt = vterm_new(24, 80);
  vterm_set_utf8(vt, 1);
  screen = vterm_obtain_screen(vt);
  vterm_screen_enable_altscreen(screen, 1);
  vterm_screen_reset(screen, 1);

  while (ttyread(in, &header, &buf) != 0) {
    /* write to terminal */
    vterm_input_write(vt, buf, header.len);
    free(buf);
    /* write screen */
    write_schene(out, vt, header.diff/10000);
  }
  gdImageGifAnimEnd(out);
  fclose(in);
  fclose(out);

  gdImageDestroy(img);
  vterm_free(vt);
  return EXIT_SUCCESS;
}

/* vim:set et: */
