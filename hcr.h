#pragma once
#include "types.h"
#include "memory.h"
#include "keyboard.h"
#include "math.h"

extern uint32_t* g_fb;
extern uint32_t g_width;
extern uint32_t g_max_y;
extern "C" volatile uint64_t timer_ticks;
extern "C" void fb_acquire();
extern "C" void fb_release();
extern "C" void* malloc(uint64_t size);
extern "C" void free(void* ptr);
extern "C" void* memcpy(void* dst, const void* src, unsigned long long n);
extern "C" void clear_screen(uint32_t* fb, uint32_t width, uint32_t height, uint32_t color);
extern "C" void memset32(void* dst, uint32_t val, unsigned long long count32);
extern "C" volatile bool g_hcr_running;
void draw_string(uint32_t* fb, uint32_t width, const char* str, uint32_t x, uint32_t y, uint32_t color);

static inline void hcr_itoa(int num, char* str) {
    int i = 0;
    bool isNegative = false;
    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }
    if (num < 0) {
        isNegative = true;
        num = -num;
    }
    while (num != 0) {
        int rem = num % 10;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / 10;
    }
    if (isNegative) str[i++] = '-';
    str[i] = '\0';
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

static inline void hcr_format_tenths(float value, char* str) {
    int tenths = (int)(value * 10.0f);
    if (tenths < 0) tenths = 0;
    int whole = tenths / 10;
    int frac = tenths % 10;

    hcr_itoa(whole, str);
    int len = 0;
    while (str[len]) len++;
    str[len++] = '.';
    str[len++] = (char)('0' + frac);
    str[len] = '\0';
}

// Car parameters
const float HCR_CAR_WIDTH = 42.0f;
const float HCR_CAR_HEIGHT = 16.0f;
const float HCR_WHEEL_RADIUS = 7.0f;
const float HCR_GRAVITY = 0.18f;
const float HCR_GROUND_ACCEL = 0.20f;
const float HCR_BRAKE_ACCEL = 0.14f;
const float HCR_AIR_CONTROL = 0.018f;
const float HCR_MAX_FUEL_LITERS = 35.0f;
const float HCR_PI = 3.14159265f;
const float HCR_TWO_PI = 6.28318530f;

static inline float hcr_sin(float x) {
    return (float)cos((double)(HCR_PI * 0.5f - x));
}

static inline float hcr_abs(float x) {
    return x < 0.0f ? -x : x;
}

static inline float hcr_clamp(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline float hcr_wrap_angle(float x) {
    while (x > HCR_PI) x -= HCR_TWO_PI;
    while (x < -HCR_PI) x += HCR_TWO_PI;
    return x;
}

// Terrain function. Multiple long and short waves keep the route varied
// without creating vertical walls that the tiny physics model cannot handle.
float hcr_get_terrain(float x) {
    float base = 500.0f
        + 62.0f * (float)cos(x * 0.0065f)
        + 46.0f * (float)cos(x * 0.0130f + 1.1f)
        + 24.0f * (float)cos(x * 0.0310f + 2.4f)
        + 10.0f * (float)cos(x * 0.0710f);
    float rolling = 18.0f * (float)cos((x + 350.0f) * 0.0040f) * (float)cos(x * 0.0180f);
    return base + rolling;
}

float hcr_get_terrain_angle(float x) {
    float left = hcr_get_terrain(x - 18.0f);
    float right = hcr_get_terrain(x + 18.0f);
    return (float)my_atan((double)((right - left) / 36.0f));
}

// Simple color helper
static inline uint32_t hcr_color(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void hcr_draw_rect_fast(uint32_t* fb, int x, int y, int w, int h, uint32_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)g_width) w = (int)g_width - x;
    if (y + h > (int)g_max_y + 1) h = (int)g_max_y + 1 - y;
    if (w <= 0 || h <= 0) return;

    for (int i = 0; i < h; i++) {
        memset32(&fb[(y + i) * g_width + x], color, w);
    }
}

static inline void hcr_put_pixel(uint32_t* fb, int x, int y, uint32_t color) {
    if (x >= 0 && x < (int)g_width && y >= 0 && y <= (int)g_max_y) {
        fb[y * g_width + x] = color;
    }
}

void hcr_draw_line(uint32_t* fb, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        hcr_put_pixel(fb, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void hcr_draw_rotated_rect(uint32_t* fb, int cx, int cy, float angle, int half_w, int half_h, uint32_t color_top, uint32_t color_bottom) {
    float ca = (float)cos(angle);
    float sa = hcr_sin(angle);

    for (int ly = -half_h; ly <= half_h; ly++) {
        for (int lx = -half_w; lx <= half_w; lx++) {
            float rx = (float)lx * ca - (float)ly * sa;
            float ry = (float)lx * sa + (float)ly * ca;
            uint32_t color = ly < -2 ? color_top : color_bottom;
            hcr_put_pixel(fb, cx + (int)rx, cy + (int)ry, color);
        }
    }
}

void hcr_draw_wheel(uint32_t* fb, int cx, int cy, float spin) {
    uint32_t rubber = hcr_color(24, 25, 27);
    uint32_t rim = hcr_color(168, 174, 176);
    uint32_t hub = hcr_color(220, 225, 220);

    for (int y = -7; y <= 7; y++) {
        for (int x = -7; x <= 7; x++) {
            int r2 = x * x + y * y;
            if (r2 <= 49) {
                hcr_put_pixel(fb, cx + x, cy + y, r2 <= 12 ? rim : rubber);
            }
        }
    }

    hcr_put_pixel(fb, cx, cy, hub);
    float ca = (float)cos(spin);
    float sa = hcr_sin(spin);
    hcr_draw_line(fb, cx - (int)(ca * 6.0f), cy - (int)(sa * 6.0f), cx + (int)(ca * 6.0f), cy + (int)(sa * 6.0f), hub);
    hcr_draw_line(fb, cx - (int)(-sa * 6.0f), cy - (int)(ca * 6.0f), cx + (int)(-sa * 6.0f), cy + (int)(ca * 6.0f), hub);
}

void hcr_transform(float cx, float cy, float angle, float lx, float ly, float* out_x, float* out_y) {
    float ca = (float)cos(angle);
    float sa = hcr_sin(angle);
    *out_x = cx + lx * ca - ly * sa;
    *out_y = cy + lx * sa + ly * ca;
}

void hcr_game_loop() {
    g_hcr_running = true;
    float car_x = 200.0f;
    float car_y = hcr_get_terrain(car_x) - 24.0f;
    float car_vx = 0.0f;
    float car_vy = 0.0f;
    float car_angle = 0.0f;
    float car_vangle = 0.0f;
    float wheel_spin = 0.0f;
    float fuel_liters = HCR_MAX_FUEL_LITERS;
    float distance = 0.0f;
    bool game_over = false;

    uint32_t* back_buffer = (uint32_t*)malloc(g_width * (g_max_y + 1) * 4);
    if (!back_buffer) {
        g_hcr_running = false;
        return;
    }

    int* heights = (int*)malloc(g_width * sizeof(int));
    if (!heights) {
        free(back_buffer);
        g_hcr_running = false;
        return;
    }

    keyboard_reset_state();

    while (!game_over) {
        uint64_t start_ticks = timer_ticks;

        // Input: Right Arrow (Gas), Left Arrow (Brake/Reverse), ESC (Exit)
        bool gas = key_state_ext[0x4D];
        bool brake = key_state_ext[0x4B];
        bool exit = key_state[0x01];

        if (exit) break;

        float rear_x, rear_y, front_x, front_y;
        hcr_transform(car_x, car_y, car_angle, -15.5f, 7.0f, &rear_x, &rear_y);
        hcr_transform(car_x, car_y, car_angle, 15.5f, 7.0f, &front_x, &front_y);

        float rear_ground = hcr_get_terrain(rear_x);
        float front_ground = hcr_get_terrain(front_x);
        float rear_pen = rear_y + HCR_WHEEL_RADIUS - rear_ground;
        float front_pen = front_y + HCR_WHEEL_RADIUS - front_ground;
        bool rear_on_ground = rear_pen >= -2.0f;
        bool front_on_ground = front_pen >= -2.0f;
        bool grounded = rear_on_ground || front_on_ground;
        float terrain_angle = hcr_get_terrain_angle(car_x);

        if (fuel_liters > 0.0f) {
            if (gas && grounded) {
                car_vx += (float)cos(terrain_angle) * HCR_GROUND_ACCEL;
                car_vy += hcr_sin(terrain_angle) * HCR_GROUND_ACCEL;
                fuel_liters -= 0.018f;
            }
            if (brake && grounded) {
                car_vx -= (float)cos(terrain_angle) * HCR_BRAKE_ACCEL;
                car_vy -= hcr_sin(terrain_angle) * HCR_BRAKE_ACCEL;
                fuel_liters -= 0.011f;
            }
        }

        fuel_liters -= 0.0025f;
        if (fuel_liters < 0.0f) fuel_liters = 0.0f;

        if (!grounded) {
            if (gas) car_vangle += HCR_AIR_CONTROL;
            if (brake) car_vangle -= HCR_AIR_CONTROL;
        }

        car_vy += HCR_GRAVITY;
        car_x += car_vx;
        car_y += car_vy;

        hcr_transform(car_x, car_y, car_angle, -15.5f, 7.0f, &rear_x, &rear_y);
        hcr_transform(car_x, car_y, car_angle, 15.5f, 7.0f, &front_x, &front_y);
        rear_ground = hcr_get_terrain(rear_x);
        front_ground = hcr_get_terrain(front_x);
        rear_pen = rear_y + HCR_WHEEL_RADIUS - rear_ground;
        front_pen = front_y + HCR_WHEEL_RADIUS - front_ground;

        if (rear_pen > 0.0f || front_pen > 0.0f) {
            float lift = 0.0f;
            int contacts = 0;
            if (rear_pen > 0.0f) { lift += rear_pen; contacts++; }
            if (front_pen > 0.0f) { lift += front_pen; contacts++; }
            car_y -= lift / (float)contacts;
            if (car_vy > 0.0f) car_vy *= -0.08f;

            float desired_angle = (float)my_atan((double)((front_ground - rear_ground) / (front_x - rear_x)));
            float angle_diff = hcr_wrap_angle(desired_angle - car_angle);
            car_vangle += hcr_clamp(angle_diff * 0.10f, -0.035f, 0.035f);

            float slope_pull = hcr_sin(terrain_angle) * HCR_GRAVITY * 0.55f;
            car_vx -= slope_pull;
            car_vx *= 0.992f;
            wheel_spin += car_vx / HCR_WHEEL_RADIUS;
        } else {
            car_vx *= 0.996f;
        }

        car_vy *= 0.990f;
        car_angle += car_vangle;
        car_angle = hcr_wrap_angle(car_angle);
        car_vangle *= grounded ? 0.82f : 0.985f;
        distance = (car_x - 200.0f) / 10.0f;

        // Rendering
        for (int x = 0; x < (int)g_width; x++) {
            float world_x = car_x - (g_width / 2) + x;
            float ty = hcr_get_terrain(world_x);
            int draw_ty = (int)ty;
            if (draw_ty < 0) draw_ty = 0;
            if (draw_ty > (int)g_max_y) draw_ty = (int)g_max_y;
            heights[x] = draw_ty;
        }

        // Draw sky and terrain
        for (int y = 0; y <= (int)g_max_y; y++) {
            uint32_t sky_color = hcr_color(100, 150 + (y * 100 / (int)g_max_y), 255);
            uint32_t grass_top = hcr_color(48, 176, 55);
            uint32_t grass_bot = hcr_color(38, 134, 46);
            uint32_t soil_color = hcr_color(83, 58, 36);
            uint32_t soil_dark = hcr_color(63, 43, 28);
            
            uint32_t* row = &back_buffer[y * g_width];
            for (int x = 0; x < (int)g_width; x++) {
                int ty = heights[x];
                if (y < ty) row[x] = sky_color;
                else if (y <= ty + 2) row[x] = ((x + y) & 3) ? grass_top : hcr_color(82, 205, 62);
                else if (y <= ty + 8) row[x] = grass_bot;
                else row[x] = (((x * 3 + y * 5) & 31) < 4) ? soil_dark : soil_color;
            }
        }

        for (int x = 0; x < (int)g_width; x += 7) {
            float world_x = car_x - (g_width / 2) + x;
            int blade = 4 + ((int)(world_x * 13.0f) & 7);
            int sway = ((int)(world_x + timer_ticks) & 3) - 1;
            int y = heights[x];
            hcr_draw_line(back_buffer, x, y, x + sway, y - blade, hcr_color(32, 150, 42));
        }

        // Draw car (Red Sports Car)
        int scx = (int)(g_width / 2);
        int scy = (int)car_y;
        hcr_draw_rotated_rect(back_buffer, scx, scy, car_angle, 21, 8, hcr_color(185, 30, 30), hcr_color(238, 55, 45));
        hcr_draw_rotated_rect(back_buffer, scx - 2, scy - 7, car_angle, 10, 4, hcr_color(170, 218, 245), hcr_color(130, 190, 228));

        float w1x, w1y, w2x, w2y;
        hcr_transform((float)scx, (float)scy, car_angle, -15.5f, 7.0f, &w1x, &w1y);
        hcr_transform((float)scx, (float)scy, car_angle, 15.5f, 7.0f, &w2x, &w2y);
        hcr_draw_wheel(back_buffer, (int)w1x, (int)w1y, wheel_spin);
        hcr_draw_wheel(back_buffer, (int)w2x, (int)w2y, wheel_spin);

        // UI: Teksty na ekranie
        char buf[32];
        
        // Paliwo (Bar + Text)
        float fuel_percent = (fuel_liters / HCR_MAX_FUEL_LITERS) * 100.0f;
        hcr_draw_rect_fast(back_buffer, 20, 20, 204, 24, hcr_color(42, 45, 48));
        hcr_draw_rect_fast(back_buffer, 22, 22, (int)(200 * (fuel_liters / HCR_MAX_FUEL_LITERS)), 20, hcr_color(245, 190, 30));
        draw_string(back_buffer, g_width, "PALIWO:", 230, 28, hcr_color(255, 255, 255));
        hcr_itoa((int)fuel_percent, buf);
        draw_string(back_buffer, g_width, buf, 290, 28, hcr_color(255, 255, 255));
        draw_string(back_buffer, g_width, "%", 320, 28, hcr_color(255, 255, 255));
        hcr_format_tenths(fuel_liters, buf);
        draw_string(back_buffer, g_width, buf, 345, 28, hcr_color(255, 255, 255));
        draw_string(back_buffer, g_width, "L", 390, 28, hcr_color(255, 255, 255));
        
        // Predkosc
        int speed = (int)(hcr_abs(car_vx) * 14.0f);
        draw_string(back_buffer, g_width, "PREDKOSC:", 20, 55, hcr_color(255, 255, 255));
        hcr_itoa(speed, buf);
        draw_string(back_buffer, g_width, buf, 100, 55, hcr_color(255, 255, 255));
        draw_string(back_buffer, g_width, "km/h", 130, 55, hcr_color(255, 255, 255));
        
        // Dystans
        draw_string(back_buffer, g_width, "DYSTANS:", 20, 70, hcr_color(255, 255, 255));
        hcr_itoa((int)distance, buf);
        draw_string(back_buffer, g_width, buf, 90, 70, hcr_color(255, 255, 255));
        draw_string(back_buffer, g_width, "m", 150, 70, hcr_color(255, 255, 255));
        
        // Spalanie
        draw_string(back_buffer, g_width, "SPALANIE:", 20, 85, hcr_color(255, 255, 255));
        int consumption = 2;
        if (gas && fuel_liters > 0.0f) consumption += 28;
        if (brake && fuel_liters > 0.0f) consumption += 13;
        hcr_itoa(consumption, buf);
        draw_string(back_buffer, g_width, buf, 100, 85, hcr_color(255, 255, 255));
        draw_string(back_buffer, g_width, "L/100km", 130, 85, hcr_color(255, 255, 255));

        // Blit
        fb_acquire();
        memcpy(g_fb, back_buffer, (uint64_t)g_width * (g_max_y + 1) * 4);
        fb_release();

        // Frame timing (~60 FPS)
        while (timer_ticks - start_ticks < 1); 
    }

    free(heights);
    free(back_buffer);
    // Cleanup screen
    fb_acquire();
    clear_screen(g_fb, g_width, g_max_y + 1, 0);
    fb_release();
    g_hcr_running = false;
}
