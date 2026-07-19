#include <pebble.h>

// --- MARATHON palette -------------------------------------------------------
// Acid yellow-green on void black; magenta for Runner alert states.
#define COLOR_ACID   PBL_IF_COLOR_ELSE(GColorSpringBud, GColorWhite)
#define COLOR_ALERT  PBL_IF_COLOR_ELSE(GColorFolly,   GColorWhite)
#define COLOR_DIM    GColorWhite
#define COLOR_VOID   GColorBlack

#define SETTINGS_KEY 1

typedef struct ClaySettings {
    bool showCity;
    bool showWeather;
    bool useFahrenheit;
    char* topText;
} ClaySettings;

static ClaySettings settings;


static Window    *s_window;
static Layer     *s_canvas_layer;
static TextLayer *s_top_layer;
static TextLayer *s_signal_layer;
static TextLayer *s_weather_layer;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_steps_layer;
static TextLayer *s_battery_layer;
static GFont      s_font_big;
static GFont      s_font_small;

static char s_time_buf[12];
static char s_date_buf[20];
static char s_steps_buf[14];
static char s_battery_buf[24];
static char s_signal_buf[18];

static int  s_battery = 100;
static int  s_steps = 0;
static bool s_signal = true;

// --- Settings ---------------------------------------------------------------
static void prv_default_settings() {
    settings.showCity = true;
    settings.showWeather = true;
    settings.useFahrenheit = true;
    settings.topText = "RUNNER // MONITOR";
}

static void prv_save_settings() {
    persist_write_data(SETTINGS_KEY, &settings, sizeof(settings));
}

static void prv_load_settings() {
    prv_default_settings();
    persist_read_data(SETTINGS_KEY, &settings, sizeof(settings));
}

// --- Rendering --------------------------------------------------------------
static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect b = layer_get_bounds(layer);
    GColor fg = s_signal ? COLOR_ACID : COLOR_ALERT;

    graphics_context_set_stroke_color(ctx, fg);
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

    // Small "signal" triangle glyph, top-center - everything past here has stroke width of 1
    graphics_context_set_stroke_width(ctx, 1);
    GPoint apex = GPoint(b.size.w / 2, m + 2);
    graphics_draw_line(ctx, GPoint(apex.x - 5, apex.y + 8), GPoint(apex.x, apex.y));
    graphics_draw_line(ctx, GPoint(apex.x + 5, apex.y + 8), GPoint(apex.x, apex.y));
    graphics_draw_line(ctx, GPoint(apex.x - 5, apex.y + 8), GPoint(apex.x + 5, apex.y + 8));

    // Rule line above and below the time block - everything past here is always green
    int rule_y1 = b.size.h / 2 - 32;
    int rule_y2 = b.size.h / 2 + 30;
    graphics_context_set_stroke_color(ctx, COLOR_ACID);
    graphics_context_set_fill_color(ctx, COLOR_ACID);
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
}

// --- State updates ----------------------------------------------------------
// Signal update
static void update_signal(void) {
    if (s_signal) {
        snprintf(s_signal_buf, sizeof(s_signal_buf), "TAU CETI IV");
        text_layer_set_text_color(s_top_layer, COLOR_ACID);
        text_layer_set_text_color(s_signal_layer, COLOR_ACID);
        text_layer_set_text_color(s_weather_layer, COLOR_ACID);
        light_set_color_rgb888(0xC2FE0B);
    } else {
        snprintf(s_signal_buf, sizeof(s_signal_buf), "!! SIGNAL LOST !!");
        text_layer_set_text_color(s_top_layer, COLOR_ALERT);
        text_layer_set_text_color(s_signal_layer, COLOR_ALERT);
        text_layer_set_text_color(s_weather_layer, COLOR_ALERT);
        light_set_color_rgb888(0xEA027E);
    }
    text_layer_set_text(s_signal_layer, s_signal_buf);
}

// Weather update
static void update_weather(void) {
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
    app_message_outbox_send();
}

// Time and date update
static void update_time(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    strftime(s_time_buf, sizeof(s_time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", t);
    text_layer_set_text(s_time_layer, s_time_buf);

    strftime(s_date_buf, sizeof(s_date_buf), "%a - %b %d %Y", t);
    text_layer_set_text(s_date_layer, s_date_buf);
}

// Steps update
static void update_steps(void) {
    s_steps = health_service_sum_today(HealthMetricStepCount);
    snprintf(s_steps_buf, sizeof(s_steps_buf), "STEPS - %d", s_steps);
    text_layer_set_text(s_steps_layer, s_steps_buf);
}

// Battery update
static void update_battery(void) {
    snprintf(s_battery_buf, sizeof(s_battery_buf), "SHELL INTEGRITY - %d%%", s_battery);
    text_layer_set_text(s_battery_layer, s_battery_buf);
}

// --- Event handlers ---------------------------------------------------------

static void tick_handler(struct tm *tick_time, TimeUnits changed) {
    update_time();
    update_steps();

    // Get weather update every 30 minutes
    if (tick_time->tm_min % 30 == 0) {
        update_weather();
    }
}

static void battery_handler(BatteryChargeState state) {
    s_battery = state.charge_percent;
    update_battery();
    layer_mark_dirty(s_canvas_layer);
}

static void bt_handler(bool connected) {
    if (connected != s_signal) vibes_double_pulse();
    s_signal = connected;
    update_signal();
    layer_mark_dirty(s_canvas_layer);
}

static void inbox_received_handler(DictionaryIterator *iterator, void *context) {
    Tuple *showCity_tuple = dict_find(iterator, MESSAGE_KEY_showCity);
    Tuple *showWeather_tuple = dict_find(iterator, MESSAGE_KEY_showWeather);
    Tuple *useFahrenheit_tuple = dict_find(iterator, MESSAGE_KEY_useFahrenheit);
    Tuple *topText_tuple = dict_find(iterator, MESSAGE_KEY_topText);
    
    if (showCity_tuple) settings.showCity = showCity_tuple->value->int32 == 1;
    if (showWeather_tuple) settings.showWeather = showWeather_tuple->value->int32 == 1;
    if (useFahrenheit_tuple) settings.useFahrenheit = useFahrenheit_tuple->value->int32 == 1;
    if (topText_tuple) settings.topText = topText_tuple->value->cstring;
    
    prv_save_settings();
    
    if (topText_tuple) text_layer_set_text(s_top_layer, settings.topText);
    
    Tuple *city_tuple = dict_find(iterator, MESSAGE_KEY_CITY);
    Tuple *temp_tuple = dict_find(iterator, MESSAGE_KEY_TEMPERATURE);
    Tuple *conditions_tuple = dict_find(iterator, MESSAGE_KEY_CONDITIONS);

    if (settings.showCity && city_tuple) {
        static char city_buffer[20];
        snprintf(city_buffer, sizeof(city_buffer), "%s", city_tuple->value->cstring);
        text_layer_set_text(s_signal_layer, city_buffer);
    } else {
        text_layer_set_text(s_signal_layer, "TAU CETI IV");
    }

    if (settings.showWeather && temp_tuple && conditions_tuple) {
        static char temperature_buffer[8];
        static char conditions_buffer[32];
        static char weather_layer_buffer[42];
        
        int t = (int)temp_tuple->value->int32;
        if (settings.useFahrenheit) t = t * 1.8 + 32;
        char *tLabel = settings.useFahrenheit ? "F" : "C";

        snprintf(temperature_buffer, sizeof(temperature_buffer), "%d° %s", (int)temp_tuple->value->int32, tLabel);
        snprintf(conditions_buffer, sizeof(conditions_buffer), "%s", conditions_tuple->value->cstring);
        snprintf(weather_layer_buffer, sizeof(weather_layer_buffer), "%s %s", temperature_buffer, conditions_buffer);
        text_layer_set_text(s_weather_layer, weather_layer_buffer);
    } else {
        text_layer_set_text(s_weather_layer, "ATMOSPHERE // NOT FOUND");
    }
    
    if (showCity_tuple || showWeather_tuple || useFahrenheit_tuple) update_weather();
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

static void outbox_failed_handler(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
}

static void outbox_sent_handler(DictionaryIterator *iterator, void *context) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

// --- Window lifecycle -------------------------------------------------------
static TextLayer* setup_text_layer(Layer* root, GRect bounds, const char* text, GFont font) {
    TextLayer *layer = text_layer_create(bounds);
    if (text) text_layer_set_text(layer, text);
    text_layer_set_font(layer, font);
    text_layer_set_text_color(layer, COLOR_ACID);
    text_layer_set_background_color(layer, GColorClear);
    text_layer_set_text_alignment(layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(layer));
    return layer;
}

static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(root);

    s_font_big   = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_MARATYPE_50));
    s_font_small = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_MARATYPE_16));

    s_canvas_layer = layer_create(bounds);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(root, s_canvas_layer);

    s_top_layer = setup_text_layer(root, GRect(0, 22, bounds.size.w, 20), settings.topText, s_font_small);
    s_signal_layer = setup_text_layer(root, GRect(0, 42, bounds.size.w, 20), NULL, s_font_small);
    s_weather_layer = setup_text_layer(root, GRect(0, 62, bounds.size.w, 20), "!! CONDITIONS UNKNOWN !!", s_font_small);
    s_time_layer = setup_text_layer(root, GRect(0, bounds.size.h / 2 - 26, bounds.size.w, 62), NULL, s_font_big);
    s_date_layer = setup_text_layer(root, GRect(0, bounds.size.h - 78, bounds.size.w, 20), NULL, s_font_small);
    s_steps_layer = setup_text_layer(root, GRect(0, bounds.size.h - 58, bounds.size.w, 20), "STEPS - 0", s_font_small);
    s_battery_layer = setup_text_layer(root, GRect(0, bounds.size.h - 38, bounds.size.w, 18), NULL, s_font_small);

    s_battery = battery_state_service_peek().charge_percent;
    s_signal = connection_service_peek_pebble_app_connection();
    light_set_color_rgb888(0xC2FE0B);

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
    text_layer_destroy(s_steps_layer);
    text_layer_destroy(s_battery_layer);
    layer_destroy(s_canvas_layer);
    fonts_unload_custom_font(s_font_big);
    fonts_unload_custom_font(s_font_small);
}

static void init(void) {
    prv_load_settings();
    s_window = window_create();
    window_set_background_color(s_window, COLOR_VOID);
    window_set_window_handlers(s_window, (WindowHandlers){
        .load = window_load,
        .unload = window_unload,
    });
    window_stack_push(s_window, true);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    battery_state_service_subscribe(battery_handler);
    connection_service_subscribe((ConnectionHandlers){.pebble_app_connection_handler = bt_handler});

    app_message_register_inbox_received(inbox_received_handler);
    app_message_register_inbox_dropped(inbox_dropped_handler);
    app_message_register_outbox_failed(outbox_failed_handler);
    app_message_register_outbox_sent(outbox_sent_handler);

    const int inbox_size = 256;
    const int outbox_size = 256;
    app_message_open(inbox_size, outbox_size);
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
