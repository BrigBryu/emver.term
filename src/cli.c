#include "ember.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool parse_scene(const char *value, EmberScene *scene) {
  if (strcmp(value, "fire") == 0) {
    *scene = EMBER_SCENE_FIRE;
    return true;
  }
  if (strcmp(value, "lava") == 0) {
    *scene = EMBER_SCENE_LAVA;
    return true;
  }
  if (strcmp(value, "spiral") == 0) {
    *scene = EMBER_SCENE_SPIRAL;
    return true;
  }
  return false;
}

static bool parse_palette(const char *value, EmberPalette *palette) {
  if (strcmp(value, "default") == 0) {
    *palette = EMBER_PALETTE_DEFAULT;
    return true;
  }
  if (strcmp(value, "gruvbox") == 0) {
    *palette = EMBER_PALETTE_GRUVBOX;
    return true;
  }
  return false;
}

static bool parse_mode(const char *value, EmberMode *mode) {
  if (strcmp(value, "widget") == 0) {
    *mode = EMBER_MODE_WIDGET;
    return true;
  }
  if (strcmp(value, "fullscreen") == 0) {
    *mode = EMBER_MODE_FULLSCREEN;
    return true;
  }
  if (strcmp(value, "print-frame") == 0) {
    *mode = EMBER_MODE_PRINT_FRAME;
    return true;
  }
  return false;
}

static bool parse_positive_int(const char *value, int *target) {
  char *end = NULL;
  long parsed = strtol(value, &end, 10);
  if (!value[0] || !end || *end != '\0' || parsed <= 0 || parsed > 10000) {
    return false;
  }
  *target = (int)parsed;
  return true;
}

void ember_options_init(EmberOptions *options) {
  memset(options, 0, sizeof(*options));
  options->width = 33;
  options->height = 12;
  options->fps = 8;
  options->mode = EMBER_MODE_WIDGET;
  options->scene = EMBER_SCENE_FIRE;
  options->palette = EMBER_PALETTE_DEFAULT;
  options->chars = " .:^*x#%@&";
  options->benchmark = false;
  options->benchmark_frames = 180;
}

const char *ember_scene_name(EmberScene scene) {
  switch (scene) {
    case EMBER_SCENE_FIRE:
      return "fire";
    case EMBER_SCENE_LAVA:
      return "lava";
    case EMBER_SCENE_SPIRAL:
      return "spiral";
  }
  return "fire";
}

const char *ember_mode_name(EmberMode mode) {
  switch (mode) {
    case EMBER_MODE_WIDGET:
      return "widget";
    case EMBER_MODE_FULLSCREEN:
      return "fullscreen";
    case EMBER_MODE_PRINT_FRAME:
      return "print-frame";
  }
  return "widget";
}

const char *ember_palette_name(EmberPalette palette) {
  switch (palette) {
    case EMBER_PALETTE_DEFAULT:
      return "default";
    case EMBER_PALETTE_GRUVBOX:
      return "gruvbox";
  }
  return "default";
}

void ember_print_usage(const char *program_name) {
  fprintf(stderr,
          "usage: %s [options]\n"
          "  --scene fire|lava|spiral\n"
          "  --palette default|gruvbox\n"
          "  --fps N\n"
          "  --width N\n"
          "  --height N\n"
          "  --chars STRING\n"
          "  --mode widget|fullscreen|print-frame\n"
          "  --fullscreen\n"
          "  --benchmark [frames]\n"
          "  --help\n",
          program_name);
}

bool ember_parse_args(int argc, char **argv, EmberOptions *options) {
  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];

    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
      ember_print_usage(argv[0]);
      return false;
    }

    if (strcmp(arg, "--fullscreen") == 0) {
      options->mode = EMBER_MODE_FULLSCREEN;
      options->fullscreen_flag = true;
      continue;
    }

    if (strcmp(arg, "--benchmark") == 0) {
      options->benchmark = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        ++i;
        if (!parse_positive_int(argv[i], &options->benchmark_frames)) {
          fprintf(stderr, "invalid benchmark frame count: %s\n", argv[i]);
          return false;
        }
      }
      continue;
    }

    if (i + 1 >= argc) {
      fprintf(stderr, "missing value for %s\n", arg);
      return false;
    }

    const char *value = argv[++i];
    if (strcmp(arg, "--scene") == 0) {
      if (!parse_scene(value, &options->scene)) {
        fprintf(stderr, "invalid scene: %s\n", value);
        return false;
      }
      continue;
    }

    if (strcmp(arg, "--palette") == 0) {
      if (!parse_palette(value, &options->palette)) {
        fprintf(stderr, "invalid palette: %s\n", value);
        return false;
      }
      continue;
    }

    if (strcmp(arg, "--mode") == 0) {
      if (!parse_mode(value, &options->mode)) {
        fprintf(stderr, "invalid mode: %s\n", value);
        return false;
      }
      continue;
    }

    if (strcmp(arg, "--fps") == 0) {
      if (!parse_positive_int(value, &options->fps)) {
        fprintf(stderr, "invalid fps: %s\n", value);
        return false;
      }
      continue;
    }

    if (strcmp(arg, "--width") == 0) {
      if (!parse_positive_int(value, &options->width)) {
        fprintf(stderr, "invalid width: %s\n", value);
        return false;
      }
      continue;
    }

    if (strcmp(arg, "--height") == 0) {
      if (!parse_positive_int(value, &options->height)) {
        fprintf(stderr, "invalid height: %s\n", value);
        return false;
      }
      continue;
    }

    if (strcmp(arg, "--chars") == 0) {
      options->chars = value;
      continue;
    }

    fprintf(stderr, "unknown option: %s\n", arg);
    return false;
  }

  if (options->fullscreen_flag) {
    options->mode = EMBER_MODE_FULLSCREEN;
  }

  return true;
}
