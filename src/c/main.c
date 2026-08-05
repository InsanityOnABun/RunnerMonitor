#include <pebble.h>

// --- Palette ----------------------------------------------------------------
// Quantized from the source palette: acid #C2FE0B -> SpringBud (#AAFF00),
// magenta #EA027E -> Folly (#FF0055). B&W platforms fall back to white.
#define COLOR_ACID  PBL_IF_COLOR_ELSE(GColorSpringBud, GColorWhite)
#define COLOR_ALERT PBL_IF_COLOR_ELSE(GColorFolly,     GColorWhite)
#define COLOR_VOID  GColorBlack

// Backlight LED colors (RGB888), matching the palette above.
#define LIGHT_ACID  0xC2FE0B
#define LIGHT_ALERT 0xEA027E

// --- Settings ---------------------------------------------------------------
// When changing settings struct, increment settings key, blow up previous keys
#define SETTINGS_KEY 1

typedef struct ClaySettings {
    bool showCity;
    bool showWeather;
    bool useFahrenheit;
    char topText[26];       // header text; 25 chars max + NUL
} ClaySettings;

static ClaySettings settings;

// --- UI handles and fonts ---------------------------------------------------
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

// --- State ------------------------------------------------------------------
// TextLayer stores pointers, not copies: these buffers back any layer text
// that is built at runtime, and must outlive the layers.
static char s_signal_buf[28];
static char s_temperature_buf[12];
static char s_conditions_buf[32];
static char s_weather_layer_buf[48];
static char s_time_buf[12];
static char s_date_buf[20];
static char s_steps_buf[16];
static char s_battery_buf[24];

static int  s_battery;
static int  s_steps;

static bool s_signal;
static bool s_charging;
static bool s_plugged;
static bool s_quiet;

// --- Display strings --------------------------------------------------------
static const char TEXT_HEADER_DEFAULT[] = "RUNNER // MONITOR";
static const char TEXT_QUIET[]          = "//--SILENT RUNNING--//";
static const char TEXT_SIGNAL_OK[]      = "TAU CETI IV";
static const char TEXT_SIGNAL_LOST[]    = "!! SIGNAL LOST !!";
static const char TEXT_ATMO_UNKNOWN[]   = "!! ATMO UNKNOWN !!";
static const char TEXT_BATTERY_MAX[]    = "SHELL INTEGRITY MAX";
static const char TEXT_STEPS_NA[]       = "STEPS - N/A";
static const char TEXT_UNIT_F[]         = "F";
static const char TEXT_UNIT_C[]         = "C";

static const char FMT_STEPS[]          = "STEPS - %d";
static const char FMT_BATTERY[]        = "SHELL INTEGRITY - %d%%";
static const char FMT_BATTERY_REPAIR[] = "SHELL REPAIRING - %d%%";
static const char FMT_TEMPERATURE[]    = "%d° %s";
static const char FMT_WEATHER[]        = "%s - %s";
static const char FMT_TIME_24H[]       = "%H:%M";
static const char FMT_TIME_12H[]       = "%I:%M %p";
static const char FMT_DATE[]           = "%a - %b %d %Y";

// --- Settings persistence ---------------------------------------------------
static void prv_default_settings(void) {
    settings.showCity = true;
    settings.showWeather = true;
    settings.useFahrenheit = true;
    snprintf(settings.topText, sizeof(settings.topText), "%s", TEXT_HEADER_DEFAULT);
}

static void prv_save_settings(void) {
    persist_write_data(SETTINGS_KEY, &settings, sizeof(settings));
}

static void prv_load_settings(void) {
    prv_default_settings();
    persist_read_data(SETTINGS_KEY, &settings, sizeof(settings));
}

// --- Canvas rendering -------------------------------------------------------
static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect b = layer_get_bounds(layer);
    GColor fg = s_signal ? COLOR_ACID : COLOR_ALERT;

    graphics_context_set_stroke_color(ctx, fg);
    graphics_context_set_fill_color(ctx, fg);
    graphics_context_set_stroke_width(ctx, 2);

    // Corner brackets - HUD reticle framing
    const int m = 6;
    
    #if defined(PBL_ROUND)
        GRect arc_rect = grect_inset(b, GEdgeInsets(m));
        const int32_t arc_half_span = DEG_TO_TRIGANGLE(8); // ~40 deg wide each
        const int32_t bracket_thickness = 3;
        static const int32_t bracket_centers[4] = {
            DEG_TO_TRIGANGLE(55),
            DEG_TO_TRIGANGLE(125),
            DEG_TO_TRIGANGLE(235),
            DEG_TO_TRIGANGLE(305),
        };
        for (int i = 0; i < 4; i++) {
            graphics_fill_radial(ctx, arc_rect, GOvalScaleModeFitCircle,
                                 bracket_thickness,
                                 bracket_centers[i] - arc_half_span,
                                 bracket_centers[i] + arc_half_span);
        }
    #else
        // Emery (rect): straight L-shaped brackets pinned to the four corners.
        const int len = 14;
        graphics_draw_line(ctx, GPoint(m, m), GPoint(m + len, m));
        graphics_draw_line(ctx, GPoint(m, m), GPoint(m, m + len));
        graphics_draw_line(ctx, GPoint(b.size.w - m, m), GPoint(b.size.w - m - len, m));
        graphics_draw_line(ctx, GPoint(b.size.w - m, m), GPoint(b.size.w - m, m + len));
        graphics_draw_line(ctx, GPoint(m, b.size.h - m), GPoint(m + len, b.size.h - m));
        graphics_draw_line(ctx, GPoint(m, b.size.h - m), GPoint(m, b.size.h - m - len));
        graphics_draw_line(ctx, GPoint(b.size.w - m, b.size.h - m), GPoint(b.size.w - m - len, b.size.h - m));
        graphics_draw_line(ctx, GPoint(b.size.w - m, b.size.h - m), GPoint(b.size.w - m, b.size.h - m - len));
    #endif

    // Signal triangle glyph, top-center
    graphics_context_set_stroke_width(ctx, 1);
    GPoint apex = GPoint(b.size.w / 2, m + 2);
    graphics_draw_line(ctx, GPoint(apex.x - 5, apex.y + 8), GPoint(apex.x, apex.y));
    graphics_draw_line(ctx, GPoint(apex.x + 5, apex.y + 8), GPoint(apex.x, apex.y));
    graphics_draw_line(ctx, GPoint(apex.x - 5, apex.y + 8), GPoint(apex.x + 5, apex.y + 8));

    // Everything below the top rule stays green regardless of signal state
    graphics_context_set_stroke_color(ctx, COLOR_ACID);
    graphics_context_set_fill_color(ctx, COLOR_ACID);

    // Rules above and below the time block
    int rule_y1 = b.size.h / 2 - 32;
    int rule_y2 = b.size.h / 2 + 30;
    graphics_draw_line(ctx, GPoint(m + 8, rule_y1), GPoint(b.size.w - m - 8, rule_y1));
    graphics_draw_line(ctx, GPoint(m + 8, rule_y2), GPoint(b.size.w - m - 8, rule_y2));

    // Battery: 10 segmented blocks, one per 10% (partial segments round up)
    int seg_w = 8, seg_h = 5, gap = 3;
    int total = 10 * seg_w + 9 * gap;
    int bx = (b.size.w - total) / 2;
    int by = b.size.h - m - seg_h - PBL_IF_ROUND_ELSE(16, 8);
    int lit = (s_battery + 9) / 10;
    for (int i = 0; i < 10; i++) {
        GRect seg = GRect(bx + i * (seg_w + gap), by, seg_w, seg_h);
        if (i < lit) graphics_fill_rect(ctx, seg, 0, GCornerNone);
        else graphics_draw_rect(ctx, seg);
    }
}

// --- State updates ----------------------------------------------------------
static void update_top_header(void) {
    text_layer_set_text(s_top_layer, s_quiet ? TEXT_QUIET : settings.topText);
}

static void update_signal(void) {
    if (s_signal) {
        text_layer_set_text(s_signal_layer, TEXT_SIGNAL_OK);
        text_layer_set_text_color(s_top_layer, COLOR_ACID);
        text_layer_set_text_color(s_signal_layer, COLOR_ACID);
        text_layer_set_text_color(s_weather_layer, COLOR_ACID);
        light_set_color_rgb888(LIGHT_ACID);
    } else {
        text_layer_set_text(s_signal_layer, TEXT_SIGNAL_LOST);
        text_layer_set_text_color(s_top_layer, COLOR_ALERT);
        text_layer_set_text_color(s_signal_layer, COLOR_ALERT);
        text_layer_set_text_color(s_weather_layer, COLOR_ALERT);
        light_set_color_rgb888(LIGHT_ALERT);
    }
    // Set weather layer to unknown until bt_handler's weather update grabs it
    text_layer_set_text(s_weather_layer, TEXT_ATMO_UNKNOWN);
}

// Requests a weather fetch from the phone. A failed begin (e.g. outbox busy)
// just drops the request; the next 30-minute tick self-heals.
static void update_weather(void) {
    DictionaryIterator *iter;
    AppMessageResult result = app_message_outbox_begin(&iter);
    if (result == APP_MSG_OK) {
        dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
        app_message_outbox_send();
    } else {
        APP_LOG(APP_LOG_LEVEL_WARNING, "Outbox unavailable: %d", (int) result);
    }
}

static void update_time(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    strftime(s_time_buf, sizeof(s_time_buf), clock_is_24h_style() ? FMT_TIME_24H : FMT_TIME_12H, t);
    text_layer_set_text(s_time_layer, s_time_buf);

    strftime(s_date_buf, sizeof(s_date_buf), FMT_DATE, t);
    text_layer_set_text(s_date_layer, s_date_buf);
}

static void update_steps(void) {
    HealthServiceAccessibilityMask mask =
        health_service_metric_accessible(HealthMetricStepCount, time_start_of_today(), time(NULL));

    if (mask & HealthServiceAccessibilityMaskAvailable) {
        s_steps = (int) health_service_sum_today(HealthMetricStepCount);
        snprintf(s_steps_buf, sizeof(s_steps_buf), FMT_STEPS, s_steps);
        text_layer_set_text(s_steps_layer, s_steps_buf);
    } else {
        text_layer_set_text(s_steps_layer, TEXT_STEPS_NA);
    }
}

static void update_battery(void) {
    // is_plugged stays true while charging, so test s_charging first
    if (s_charging) {
        snprintf(s_battery_buf, sizeof(s_battery_buf), FMT_BATTERY_REPAIR, s_battery);
        text_layer_set_text(s_battery_layer, s_battery_buf);
    } else if (s_plugged) {
        text_layer_set_text(s_battery_layer, TEXT_BATTERY_MAX);
    } else {
        snprintf(s_battery_buf, sizeof(s_battery_buf), FMT_BATTERY, s_battery);
        text_layer_set_text(s_battery_layer, s_battery_buf);
    }
}

// --- Event handlers ---------------------------------------------------------
static void tick_handler(struct tm *tick_time, TimeUnits changed) {
    update_time();
    update_steps();

    // Quiet Time has no event service; poll it once a minute
    if (quiet_time_is_active() != s_quiet) {
        s_quiet = !s_quiet;
        update_top_header();
    }

    if (tick_time->tm_min % 30 == 0) update_weather();
}

static void battery_handler(BatteryChargeState state) {
    s_battery  = state.charge_percent;
    s_charging = state.is_charging;
    s_plugged  = state.is_plugged;
    update_battery();
    layer_mark_dirty(s_canvas_layer);   // battery bar
}

static void bt_handler(bool connected) {
    bool prev = s_signal;
    // Live quiet check, not s_quiet: the cached flag can be a minute stale
    if (connected != prev && !quiet_time_is_active()) vibes_double_pulse();
    s_signal = connected;
    update_signal();
    if (connected && !prev) update_weather();
    layer_mark_dirty(s_canvas_layer);   // brackets + glyph color
}

static void inbox_received_handler(DictionaryIterator *iterator, void *context) {
    // Settings tuples (present only when the Clay config page was submitted)
    Tuple *showCity_tuple      = dict_find(iterator, MESSAGE_KEY_showCity);
    Tuple *showWeather_tuple   = dict_find(iterator, MESSAGE_KEY_showWeather);
    Tuple *useFahrenheit_tuple = dict_find(iterator, MESSAGE_KEY_useFahrenheit);
    Tuple *topText_tuple       = dict_find(iterator, MESSAGE_KEY_topText);

    if (showCity_tuple)      settings.showCity      = showCity_tuple->value->int32 == 1;
    if (showWeather_tuple)   settings.showWeather   = showWeather_tuple->value->int32 == 1;
    if (useFahrenheit_tuple) settings.useFahrenheit = useFahrenheit_tuple->value->int32 == 1;
    if (topText_tuple) {
        if (topText_tuple->value->cstring[0]) {
            snprintf(settings.topText, sizeof(settings.topText), "%s", topText_tuple->value->cstring);
        } else {
            snprintf(settings.topText, sizeof(settings.topText), "%s", TEXT_HEADER_DEFAULT);
        }
        update_top_header();
    }

    // Only touch flash when a settings tuple actually arrived
    if (showCity_tuple || showWeather_tuple || useFahrenheit_tuple || topText_tuple) {
        prv_save_settings();
    }

    // Weather tuples (present only in fetch responses)
    Tuple *city_tuple       = dict_find(iterator, MESSAGE_KEY_CITY);
    Tuple *temp_tuple       = dict_find(iterator, MESSAGE_KEY_TEMPERATURE);
    Tuple *conditions_tuple = dict_find(iterator, MESSAGE_KEY_CONDITIONS);

    if (settings.showCity && city_tuple) {
        snprintf(s_signal_buf, sizeof(s_signal_buf), "%s", city_tuple->value->cstring);
        text_layer_set_text(s_signal_layer, s_signal_buf);
    } else {
        text_layer_set_text(s_signal_layer, TEXT_SIGNAL_OK);
    }

    if (settings.showWeather && temp_tuple && conditions_tuple) {
        int t = settings.useFahrenheit
            ? (int) temp_tuple->value->int32 * 9 / 5 + 32
            : (int) temp_tuple->value->int32;
        const char *tLabel = settings.useFahrenheit ? TEXT_UNIT_F : TEXT_UNIT_C;

        snprintf(s_temperature_buf, sizeof(s_temperature_buf), FMT_TEMPERATURE, t, tLabel);
        snprintf(s_conditions_buf, sizeof(s_conditions_buf), "%s", conditions_tuple->value->cstring);
        snprintf(s_weather_layer_buf, sizeof(s_weather_layer_buf), FMT_WEATHER,
                 s_temperature_buf, s_conditions_buf);
        text_layer_set_text(s_weather_layer, s_weather_layer_buf);
    } else {
        text_layer_set_text(s_weather_layer, TEXT_ATMO_UNKNOWN);
    }

    // Settings changes may alter what to display; fetch fresh data
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
static TextLayer *setup_text_layer(Layer *root, GRect bounds, const char *text, GFont font) {
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

    s_top_layer     = setup_text_layer(root, GRect(0, PBL_IF_ROUND_ELSE(30, 22), bounds.size.w, 20), settings.topText, s_font_small);
    s_signal_layer  = setup_text_layer(root, GRect(0, PBL_IF_ROUND_ELSE(50, 42), bounds.size.w, 20), NULL, s_font_small);
    s_weather_layer = setup_text_layer(root, GRect(0, PBL_IF_ROUND_ELSE(70, 62), bounds.size.w, 20), NULL, s_font_small);
    s_time_layer    = setup_text_layer(root, GRect(0, bounds.size.h / 2 - 26, bounds.size.w, 62), NULL, s_font_big);
    s_date_layer    = setup_text_layer(root, GRect(0, bounds.size.h - PBL_IF_ROUND_ELSE(86, 78), bounds.size.w, 20), NULL, s_font_small);
    s_steps_layer   = setup_text_layer(root, GRect(0, bounds.size.h - PBL_IF_ROUND_ELSE(66, 58), bounds.size.w, 20), NULL, s_font_small);
    s_battery_layer = setup_text_layer(root, GRect(0, bounds.size.h - PBL_IF_ROUND_ELSE(46, 38), bounds.size.w, 18), NULL, s_font_small);

    BatteryChargeState charge_state = battery_state_service_peek();
    s_battery  = charge_state.charge_percent;
    s_charging = charge_state.is_charging;
    s_plugged  = charge_state.is_plugged;

    s_signal = connection_service_peek_pebble_app_connection();
    s_quiet  = quiet_time_is_active();

    update_time();
    update_steps();
    update_battery();
    update_signal();
    update_top_header();
    update_weather();
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
    window_set_window_handlers(s_window, (WindowHandlers) {
        .load = window_load,
        .unload = window_unload,
    });
    window_stack_push(s_window, true);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    battery_state_service_subscribe(battery_handler);
    connection_service_subscribe((ConnectionHandlers) {
        .pebble_app_connection_handler = bt_handler,
    });

    app_message_register_inbox_received(inbox_received_handler);
    app_message_register_inbox_dropped(inbox_dropped_handler);
    app_message_register_outbox_failed(outbox_failed_handler);
    app_message_register_outbox_sent(outbox_sent_handler);
    app_message_open(256, 256);
}

static void deinit(void) {
    // Stop event sources before destroying the UI they write into
    app_message_deregister_callbacks();
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
