#include "ember.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    if ((strcmp(argv[i], "--help") == 0) || (strcmp(argv[i], "-h") == 0)) {
      ember_print_usage(argv[0]);
      return 0;
    }
  }

  EmberOptions options;
  ember_options_init(&options);
  if (!ember_parse_args(argc, argv, &options)) {
    return argc > 1 ? 1 : 0;
  }

  if (options.benchmark) {
    ember_run_benchmark(&options);
    return 0;
  }

  int width = options.width;
  int height = options.height;

  if (options.mode == EMBER_MODE_FULLSCREEN) {
    width = 80;
    height = 24;
  }

  EmberState state;
  if (!ember_state_init(&state, &options, width, height)) {
    fprintf(stderr, "failed to initialize ember state\n");
    return 1;
  }

  bool ok = true;
  if (options.mode == EMBER_MODE_PRINT_FRAME) {
    ember_render_frame(&state);
    ember_print_frame(&state);
  } else {
    ok = ember_run_live(&state, &options);
  }

  ember_state_free(&state);
  return ok ? 0 : 1;
}
