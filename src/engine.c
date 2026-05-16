#include "ember.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const double EMBER_TAU = 6.28318530717958647692;
static const char *DEFAULT_RAMP = " .:^*x#%@&";

static uint32_t hash_row(const char *glyphs, const uint8_t *colors, int offset, int width) {
  uint32_t hash = 2166136261u;
  for (int col = 0; col < width; ++col) {
    hash ^= (unsigned char)glyphs[offset + col];
    hash *= 16777619u;
    hash ^= colors[offset + col];
    hash *= 16777619u;
  }
  return hash;
}

static inline int clampi(int value, int low, int high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

static inline double clampd(double value, double low, double high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

static inline float clampf(float value, float low, float high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

static inline int index_for(const EmberState *state, int row, int col) {
  return row * state->width + col;
}

static double rand_unit(void) {
  return (double)rand() / (double)RAND_MAX;
}

static int heat_to_ramp_index(const EmberState *state, int heat) {
  if (state->ramp_len <= 1) {
    return 0;
  }
  double ratio = (double)heat / (double)state->max_heat;
  int index = (int)llround(ratio * (double)(state->ramp_len - 1));
  return clampi(index, 0, (int)state->ramp_len - 1);
}

static void set_cell(EmberState *state, int row, int col, char glyph, int level) {
  if (row < 0 || row >= state->height || col < 0 || col >= state->width) {
    return;
  }

  int idx = index_for(state, row, col);
  int clamped = clampi(level, 0, state->max_heat);
  state->glyphs[idx] = glyph;
  state->glyph_levels[idx] = (uint8_t)clamped;
  state->colors[idx] = (uint8_t)clamped;
}

static void paint_heat(EmberState *state, int row, int col, int level) {
  if (row < 0 || row >= state->height || col < 0 || col >= state->width) {
    return;
  }

  int clamped = clampi(level, 0, state->max_heat);
  int idx = index_for(state, row, col);
  state->glyph_levels[idx] = (uint8_t)clamped;
  state->colors[idx] = (uint8_t)clamped;
  state->glyphs[idx] = state->ramp[heat_to_ramp_index(state, clamped)];
}

static void clear_frame_buffers(EmberState *state) {
  memset(state->glyphs, ' ', (size_t)state->size);
  memset(state->glyph_levels, 0, (size_t)state->size);
  memset(state->colors, 0, (size_t)state->size);
}

static void clear_next_grid(EmberState *state) {
  memset(state->next_grid, 0, sizeof(float) * (size_t)state->size);
}

static void swap_grids(EmberState *state) {
  float *tmp = state->grid;
  state->grid = state->next_grid;
  state->next_grid = tmp;
}

static void decay_grid(EmberState *state, float keep) {
  for (int i = 0; i < state->size; ++i) {
    state->grid[i] *= keep;
  }
}

static void free_kernel(EmberKernel *kernel) {
  free(kernel->points);
  kernel->points = NULL;
  kernel->count = 0;
}

static bool build_kernel(EmberKernel *kernel, double radius, bool circular) {
  free_kernel(kernel);

  int reach = (int)ceil(radius);
  int max_points = (reach * 2 + 1) * (reach * 2 + 1);
  kernel->points = calloc((size_t)max_points, sizeof(*kernel->points));
  if (!kernel->points) {
    return false;
  }

  int count = 0;
  for (int d_row = -reach; d_row <= reach; ++d_row) {
    for (int d_col = -reach; d_col <= reach; ++d_col) {
      double distance_sq = (double)(d_col * d_col + d_row * d_row);
      double normalized;
      if (circular) {
        double distance = sqrt(distance_sq);
        normalized = 1.0 - (distance / radius);
      } else {
        normalized = 1.0 - (distance_sq / (radius * radius));
      }

      if (normalized > 0.0) {
        kernel->points[count].d_row = d_row;
        kernel->points[count].d_col = d_col;
        kernel->points[count].weight = (float)(normalized * normalized);
        ++count;
      }
    }
  }

  kernel->count = count;
  return true;
}

static void configure_wave(EmberState *state, int fps) {
  state->wave.enabled = true;
  state->wave.sway_period_frames = (double)(fps * 6);
  state->wave.breathe_period_frames = (double)(fps * 8.5);
  state->wave.sway_amplitude = 1.35;
  state->wave.radius_mod = 0.08;
  state->wave.height_mod = 0.07;
  state->wave.energy_mod = 0.05;
  state->wave.phase = 0.0;
  state->wave.sway = 0.0;
  state->wave.breathe = 0.0;
}

static void configure_lava(EmberState *state) {
  state->lava.blobs = 4;
  state->lava.speed = 0.16;
  state->lava.pulse_amount = 0.08;
  state->lava.center_bias_x = 0.0;
  state->lava.center_bias_y = 0.0;
}

static void configure_spiral(EmberState *state) {
  state->spiral.turns = 1.85;
  state->spiral.thickness = 1.15;
  state->spiral.rotation_speed = 0.24;
  state->spiral.pulse_amount = 0.08;
  state->spiral.center_bias_x = 0.0;
  state->spiral.center_bias_y = 0.0;
}

static bool init_lava_state(EmberState *state) {
  int area = state->width * state->height;
  int max_blobs = area / 28;
  if (max_blobs < 1) {
    max_blobs = 1;
  }
  state->lava_blob_count = clampi(state->lava.blobs, 1, max_blobs > 6 ? 6 : max_blobs);

  free(state->lava_blobs);
  state->lava_blobs = calloc((size_t)state->lava_blob_count, sizeof(*state->lava_blobs));
  if (!state->lava_blobs) {
    return false;
  }

  double center_x = ((double)state->width + 1.0) * 0.5 + state->lava.center_bias_x;
  double center_y = ((double)state->height + 1.0) * 0.5 + state->lava.center_bias_y;
  double horizontal_span = fmax(1.2, (double)state->width * 0.16);
  double vertical_span = fmax(1.4, (double)state->height * 0.22);

  for (int i = 0; i < 16; ++i) {
    free_kernel(&state->lava_kernels[i]);
  }

  for (int i = 0; i < state->lava_blob_count; ++i) {
    double ratio = (double)(i + 1) / (double)state->lava_blob_count;
    double max_radius = fmax(1.15, fmin((double)state->width * 0.2, (double)state->height * 0.32));
    double base_radius = 1.45 + (ratio * fmin((double)state->width, (double)state->height) * 0.08);
    base_radius = clampd(base_radius, 1.15, max_radius);
    int radius_key = (int)llround(base_radius * 100.0);
    int slot = radius_key % 16;

    if (!state->lava_kernels[slot].points && !build_kernel(&state->lava_kernels[slot], base_radius, false)) {
      return false;
    }

    state->lava_blobs[i].base_x = center_x + cos((double)(i + 1) * 1.7) * horizontal_span * 0.18;
    state->lava_blobs[i].base_y = center_y + sin((double)(i + 1) * 1.1) * vertical_span * 0.15;
    state->lava_blobs[i].radius = base_radius;
    state->lava_blobs[i].energy = 0.72 + ratio * 0.22;
    state->lava_blobs[i].phase = (double)(i + 1) * 1.913;
    state->lava_blobs[i].drift_x = horizontal_span * (0.5 + ratio * 0.3);
    state->lava_blobs[i].drift_y = vertical_span * (0.8 + ratio * 0.25);
    state->lava_blobs[i].rate_x = 0.55 + ratio * 0.22;
    state->lava_blobs[i].rate_y = 0.36 + ratio * 0.18;
    state->lava_blobs[i].pulse_rate = 0.42 + ratio * 0.14;
    state->lava_blobs[i].radius_key = slot;
  }

  return true;
}

static bool init_spiral_state(EmberState *state) {
  state->spiral_kernel_thickness = state->spiral.thickness;
  return build_kernel(&state->spiral_kernel, state->spiral.thickness < 1.35 ? 1.35 : state->spiral.thickness, true);
}

static bool init_scene_state(EmberState *state, int fps) {
  configure_wave(state, fps);
  configure_lava(state);
  configure_spiral(state);
  state->spiral_angle = 0.0;
  state->spiral_pulse = 1.0;
  if (!init_lava_state(state)) {
    return false;
  }
  if (!init_spiral_state(state)) {
    return false;
  }
  return true;
}

static void update_tongues(EmberState *state) {
  for (int col = 0; col < state->width; ++col) {
    double target = rand_unit();
    double previous = state->tongues[col];
    state->tongues[col] = (float)((previous * 0.88) + (target * 0.12));
  }
}

static void update_wave(EmberState *state) {
  if (!state->wave.enabled) {
    return;
  }

  state->wave.phase += 1.0;
  double sway_theta = EMBER_TAU * (state->wave.phase / state->wave.sway_period_frames);
  double breathe_theta = EMBER_TAU * (state->wave.phase / state->wave.breathe_period_frames);
  state->wave.sway = sin(sway_theta) * state->wave.sway_amplitude;
  state->wave.breathe = sin(breathe_theta);
}

static void seed_fire_bottom(EmberState *state) {
  int fuel_row = state->height > 1 ? state->height - 2 : 0;
  double center = ((double)state->width + 1.0) * 0.5;
  int band_half = (int)((double)state->width * 0.16);
  if (band_half < 2) {
    band_half = 2;
  }

  double sway = state->wave.enabled ? state->wave.sway : 0.0;
  double breathe = state->wave.enabled ? state->wave.breathe : 0.0;
  double energy = state->wave.enabled ? (1.0 + state->wave.energy_mod * breathe) : 1.0;

  for (int col = 0; col < state->width; ++col) {
    double distance = fabs((double)(col + 1) - (center + sway * 0.35));
    double target = 0.0;

    if (distance <= (double)band_half) {
      double falloff = 1.0 - (distance / (double)(band_half + 1));
      double pulse = 0.95 + rand_unit() * 0.35;
      double center_bias = 0.72 + falloff * 0.55;
      target = clampd(((double)state->max_heat * center_bias) * pulse * state->intensity * energy, 0.0, (double)state->max_heat);
    } else if (distance <= (double)(band_half + 1) && rand_unit() < 0.35) {
      target = clampd((1.0 + rand_unit() * 1.2) * state->intensity * energy, 0.0, (double)state->max_heat * 0.22);
    }

    int idx = index_for(state, fuel_row, col);
    double previous = state->fuel[col] > 0.0f ? state->fuel[col] : state->grid[idx];
    double smoothed = (previous * 0.78) + (target * 0.22);
    if (target == 0.0 && smoothed < 0.08) {
      smoothed = 0.0;
    }

    state->fuel[col] = (float)smoothed;
    state->grid[idx] = (float)smoothed;
  }
}

static int flame_level(const EmberState *state, int row, int col) {
  double center = ((double)state->width + 1.0) * 0.5;
  int from_bottom = state->height - (row + 1);
  int max_flame_rows = state->height - 2;
  if (max_flame_rows < 3) {
    max_flame_rows = 3;
  }

  if (from_bottom < 1 || from_bottom > max_flame_rows) {
    return -1;
  }

  double normalized_height = (double)(from_bottom - 1) / (double)((state->height - 3) > 0 ? (state->height - 3) : 1);
  double sway = state->wave.enabled ? state->wave.sway : 0.0;
  double breathe = state->wave.enabled ? state->wave.breathe : 0.0;
  double wave_center = center + sway * (0.35 + normalized_height * 0.85);
  double distance = fabs((double)(col + 1) - wave_center);
  double base_radius = (double)state->width * 0.23;
  double top_radius = (double)state->width * 0.045;
  double radius = base_radius * (1.0 - normalized_height) + top_radius * normalized_height;

  if (state->wave.enabled) {
    radius *= (1.0 + state->wave.radius_mod * breathe);
  }

  double tongue = state->tongues[col];
  double effective_height = normalized_height - tongue * 0.28 - (state->wave.enabled ? (state->wave.height_mod * breathe) : 0.0);
  if (effective_height < 0.0) {
    effective_height = 0.0;
  }

  double mask = 1.0 - (distance / (radius > 0.6 ? radius : 0.6));
  if (mask <= 0.0) {
    return 0;
  }

  mask *= mask;
  float heat = state->grid[index_for(state, row, col)];
  double height_fade = 1.05 - effective_height * 0.55;
  double shaped = heat * (0.35 + mask * 1.15) * height_fade;

  if (normalized_height > 0.72) {
    shaped *= (0.55 + tongue * 0.45);
  }

  return clampi((int)llround(shaped), 0, state->max_heat);
}

static void step_fire(EmberState *state) {
  update_wave(state);
  update_tongues(state);
  seed_fire_bottom(state);

  for (int row = state->height - 3; row >= 0; --row) {
    double from_base = (double)(state->height - row - 1) / (double)((state->height - 2) > 0 ? (state->height - 2) : 1);
    double inertia = 0.25 + from_base * 0.35;
    if (row >= state->height - 4) {
      inertia = 0.18;
    } else if (row <= 2) {
      inertia = 0.42;
    }

    for (int col = 0; col < state->width; ++col) {
      int sample_row = row + 1;
      int below_idx = index_for(state, sample_row, col);
      int left_idx = index_for(state, sample_row, state->left_cols[col]);
      int right_idx = index_for(state, sample_row, state->right_cols[col]);
      int drift_col = clampi(col + (rand() % 3) - 1, 0, state->width - 1);
      int drift_idx = index_for(state, sample_row, drift_col);
      float below = state->grid[below_idx];
      float left = state->grid[left_idx];
      float right = state->grid[right_idx];
      float drift = state->grid[drift_idx];
      float average = below * 0.34f + left * 0.18f + right * 0.18f + drift * 0.30f;
      float cooling = (float)(0.45 + rand_unit() * 0.75 + (1.0 - state->intensity) * 1.2);

      if (row <= 1) {
        cooling += 0.5f;
      }

      float next_heat = clampf(average - cooling, 0.0f, (float)state->max_heat);
      int idx = index_for(state, row, col);
      float previous = state->grid[idx];
      state->grid[idx] = previous * (float)inertia + next_heat * (float)(1.0 - inertia);
    }
  }
}

static void render_fire(EmberState *state) {
  static const char log_chars[] = "/____\\";
  int center = state->width / 2;
  int spark_row = state->height - 7;
  if (spark_row < 0) {
    spark_row = 0;
  }
  int log_row = state->height - 1;

  for (int row = 0; row < state->height - 1; ++row) {
    for (int col = 0; col < state->width; ++col) {
      int level = flame_level(state, row, col);
      if (level > 0) {
        paint_heat(state, row, col, level);
      }
    }
  }

  if (state->smoke_life > 0) {
    state->smoke_life -= 1;
  } else if (rand_unit() < 0.14) {
    int smoke_center = center + (int)llround(state->wave.sway);
    state->smoke_col = clampi(smoke_center + (rand() % 5) - 2, 0, state->width - 1);
    state->smoke_life = 3;
  }

  if (state->smoke_life > 0 && state->smoke_col >= 0) {
    set_cell(state, spark_row, state->smoke_col, '.', 4);
  }

  int start_col = center - 2;
  int low = state->max_heat * 0.22;
  if (low < 2) {
    low = 2;
  }
  int mid = state->max_heat * 0.36;
  if (mid <= low) {
    mid = low + 1;
  }

  for (int i = 0; i < 6; ++i) {
    int col = start_col + i;
    if (col < 0 || col >= state->width) {
      continue;
    }
    int level = (i >= 1 && i <= 4) ? mid : low;
    set_cell(state, log_row, col, log_chars[i], level);
  }
}

static void blend_heat(EmberState *state, int row, int col, float heat, bool additive) {
  if (row < 0 || row >= state->height || col < 0 || col >= state->width) {
    return;
  }

  int idx = index_for(state, row, col);
  if (additive) {
    state->grid[idx] = clampf(state->grid[idx] + heat, 0.0f, (float)state->max_heat);
  } else {
    if (heat > state->grid[idx]) {
      state->grid[idx] = clampf(heat, 0.0f, (float)state->max_heat);
    }
  }
}

static void stamp_lava_blob(EmberState *state, const EmberBlob *blob, double x, double y, double radius_scale) {
  const EmberKernel *kernel = &state->lava_kernels[blob->radius_key];
  int row = (int)llround(y) - 1;
  int col = (int)llround(x) - 1;
  float heat = (float)((double)state->max_heat * blob->energy * state->intensity * radius_scale);

  for (int i = 0; i < kernel->count; ++i) {
    const EmberKernelPoint *point = &kernel->points[i];
    blend_heat(state, row + point->d_row, col + point->d_col, heat * point->weight, true);
  }
}

static void smooth_lava(EmberState *state) {
  for (int row = 0; row < state->height; ++row) {
    int up = row > 0 ? row - 1 : 0;
    int down = row + 1 < state->height ? row + 1 : state->height - 1;

    for (int col = 0; col < state->width; ++col) {
      int left = state->left_cols[col];
      int right = state->right_cols[col];
      int idx = index_for(state, row, col);
      float current = state->grid[idx];
      float smoothed = current * 0.52f
        + state->grid[index_for(state, row, left)] * 0.12f
        + state->grid[index_for(state, row, right)] * 0.12f
        + state->grid[index_for(state, up, col)] * 0.12f
        + state->grid[index_for(state, down, col)] * 0.12f;
      state->next_grid[idx] = clampf(smoothed - 0.05f, 0.0f, (float)state->max_heat);
    }
  }

  swap_grids(state);
}

static void step_lava(EmberState *state) {
  double center_x = ((double)state->width + 1.0) * 0.5 + state->lava.center_bias_x;
  double center_y = ((double)state->height + 1.0) * 0.5 + state->lava.center_bias_y;
  double horizontal_limit = fmax(1.2, (double)state->width * 0.24);
  double vertical_limit = fmax(1.6, (double)state->height * 0.28);

  state->phase += state->lava.speed;
  decay_grid(state, 0.76f);

  for (int i = 0; i < state->lava_blob_count; ++i) {
    EmberBlob *blob = &state->lava_blobs[i];
    double phase = state->phase + blob->phase;
    double pulse = 1.0 + sin(phase * blob->pulse_rate) * state->lava.pulse_amount;
    double drift_x = sin(phase * blob->rate_x) * blob->drift_x;
    double drift_y = cos(phase * blob->rate_y) * blob->drift_y;
    double x = center_x + clampd(drift_x, -horizontal_limit, horizontal_limit);
    double y = center_y + clampd(drift_y, -vertical_limit, vertical_limit);
    stamp_lava_blob(state, blob, x, y, pulse);
  }

  smooth_lava(state);
}

static void render_lava(EmberState *state) {
  int threshold = state->max_heat * 0.12;
  if (threshold < 1) {
    threshold = 1;
  }

  for (int row = 0; row < state->height; ++row) {
    int base = row * state->width;
    for (int col = 0; col < state->width; ++col) {
      int level = clampi((int)llround(state->grid[base + col]), 0, state->max_heat);
      if (level > threshold) {
        paint_heat(state, row, col, level);
      }
    }
  }
}

static int sample_count_for(const EmberState *state) {
  int area_scaled = (int)((double)(state->width * state->height) * 0.55);
  if (area_scaled < 96) {
    return 96;
  }
  if (area_scaled > 512) {
    return 512;
  }
  return area_scaled;
}

static void diffuse_spiral(EmberState *state) {
  for (int row = 0; row < state->height; ++row) {
    int up = row > 0 ? row - 1 : 0;
    int down = row + 1 < state->height ? row + 1 : state->height - 1;

    for (int col = 0; col < state->width; ++col) {
      int left = state->left_cols[col];
      int right = state->right_cols[col];
      int idx = index_for(state, row, col);
      float total = state->grid[idx] * 0.42f;
      float weight = 0.42f;

      total += state->grid[index_for(state, row, left)] * 0.09f;
      total += state->grid[index_for(state, row, right)] * 0.09f;
      total += state->grid[index_for(state, up, col)] * 0.09f;
      total += state->grid[index_for(state, down, col)] * 0.09f;
      weight += 0.36f;

      total += state->grid[index_for(state, up, left)] * 0.045f;
      total += state->grid[index_for(state, up, right)] * 0.045f;
      total += state->grid[index_for(state, down, left)] * 0.045f;
      total += state->grid[index_for(state, down, right)] * 0.045f;
      weight += 0.18f;

      float smoothed = total / weight;
      float cooling = (float)(0.18 + rand_unit() * 0.32 + (1.0 - state->intensity) * 0.68);
      state->next_grid[idx] = clampf(smoothed - cooling, 0.0f, (float)state->max_heat);
    }
  }

  swap_grids(state);
}

static void enrich_spiral_hotspots(EmberState *state) {
  for (int i = 0; i < state->size; ++i) {
    float current = state->grid[i];
    if (current > (float)state->max_heat * 0.3f) {
      float normalized = current / (float)state->max_heat;
      float boost = 1.0f + ((normalized - 0.3f) / 0.7f) * 0.28f;
      state->grid[i] = clampf(current * boost, 0.0f, (float)state->max_heat);
    }
  }
}

static void seed_spiral(EmberState *state) {
  double thickness = state->spiral.thickness < 1.35 ? 1.35 : state->spiral.thickness;
  if (fabs(state->spiral_kernel_thickness - thickness) > 0.0001) {
    build_kernel(&state->spiral_kernel, thickness, true);
    state->spiral_kernel_thickness = thickness;
  }

  double center_x = ((double)state->width + 1.0) * 0.5 + state->spiral.center_bias_x;
  double center_y = ((double)state->height + 1.0) * 0.5 + state->spiral.center_bias_y;
  double max_radius = fmax(2.2, fmin((double)state->width, (double)state->height) * 0.47);
  int samples = sample_count_for(state);
  double trail_length = EMBER_TAU * state->spiral.turns;
  double angle_step = -trail_length / (double)samples;
  double cos_step = cos(angle_step);
  double sin_step = sin(angle_step);

  state->spiral_angle += fabs(state->spiral.rotation_speed);
  state->phase += 1.0;
  state->spiral_pulse = 1.0 + sin(state->phase * 0.09) * state->spiral.pulse_amount;

  double cos_theta = cos(state->spiral_angle);
  double sin_theta = sin(state->spiral_angle);
  double radius = 0.65 * state->spiral_pulse;
  double radius_step = max_radius / (double)samples;

  for (int i = 0; i <= samples; ++i) {
    double progress = (double)i / (double)samples;
    double x = center_x + cos_theta * radius;
    double y = center_y + sin_theta * radius * 0.72;
    int row = (int)llround(y) - 1;
    int col = (int)llround(x) - 1;
    double lead = 1.0 - progress;
    float heat = (float)((double)state->max_heat * (0.5 + lead * 0.5) * state->intensity);

    for (int j = 0; j < state->spiral_kernel.count; ++j) {
      const EmberKernelPoint *point = &state->spiral_kernel.points[j];
      float glow_heat = heat * point->weight * (float)(0.92 + rand_unit() * 0.12);
      blend_heat(state, row + point->d_row, col + point->d_col, glow_heat, false);
    }

    double next_cos = cos_theta * cos_step - sin_theta * sin_step;
    double next_sin = sin_theta * cos_step + cos_theta * sin_step;
    cos_theta = next_cos;
    sin_theta = next_sin;
    radius += radius_step * state->spiral_pulse;
  }
}

static void step_spiral(EmberState *state) {
  decay_grid(state, 0.82f);
  seed_spiral(state);
  diffuse_spiral(state);
  enrich_spiral_hotspots(state);
}

static void render_spiral(EmberState *state) {
  for (int row = 0; row < state->height; ++row) {
    int base = row * state->width;
    for (int col = 0; col < state->width; ++col) {
      int level = clampi((int)llround(state->grid[base + col]), 0, state->max_heat);
      if (level > 1) {
        paint_heat(state, row, col, level);
      }
    }
  }
}

static void compute_dirty_rows(EmberState *state) {
  for (int row = 0; row < state->height; ++row) {
    int base = row * state->width;
    state->row_hashes[row] = hash_row(state->glyphs, state->colors, base, state->width);
    state->dirty_rows[row] = state->row_hashes[row] != state->prev_row_hashes[row];
  }
}

bool ember_state_init(EmberState *state, const EmberOptions *options, int width, int height) {
  memset(state, 0, sizeof(*state));
  state->width = width;
  state->height = height;
  state->size = width * height;
  state->max_heat = EMBER_MAX_HEAT;
  state->scene = options->scene;
  state->palette = options->palette;
  state->intensity = 0.65;
  state->smoke_col = -1;
  state->ramp = options->chars ? options->chars : DEFAULT_RAMP;
  state->ramp_len = strlen(state->ramp);
  if (state->ramp_len < 2) {
    state->ramp = DEFAULT_RAMP;
    state->ramp_len = strlen(DEFAULT_RAMP);
  }

  state->grid = calloc((size_t)state->size, sizeof(*state->grid));
  state->next_grid = calloc((size_t)state->size, sizeof(*state->next_grid));
  state->glyph_levels = calloc((size_t)state->size, sizeof(*state->glyph_levels));
  state->prev_levels = calloc((size_t)state->size, sizeof(*state->prev_levels));
  state->glyphs = calloc((size_t)state->size, sizeof(*state->glyphs));
  state->prev_glyphs = calloc((size_t)state->size, sizeof(*state->prev_glyphs));
  state->colors = calloc((size_t)state->size, sizeof(*state->colors));
  state->prev_colors = calloc((size_t)state->size, sizeof(*state->prev_colors));
  state->left_cols = calloc((size_t)state->width, sizeof(*state->left_cols));
  state->right_cols = calloc((size_t)state->width, sizeof(*state->right_cols));
  state->row_hashes = calloc((size_t)state->height, sizeof(*state->row_hashes));
  state->prev_row_hashes = calloc((size_t)state->height, sizeof(*state->prev_row_hashes));
  state->dirty_rows = calloc((size_t)state->height, sizeof(*state->dirty_rows));
  state->fuel = calloc((size_t)state->width, sizeof(*state->fuel));
  state->tongues = calloc((size_t)state->width, sizeof(*state->tongues));

  if (!state->grid || !state->next_grid || !state->glyph_levels || !state->prev_levels ||
      !state->glyphs || !state->prev_glyphs || !state->colors || !state->prev_colors ||
      !state->left_cols || !state->right_cols || !state->row_hashes || !state->prev_row_hashes ||
      !state->dirty_rows || !state->fuel || !state->tongues) {
    ember_state_free(state);
    return false;
  }

  memset(state->glyphs, ' ', (size_t)state->size);
  memset(state->prev_glyphs, ' ', (size_t)state->size);

  for (int col = 0; col < state->width; ++col) {
    state->left_cols[col] = col > 0 ? col - 1 : 0;
    state->right_cols[col] = col + 1 < state->width ? col + 1 : state->width - 1;
  }

  if (!init_scene_state(state, options->fps)) {
    ember_state_free(state);
    return false;
  }

  return true;
}

void ember_state_free(EmberState *state) {
  free(state->grid);
  free(state->next_grid);
  free(state->glyph_levels);
  free(state->prev_levels);
  free(state->glyphs);
  free(state->prev_glyphs);
  free(state->colors);
  free(state->prev_colors);
  free(state->left_cols);
  free(state->right_cols);
  free(state->row_hashes);
  free(state->prev_row_hashes);
  free(state->dirty_rows);
  free(state->fuel);
  free(state->tongues);
  free(state->lava_blobs);
  for (int i = 0; i < 16; ++i) {
    free_kernel(&state->lava_kernels[i]);
  }
  free_kernel(&state->spiral_kernel);
  memset(state, 0, sizeof(*state));
}

bool ember_state_resize(EmberState *state, int width, int height) {
  EmberOptions options;
  ember_options_init(&options);
  options.scene = state->scene;
  options.palette = state->palette;
  options.chars = state->ramp;
  EmberState replacement;
  if (!ember_state_init(&replacement, &options, width, height)) {
    return false;
  }
  replacement.intensity = state->intensity;
  ember_state_free(state);
  *state = replacement;
  return true;
}

void ember_render_frame(EmberState *state) {
  state->frame_no += 1;
  clear_frame_buffers(state);
  clear_next_grid(state);

  switch (state->scene) {
    case EMBER_SCENE_FIRE:
      step_fire(state);
      render_fire(state);
      break;
    case EMBER_SCENE_LAVA:
      step_lava(state);
      render_lava(state);
      break;
    case EMBER_SCENE_SPIRAL:
      step_spiral(state);
      render_spiral(state);
      break;
  }

  compute_dirty_rows(state);
}

void ember_copy_frame_as_previous(EmberState *state) {
  memcpy(state->prev_glyphs, state->glyphs, (size_t)state->size);
  memcpy(state->prev_levels, state->glyph_levels, (size_t)state->size);
  memcpy(state->prev_colors, state->colors, (size_t)state->size);
  memcpy(state->prev_row_hashes, state->row_hashes, sizeof(*state->row_hashes) * (size_t)state->height);
}

void ember_print_frame(const EmberState *state) {
  for (int row = 0; row < state->height; ++row) {
    fwrite(&state->glyphs[row * state->width], 1, (size_t)state->width, stdout);
    fputc('\n', stdout);
  }
}

static double timespec_to_ms(const struct timespec *start, const struct timespec *end) {
  long seconds = end->tv_sec - start->tv_sec;
  long nanoseconds = end->tv_nsec - start->tv_nsec;
  return (double)seconds * 1000.0 + (double)nanoseconds / 1000000.0;
}

void ember_run_benchmark(const EmberOptions *options) {
  EmberState state;
  if (!ember_state_init(&state, options, options->width, options->height)) {
    fprintf(stderr, "failed to initialize benchmark state\n");
    return;
  }

  EmberBenchmark bench = {0};
  bench.min_ms = DBL_MAX;
  for (int i = 0; i < options->benchmark_frames; ++i) {
    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    ember_render_frame(&state);
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = timespec_to_ms(&start, &end);
    bench.frames += 1;
    bench.total_ms += elapsed;
    if (elapsed < bench.min_ms) {
      bench.min_ms = elapsed;
    }
    if (elapsed > bench.max_ms) {
      bench.max_ms = elapsed;
    }
    ember_copy_frame_as_previous(&state);
  }

  bench.average_ms = bench.total_ms / (double)bench.frames;
  printf("benchmark scene=%s size=%dx%d frames=%lld avg=%.3fms min=%.3fms max=%.3fms\n",
         ember_scene_name(options->scene), options->width, options->height,
         bench.frames, bench.average_ms, bench.min_ms, bench.max_ms);

  ember_state_free(&state);
}
