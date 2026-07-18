#include <pebble.h>

// ─── MARATHON palette ────────────────────────────────────────────────
// Acid yellow-green on void black; magenta for Runner alert states.
#define COLOR_ACID   PBL_IF_COLOR_ELSE(GColorSpringBud, GColorWhite)
#define COLOR_ALERT  PBL_IF_COLOR_ELSE(GColorMagenta,   GColorWhite)
#define COLOR_DIM    PBL_IF_COLOR_ELSE(GColorDarkGreen, GColorWhite)
#define COLOR_VOID   GColorBlack

static Window    *s_window;
static Layer     *s_canvas_layer;
static TextLayer *s_top_layer;
static TextLayer *s_signal_layer;
static TextLayer *s_weather_layer;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_battery_layer;
static GFont      s_font_big;
static GFont      s_font_small;

static char s_time_buf[12];
static char s_date_raw_buf[20];
static char s_date_buf[20];
static char s_battery_buf[30];
static char s_signal_buf[22];

static int  s_battery = 100;
static bool s_bt_connected = true;

// ─── Rendering ───────────────────────────────────────────────────────
static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect b = layer_get_bounds(layer);
    GColor fg = s_bt_connected ? COLOR_ACID : COLOR_ALERT;

    graphics_context_set_stroke_color(ctx, fg);
    graphics_context_set_fill_color(ctx, fg);
    graphics_context_set_stroke_width(ctx, 2);

    // Corner brackets — HUD reticle framing
    const int m = 6, len = 14;
    // top-left
    graphics_draw_line(ctx, GPoint(m, m), GPoint(m + len, m));
    graphics_draw_line(ctx, GPoint(m, m), GPoint(m, m + len));
    // top-right
    graphics_draw_line(ctx, GPoint(b.size.w - m, m), GPoint(b.size.w - m - len, m));
    graphics_draw_line(ctx, GPoint(b.size.w - m, m), GPoint(b.size.w - m, m + len));
    // bottom-left
    graphics_draw_line(ctx, GPoint(m, b.size.h - m), GPoint(m + len, b.size.h - m));
    graphics_draw_line(ctx, GPoint(m, b.size.h - m), GPoint(m, b.size.h - m - len));
    // bottom-right
    graphics_draw_line(ctx, GPoint(b.size.w - m, b.size.h - m), GPoint(b.size.w - m - len, b.size.h - m));
    graphics_draw_line(ctx, GPoint(b.size.w - m, b.size.h - m), GPoint(b.size.w - m, b.size.h - m - len));

    // Rule lines above and below the time block
    int rule_y1 = b.size.h / 2 - 32;
    int rule_y2 = b.size.h / 2 + 30;
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_line(ctx, GPoint(m + 8, rule_y1), GPoint(b.size.w - m - 8, rule_y1));
    graphics_draw_line(ctx, GPoint(m + 8, rule_y2), GPoint(b.size.w - m - 8, rule_y2));

    // Battery: 10 segmented blocks, Marathon-style telemetry bar
    int seg_w = 8, seg_h = 5, gap = 3;
    int total = 10 * seg_w + 9 * gap;
    int bx = (b.size.w - total) / 2;
    int by = b.size.h - m - seg_h - 8;
    int lit = (s_battery + 9) / 10;
    for (int i = 0; i < 10; i++) {
        GRect seg = GRect(bx + i * (seg_w + gap), by, seg_w, seg_h);
        if (i < lit) {
            graphics_fill_rect(ctx, seg, 0, GCornerNone);
        } else {
            graphics_draw_rect(ctx, seg);
        }
    }

    // Small "signal" triangle glyph, top-center
    GPoint apex = GPoint(b.size.w / 2, m + 2);
    graphics_draw_line(ctx, GPoint(apex.x - 5, apex.y + 8), GPoint(apex.x, apex.y));
    graphics_draw_line(ctx, GPoint(apex.x + 5, apex.y + 8), GPoint(apex.x, apex.y));
    graphics_draw_line(ctx, GPoint(apex.x - 5, apex.y + 8), GPoint(apex.x + 5, apex.y + 8));
}

// ─── State updates ───────────────────────────────────────────────────
static void update_signal(void) {
    if (s_bt_connected) {
        snprintf(s_signal_buf, sizeof(s_signal_buf), "LOC: TAU CETI IV");
        text_layer_set_text_color(s_top_layer, COLOR_ACID);
        text_layer_set_text_color(s_signal_layer, COLOR_ACID);
        text_layer_set_text_color(s_weather_layer, COLOR_ACID);
        text_layer_set_text_color(s_battery_layer, COLOR_ACID);
    } else {
        snprintf(s_signal_buf, sizeof(s_signal_buf), "LOC: !! NO SIGNAL !!");
        text_layer_set_text_color(s_top_layer, COLOR_ALERT);
        text_layer_set_text_color(s_signal_layer, COLOR_ALERT);
        text_layer_set_text_color(s_weather_layer, COLOR_ALERT);
        text_layer_set_text_color(s_battery_layer, COLOR_ALERT);
    }
    text_layer_set_text(s_signal_layer, s_signal_buf);
}

static void update_weather(void) {
}

static void update_time(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    strftime(s_time_buf, sizeof(s_time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", t);
    text_layer_set_text(s_time_layer, s_time_buf);

    strftime(s_date_raw_buf, sizeof(s_date_raw_buf), "%a. %b %d %Y", t);
    // Maratype has no lowercase — force caps
    for (char *p = s_date_raw_buf; *p; p++) {
        if (*p >= 'a' && *p <= 'z') *p -= 32;
    }
    snprintf(s_date_buf, sizeof(s_date_buf), "%s", s_date_raw_buf);
    text_layer_set_text(s_date_layer, s_date_buf);
}

static void update_battery(void) {
    snprintf(s_battery_buf, sizeof(s_battery_buf), "SHELL INTEGRITY %d%%", s_battery);
    text_layer_set_text(s_battery_layer, s_battery_buf);
}

static void tick_handler(struct tm *tick_time, TimeUnits changed) {
    update_time();
}

static void battery_handler(BatteryChargeState state) {
    s_battery = state.charge_percent;
    update_battery();
    layer_mark_dirty(s_canvas_layer);
}

static void bt_handler(bool connected) {
    if (connected != s_bt_connected) vibes_double_pulse();
    s_bt_connected = connected;
    update_signal();
    layer_mark_dirty(s_canvas_layer);
}

// ─── Window lifecycle ────────────────────────────────────────────────
static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    GRect b = layer_get_bounds(root);

    s_font_big   = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_MARATYPE_50));
    s_font_small = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_MARATYPE_16));

    s_canvas_layer = layer_create(b);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(root, s_canvas_layer);

    // Header
    s_top_layer = text_layer_create(GRect(0, 22, b.size.w, 20));
    text_layer_set_text(s_top_layer, "RUNNER // MONITOR");
    text_layer_set_font(s_top_layer, s_font_small);
    text_layer_set_text_color(s_top_layer, COLOR_ACID);
    text_layer_set_background_color(s_top_layer, GColorClear);
    text_layer_set_text_alignment(s_top_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_top_layer));
        
    // Signal
    s_signal_layer = text_layer_create(GRect(0, 42, b.size.w, 20));
    //text_layer_set_text(s_signal_layer, "LOC: TAU CETI IV");
    text_layer_set_font(s_signal_layer, s_font_small);
    text_layer_set_text_color(s_signal_layer, COLOR_ACID);
    text_layer_set_background_color(s_signal_layer, GColorClear);
    text_layer_set_text_alignment(s_signal_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_signal_layer));
        
    // Weather
    s_weather_layer = text_layer_create(GRect(0, 62, b.size.w, 20));
    text_layer_set_text(s_weather_layer, "WEATHER PLACEHOLDER");
    text_layer_set_font(s_weather_layer, s_font_small);
    text_layer_set_text_color(s_weather_layer, COLOR_ACID);
    text_layer_set_background_color(s_weather_layer, GColorClear);
    text_layer_set_text_alignment(s_weather_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_weather_layer));

    // Time
    s_time_layer = text_layer_create(GRect(0, b.size.h / 2 - 26, b.size.w, 62));
    text_layer_set_font(s_time_layer, s_font_big);
    text_layer_set_text_color(s_time_layer, COLOR_ACID);
    text_layer_set_background_color(s_time_layer, GColorClear);
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_time_layer));

    // Date
    s_date_layer = text_layer_create(GRect(0, b.size.h / 2 + 34, b.size.w, 20));
    text_layer_set_font(s_date_layer, s_font_small);
    text_layer_set_text_color(s_date_layer, COLOR_ACID);
    text_layer_set_background_color(s_date_layer, GColorClear);
    text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_date_layer));

    // Battery
    s_battery_layer = text_layer_create(GRect(0, b.size.h - 38, b.size.w, 18));
    text_layer_set_font(s_battery_layer, s_font_small);
    text_layer_set_background_color(s_battery_layer, GColorClear);
    text_layer_set_text_alignment(s_battery_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_battery_layer));

    update_time();
    update_battery();
    update_signal();
}

static void window_unload(Window *window) {
    text_layer_destroy(s_top_layer);
    text_layer_destroy(s_signal_layer);
    text_layer_destroy(s_weather_layer);
    text_layer_destroy(s_time_layer);
    text_layer_destroy(s_date_layer);
    text_layer_destroy(s_battery_layer);
    layer_destroy(s_canvas_layer);
    fonts_unload_custom_font(s_font_big);
    fonts_unload_custom_font(s_font_small);
}

static void init(void) {
    s_window = window_create();
    window_set_background_color(s_window, COLOR_VOID);
    window_set_window_handlers(s_window, (WindowHandlers){
        .load = window_load,
        .unload = window_unload,
    });
    window_stack_push(s_window, true);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    battery_state_service_subscribe(battery_handler);
    s_battery = battery_state_service_peek().charge_percent;
    connection_service_subscribe((ConnectionHandlers){
        .pebble_app_connection_handler = bt_handler,
    });
    s_bt_connected = connection_service_peek_pebble_app_connection();
}

static void deinit(void) {
    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();
    connection_service_unsubscribe();
    window_destroy(s_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
