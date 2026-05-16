#include "ember.h"

#include <curses.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;
static volatile sig_atomic_t g_resize = 0;

static void handle_signal(int sig) {
  if (sig == SIGWINCH) {
    g_resize = 1;
    return;
  }
  g_stop = 1;
}

static void sleep_frame(int fps) {
  struct timespec req;
  req.tv_sec = 0;
  req.tv_nsec = (long)(1000000000L / (fps > 0 ? fps : 1));
  nanosleep(&req, NULL);
}

static void configure_colors(EmberPalette palette) {
  static const short default_palette[EMBER_MAX_HEAT + 1] = {
    COLOR_BLACK, COLOR_BLACK, COLOR_BLUE, COLOR_CYAN,
    COLOR_WHITE, COLOR_YELLOW, COLOR_YELLOW, COLOR_RED,
    COLOR_RED, COLOR_MAGENTA, COLOR_YELLOW, COLOR_WHITE
  };
  static const short gruvbox_256[EMBER_MAX_HEAT + 1] = {
    235, 237, 239, 241, 243, 88, 124, 167, 166, 208, 214, 223
  };
  static const short gruvbox_fallback[EMBER_MAX_HEAT + 1] = {
    COLOR_BLACK, COLOR_BLACK, COLOR_BLACK, COLOR_WHITE,
    COLOR_WHITE, COLOR_RED, COLOR_RED, COLOR_RED,
    COLOR_YELLOW, COLOR_YELLOW, COLOR_YELLOW, COLOR_WHITE
  };

  if (!has_colors()) {
    return;
  }

  start_color();
  use_default_colors();
  const short *spec = default_palette;
  if (palette == EMBER_PALETTE_GRUVBOX) {
    spec = COLORS >= 256 ? gruvbox_256 : gruvbox_fallback;
  }

  for (short i = 0; i <= EMBER_MAX_HEAT; ++i) {
    init_pair((short)(i + 1), spec[i], -1);
  }
}

static void draw_row(const EmberState *state, int row) {
  int base = row * state->width;
  int current_color = -1;

  move(row, 0);
  for (int col = 0; col < state->width; ++col) {
    int idx = base + col;
    int pair = (int)state->colors[idx] + 1;
    if (pair != current_color && has_colors()) {
      attrset(COLOR_PAIR(pair));
      current_color = pair;
    }
    addch((chtype)state->glyphs[idx]);
  }
  clrtoeol();
}

bool ember_run_live(EmberState *state, const EmberOptions *options) {
  setlocale(LC_ALL, "");
  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);
  signal(SIGWINCH, handle_signal);

  if (initscr() == NULL) {
    fprintf(stderr, "failed to initialize ncurses\n");
    return false;
  }

  cbreak();
  noecho();
  curs_set(0);
  keypad(stdscr, TRUE);
  nodelay(stdscr, TRUE);
  erase();

  configure_colors(options->palette);

  int draw_height = state->height;
  int draw_width = state->width;

  while (!g_stop) {
    if (options->mode == EMBER_MODE_FULLSCREEN && g_resize) {
      g_resize = 0;
      endwin();
      refresh();
      clear();
      int rows = 0;
      int cols = 0;
      getmaxyx(stdscr, rows, cols);
      if (rows > 0 && cols > 0) {
        ember_state_resize(state, cols, rows);
        draw_height = state->height;
        draw_width = state->width;
      }
    }

    ember_render_frame(state);

    int max_rows = 0;
    int max_cols = 0;
    getmaxyx(stdscr, max_rows, max_cols);
    if (options->mode == EMBER_MODE_WIDGET) {
      draw_height = state->height < max_rows ? state->height : max_rows;
      draw_width = state->width < max_cols ? state->width : max_cols;
    } else {
      draw_height = state->height;
      draw_width = state->width;
    }

    for (int row = 0; row < draw_height; ++row) {
      if (!state->dirty_rows[row]) {
        continue;
      }
      draw_row(state, row);
    }

    if (draw_height < max_rows) {
      move(draw_height, 0);
    }
    refresh();
    ember_copy_frame_as_previous(state);

    int ch = getch();
    if (ch == 'q' || ch == 'Q') {
      break;
    }

    sleep_frame(options->fps);
  }

  endwin();
  return true;
}
