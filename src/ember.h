#ifndef EMBER_H
#define EMBER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EMBER_MAX_HEAT 11

typedef enum {
  EMBER_MODE_WIDGET = 0,
  EMBER_MODE_FULLSCREEN = 1,
  EMBER_MODE_PRINT_FRAME = 2,
} EmberMode;

typedef enum {
  EMBER_SCENE_FIRE = 0,
  EMBER_SCENE_LAVA = 1,
  EMBER_SCENE_SPIRAL = 2,
} EmberScene;

typedef enum {
  EMBER_PALETTE_DEFAULT = 0,
  EMBER_PALETTE_GRUVBOX = 1,
} EmberPalette;

typedef struct {
  int width;
  int height;
  int fps;
  EmberMode mode;
  EmberScene scene;
  EmberPalette palette;
  const char *chars;
  bool benchmark;
  int benchmark_frames;
  bool fullscreen_flag;
} EmberOptions;

typedef struct {
  double turns;
  double thickness;
  double rotation_speed;
  double pulse_amount;
  double center_bias_x;
  double center_bias_y;
} EmberSpiralConfig;

typedef struct {
  int blobs;
  double speed;
  double pulse_amount;
  double center_bias_x;
  double center_bias_y;
} EmberLavaConfig;

typedef struct {
  bool enabled;
  double sway_period_frames;
  double breathe_period_frames;
  double sway_amplitude;
  double radius_mod;
  double height_mod;
  double energy_mod;
  double phase;
  double sway;
  double breathe;
} EmberWaveConfig;

typedef struct {
  int d_row;
  int d_col;
  float weight;
} EmberKernelPoint;

typedef struct {
  double base_x;
  double base_y;
  double radius;
  double energy;
  double phase;
  double drift_x;
  double drift_y;
  double rate_x;
  double rate_y;
  double pulse_rate;
  int radius_key;
} EmberBlob;

typedef struct {
  EmberKernelPoint *points;
  int count;
} EmberKernel;

typedef struct {
  int width;
  int height;
  int size;
  int max_heat;
  EmberScene scene;
  EmberPalette palette;
  double intensity;
  int frame_no;
  double phase;
  const char *ramp;
  size_t ramp_len;

  float *grid;
  float *next_grid;
  uint8_t *glyph_levels;
  uint8_t *prev_levels;
  char *glyphs;
  char *prev_glyphs;
  uint8_t *colors;
  uint8_t *prev_colors;
  int *left_cols;
  int *right_cols;

  uint32_t *row_hashes;
  uint32_t *prev_row_hashes;
  uint8_t *dirty_rows;

  float *fuel;
  float *tongues;
  int smoke_col;
  int smoke_life;

  EmberWaveConfig wave;
  EmberLavaConfig lava;
  EmberSpiralConfig spiral;

  EmberBlob *lava_blobs;
  int lava_blob_count;
  EmberKernel lava_kernels[16];

  EmberKernel spiral_kernel;
  double spiral_kernel_thickness;
  double spiral_angle;
  double spiral_pulse;
} EmberState;

typedef struct {
  long long frames;
  double total_ms;
  double average_ms;
  double min_ms;
  double max_ms;
} EmberBenchmark;

void ember_options_init(EmberOptions *options);
bool ember_parse_args(int argc, char **argv, EmberOptions *options);
void ember_print_usage(const char *program_name);

bool ember_state_init(EmberState *state, const EmberOptions *options, int width, int height);
void ember_state_free(EmberState *state);
bool ember_state_resize(EmberState *state, int width, int height);
void ember_render_frame(EmberState *state);
void ember_copy_frame_as_previous(EmberState *state);
void ember_print_frame(const EmberState *state);
bool ember_run_live(EmberState *state, const EmberOptions *options);
void ember_run_benchmark(const EmberOptions *options);

const char *ember_scene_name(EmberScene scene);
const char *ember_mode_name(EmberMode mode);
const char *ember_palette_name(EmberPalette palette);

#endif
