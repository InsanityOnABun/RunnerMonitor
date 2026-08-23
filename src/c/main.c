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
// Legacy keys are kept only to detect and migrate old persisted blobs.
#define SETTINGS_KEY_LEGACY_1         1
#define SETTINGS_KEY_LEGACY_2         2

#define SETTINGS_KEY_VERSION          100
#define SETTINGS_KEY_SHOW_CITY        101
#define SETTINGS_KEY_SHOW_WEATHER     102
#define SETTINGS_KEY_USE_FAHRENHEIT   103
#define SETTINGS_KEY_WEATHER_INTERVAL 104
#define SETTINGS_KEY_TOP_TEXT         105
#define SETTINGS_KEY_CHANGE_BACKLIGHT 106

#define SETTINGS_VERSION 1

typedef struct ClaySettings {
    char topText[26];
    bool showCity;
    bool showWeather;
    bool useFahrenheit;
    int  weatherInterval;
    bool changeBacklight;
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

static int  s_steps;
static bool s_signal;
static bool s_quiet;

static BatteryChargeState s_battery_state;

// --- Display strings --------------------------------------------------------
static const char TEXT_HEADER_DEFAULT[]   = "RUNNER // MONITOR";
static const char TEXT_QUIET[]            = "//--SILENT RUNNING--//";
static const char TEXT_SIGNAL_OK[]        = "TAU CETI IV";
static const char TEXT_SIGNAL_LOST[]      = "!! SIGNAL LOST !!";
static const char TEXT_ATMO_UNKNOWN[]     = "!! ATMO UNKNOWN !!";
static const char TEXT_BATTERY_MAX[]      = "SHELL INTEGRITY MAX";
static const char TEXT_STEPS_NA[]         = "STEPS - N/A";
static const char TEXT_UNIT_F[]           = "F";
static const char TEXT_UNIT_C[]           = "C";
// Transient status strings shown while a message round-trip is in progress
// or has failed.
static const char TEXT_WEATHER_FETCHING[] = ">> FETCHING ATMO <<";
static const char TEXT_MSG_DROPPED[]      = "!! MSG DROPPED !!";
static const char TEXT_SEND_FAILED[]      = "!! SEND FAILED !!";

// --- Format strings ---------------------------------------------------------
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
    snprintf(settings.topText, sizeof(settings.topText), "%s", TEXT_HEADER_DEFAULT);
    settings.showCity        = true;
    settings.showWeather     = true;
    settings.useFahrenheit   = true;
    settings.weatherInterval = 30;
    settings.changeBacklight = true;
}

static void prv_save_settings(void) {
    persist_write_int(SETTINGS_KEY_VERSION,          SETTINGS_VERSION);
    persist_write_string(SETTINGS_KEY_TOP_TEXT,      settings.topText);
    persist_write_bool(SETTINGS_KEY_SHOW_CITY,       settings.showCity);
    persist_write_bool(SETTINGS_KEY_SHOW_WEATHER,    settings.showWeather);
    persist_write_bool(SETTINGS_KEY_USE_FAHRENHEIT,  settings.useFahrenheit);
    persist_write_int(SETTINGS_KEY_WEATHER_INTERVAL, settings.weatherInterval);
    persist_write_bool(SETTINGS_KEY_CHANGE_BACKLIGHT, settings.changeBacklight);
}

static void prv_migrate_legacy_settings(void) {
    bool migrated = true;

    if (persist_exists(SETTINGS_KEY_LEGACY_2)) {
        APP_LOG(APP_LOG_LEVEL_INFO,
                "Migrating legacy blob (key %d) to per-field keys",
                SETTINGS_KEY_LEGACY_2);

        typedef struct ClaySettingsLegacy2 {
            bool showCity;
            bool showWeather;
            bool useFahrenheit;
            int  weatherInterval;
            char topText[26];
        } ClaySettingsLegacy2;

        ClaySettingsLegacy2 old;
        persist_read_data(SETTINGS_KEY_LEGACY_2, &old, sizeof(old));

        persist_write_bool(SETTINGS_KEY_SHOW_CITY,       old.showCity);
        persist_write_bool(SETTINGS_KEY_SHOW_WEATHER,    old.showWeather);
        persist_write_bool(SETTINGS_KEY_USE_FAHRENHEIT,  old.useFahrenheit);
        persist_write_int(SETTINGS_KEY_WEATHER_INTERVAL, old.weatherInterval);
        persist_write_string(SETTINGS_KEY_TOP_TEXT,      old.topText);

        persist_delete(SETTINGS_KEY_LEGACY_2);
        persist_delete(SETTINGS_KEY_LEGACY_1);

    } else if (persist_exists(SETTINGS_KEY_LEGACY_1)) {
        APP_LOG(APP_LOG_LEVEL_INFO,
                "Migrating legacy blob (key %d) to per-field keys",
                SETTINGS_KEY_LEGACY_1);

        typedef struct ClaySettingsLegacy1 {
            bool showCity;
            bool showWeather;
            bool useFahrenheit;
            char topText[26];
        } ClaySettingsLegacy1;

        ClaySettingsLegacy1 old;
        persist_read_data(SETTINGS_KEY_LEGACY_1, &old, sizeof(old));

        persist_write_bool(SETTINGS_KEY_SHOW_CITY,      old.showCity);
        persist_write_bool(SETTINGS_KEY_SHOW_WEATHER,   old.showWeather);
        persist_write_bool(SETTINGS_KEY_USE_FAHRENHEIT, old.useFahrenheit);
        persist_write_string(SETTINGS_KEY_TOP_TEXT,     old.topText);

        persist_delete(SETTINGS_KEY_LEGACY_2);
        persist_delete(SETTINGS_KEY_LEGACY_1);

    } else if (persist_read_int(SETTINGS_KEY_VERSION) != SETTINGS_VERSION) {
        APP_LOG(APP_LOG_LEVEL_INFO,
                "Migrating from settings version %d to latest version",
                persist_read_int(SETTINGS_KEY_VERSION));
        // Nothing here yet — still on version 1.

    } else {
        APP_LOG(APP_LOG_LEVEL_INFO, "Old and current settings versions match");
        migrated = false;
    }

    if (migrated) {
        persist_write_int(SETTINGS_KEY_VERSION, SETTINGS_VERSION);
    }
}

static void prv_load_settings(void) {
    prv_default_settings();
    prv_migrate_legacy_settings();

    if (persist_exists(SETTINGS_KEY_TOP_TEXT)) {
        persist_read_string(SETTINGS_KEY_TOP_TEXT,
                            settings.topText, sizeof(settings.topText));
    }
    if (persist_exists(SETTINGS_KEY_SHOW_CITY)) {
        settings.showCity = persist_read_bool(SETTINGS_KEY_SHOW_CITY);
    }
    if (persist_exists(SETTINGS_KEY_SHOW_WEATHER)) {
        settings.showWeather = persist_read_bool(SETTINGS_KEY_SHOW_WEATHER);
    }
    if (persist_exists(SETTINGS_KEY_USE_FAHRENHEIT)) {
        settings.useFahrenheit = persist_read_bool(SETTINGS_KEY_USE_FAHRENHEIT);
    }
    if (persist_exists(SETTINGS_KEY_WEATHER_INTERVAL)) {
        settings.weatherInterval = persist_read_int(SETTINGS_KEY_WEATHER_INTERVAL);
    }
    if (persist_exists(SETTINGS_KEY_CHANGE_BACKLIGHT)) {
        settings.changeBacklight = persist_read_bool(SETTINGS_KEY_CHANGE_BACKLIGHT);
    }
}

// --- Canvas rendering -------------------------------------------------------
static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect  b  = layer_get_bounds(layer);
    GColor fg = s_signal ? COLOR_ACID : COLOR_ALERT;

    graphics_context_set_stroke_color(ctx, fg);
    graphics_context_set_fill_color(ctx, fg);
    graphics_context_set_stroke_width(ctx, 2);

    // Margin offset
    const int m = 6;

    // HUD corner brackets — arcs on round displays, L-shaped lines on rect.
    #if defined(PBL_ROUND)
    GRect arc_rect = grect_inset(b, GEdgeInsets(m));
    const int32_t arc_half_span      = DEG_TO_TRIGANGLE(8);
    const int32_t bracket_thickness  = 3;
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
    #else // PBL_RECT
    const int len = 14;
    int r = b.size.w - m; // right edge
    int bot = b.size.h - m; // bottom edge

    // Top-left
    graphics_draw_line(ctx, GPoint(m, m),         GPoint(m + len, m));
    graphics_draw_line(ctx, GPoint(m, m),          GPoint(m, m + len));
    // Top-right
    graphics_draw_line(ctx, GPoint(r, m),          GPoint(r - len, m));
    graphics_draw_line(ctx, GPoint(r, m),          GPoint(r, m + len));
    // Bottom-left
    graphics_draw_line(ctx, GPoint(m, bot),        GPoint(m + len, bot));
    graphics_draw_line(ctx, GPoint(m, bot),        GPoint(m, bot - len));
    // Bottom-right
    graphics_draw_line(ctx, GPoint(r, bot),        GPoint(r - len, bot));
    graphics_draw_line(ctx, GPoint(r, bot),        GPoint(r, bot - len));
    #endif

    // Signal triangle glyph, top-center
    graphics_context_set_stroke_width(ctx, 1);
    GPoint apex = GPoint(b.size.w / 2, m + 2);
    graphics_draw_line(ctx, GPoint(apex.x - 5, apex.y + 8), GPoint(apex.x, apex.y));
    graphics_draw_line(ctx, GPoint(apex.x + 5, apex.y + 8), GPoint(apex.x, apex.y));
    graphics_draw_line(ctx, GPoint(apex.x - 5, apex.y + 8), GPoint(apex.x + 5, apex.y + 8));

    // Everything below the top rule stays green regardless of signal state.
    graphics_context_set_stroke_color(ctx, COLOR_ACID);
    graphics_context_set_fill_color(ctx, COLOR_ACID);

    // Rules above and below the time block
    int rule_y1 = b.size.h / 2 - 32;
    int rule_y2 = b.size.h / 2 + 30;
    graphics_draw_line(ctx, GPoint(m + 8, rule_y1), GPoint(b.size.w - m - 8, rule_y1));
    graphics_draw_line(ctx, GPoint(m + 8, rule_y2), GPoint(b.size.w - m - 8, rule_y2));

    // Battery indicator — a row of 10 filled/outlined segments
    int seg_w  = 8, seg_h = 5, gap = 3;
    int total  = 10 * seg_w + 9 * gap;
    int bx     = (b.size.w - total) / 2;
    int by     = b.size.h - m - seg_h - PBL_IF_ROUND_ELSE(16, 8);
    int lit    = (s_battery_state.charge_percent + 9) / 10;
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
static void update_top_header(void) {
    text_layer_set_text(s_top_layer, s_quiet ? TEXT_QUIET : settings.topText);
}

static void update_backlight(void) {
    #if defined(PBL_RGB_BACKLIGHT)
    if (settings.changeBacklight) {
        light_set_color_rgb888(s_signal ? LIGHT_ACID : LIGHT_ALERT);
    } else {
        light_set_system_color();
    }
    #endif
}

static void update_signal(void) {
    GColor color;
    if (s_signal) {
        color = COLOR_ACID;
        text_layer_set_text(s_signal_layer, TEXT_SIGNAL_OK);
    } else {
        color = COLOR_ALERT;
        text_layer_set_text(s_signal_layer,  TEXT_SIGNAL_LOST);
        text_layer_set_text(s_weather_layer, TEXT_ATMO_UNKNOWN);
    }
    text_layer_set_text_color(s_top_layer,     color);
    text_layer_set_text_color(s_signal_layer,  color);
    text_layer_set_text_color(s_weather_layer, color);

    update_backlight();
}

// Requests a weather fetch from the phone and updates the signal and weather
// lines to communicate whether the request was sent successfully.
static void update_weather(void) {
    if (s_signal && (settings.showCity || settings.showWeather)) {
        DictionaryIterator *iter;
        AppMessageResult result = app_message_outbox_begin(&iter);
        if (result == APP_MSG_OK) {
            dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
            app_message_outbox_send();
            text_layer_set_text(s_weather_layer, TEXT_WEATHER_FETCHING);
        } else {
            APP_LOG(APP_LOG_LEVEL_WARNING, "Outbox unavailable: %d", (int)result);
            snprintf(s_weather_layer_buf, sizeof(s_weather_layer_buf),
                     "!! OUTBOX ERR %d !!", (int)result);
            text_layer_set_text(s_weather_layer, s_weather_layer_buf);
        }
    }
}

static void update_time(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    strftime(s_time_buf, sizeof(s_time_buf),
             clock_is_24h_style() ? FMT_TIME_24H : FMT_TIME_12H, t);
    
    text_layer_set_text(s_time_layer, s_time_buf);

    strftime(s_date_buf, sizeof(s_date_buf), FMT_DATE, t);
    text_layer_set_text(s_date_layer, s_date_buf);
}

static void update_steps(void) {
    HealthServiceAccessibilityMask mask = health_service_metric_accessible(
        HealthMetricStepCount, time_start_of_today(), time(NULL));

    if (mask & HealthServiceAccessibilityMaskAvailable) {
        s_steps = (int)health_service_sum_today(HealthMetricStepCount);
        snprintf(s_steps_buf, sizeof(s_steps_buf), FMT_STEPS, s_steps);
        text_layer_set_text(s_steps_layer, s_steps_buf);
    } else {
        text_layer_set_text(s_steps_layer, TEXT_STEPS_NA);
    }
}

static void update_battery(void) {
    // is_plugged stays true while charging, so test is_charging first
    if (s_battery_state.is_charging) {
        snprintf(s_battery_buf, sizeof(s_battery_buf),
                 FMT_BATTERY_REPAIR, s_battery_state.charge_percent);
        
        text_layer_set_text(s_battery_layer, s_battery_buf);
    } else if (s_battery_state.is_plugged) {
        text_layer_set_text(s_battery_layer, TEXT_BATTERY_MAX);
    } else {
        snprintf(s_battery_buf, sizeof(s_battery_buf),
                 FMT_BATTERY, s_battery_state.charge_percent);
        
        text_layer_set_text(s_battery_layer, s_battery_buf);
    }
    // Redraw canvas so the battery bar reflects the new charge level
    layer_mark_dirty(s_canvas_layer);
}

// --- Event handlers ---------------------------------------------------------
static void weather_refresh_timer_handler(void *data) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Firing delayed weather call");
    update_weather();
}

static void tick_handler(struct tm *tick_time, TimeUnits changed) {
    update_time();
    update_steps();

    // Quiet Time has no event service; poll it once a minute
    if (quiet_time_is_active() != s_quiet) {
        s_quiet = !s_quiet;
        update_top_header();
    }

    if ((tick_time->tm_min + 1) % settings.weatherInterval == 0) {
        int delay = rand() % 120000;
        APP_LOG(APP_LOG_LEVEL_INFO, "Delaying weather call for %d ms", delay);
        app_timer_register(delay, weather_refresh_timer_handler, NULL);
    }
}

static void battery_handler(BatteryChargeState state) {
    s_battery_state = state;
    update_battery();
}

static void bt_handler(bool connected) {
    bool prev = s_signal;
    
    // Live quiet check, not s_quiet: the cached flag can be a minute stale
    if (connected != prev && !quiet_time_is_active()) {
        vibes_double_pulse();
    }
    
    s_signal = connected;
    update_signal();
    
    if (connected && !prev) {
        update_weather();
    }
    
    // Redraw canvas to update color
    layer_mark_dirty(s_canvas_layer);
}

static void inbox_received_handler(DictionaryIterator *iterator, void *context) {
    // Settings tuples (present only when the Clay config page was submitted)
    Tuple *topText_tuple         = dict_find(iterator, MESSAGE_KEY_topText);
    Tuple *showCity_tuple        = dict_find(iterator, MESSAGE_KEY_showCity);
    Tuple *showWeather_tuple     = dict_find(iterator, MESSAGE_KEY_showWeather);
    Tuple *useFahrenheit_tuple   = dict_find(iterator, MESSAGE_KEY_useFahrenheit);
    Tuple *weatherInterval_tuple = dict_find(iterator, MESSAGE_KEY_weatherInterval);
    Tuple *changeBacklight_tuple = dict_find(iterator, MESSAGE_KEY_changeBacklight);

    if (topText_tuple) {
        if (topText_tuple->value->cstring[0]) {
            snprintf(settings.topText, sizeof(settings.topText), "%s", topText_tuple->value->cstring);
        } else {
            snprintf(settings.topText, sizeof(settings.topText), "%s", TEXT_HEADER_DEFAULT);
        }
        update_top_header();
    }

    if (showCity_tuple)        settings.showCity        = showCity_tuple->value->int32 == 1;
    if (showWeather_tuple)     settings.showWeather     = showWeather_tuple->value->int32 == 1;
    if (useFahrenheit_tuple)   settings.useFahrenheit   = useFahrenheit_tuple->value->int32 == 1;
    if (weatherInterval_tuple) settings.weatherInterval = weatherInterval_tuple->value->int32;
    
    if (changeBacklight_tuple) {
        settings.changeBacklight = changeBacklight_tuple->value->int32 == 1;
        update_backlight();
    }

    // Only touch flash when a settings tuple actually arrived
    if (topText_tuple || showCity_tuple || showWeather_tuple || useFahrenheit_tuple || 
        weatherInterval_tuple || changeBacklight_tuple) {
        prv_save_settings();
    }

    // Weather/status tuples (present only in weather fetch responses)
    Tuple *city_tuple       = dict_find(iterator, MESSAGE_KEY_CITY);
    Tuple *temp_tuple       = dict_find(iterator, MESSAGE_KEY_TEMPERATURE);
    Tuple *conditions_tuple = dict_find(iterator, MESSAGE_KEY_CONDITIONS);

    // Signal / city layer
    if (settings.showCity && city_tuple) {
        snprintf(s_signal_buf, sizeof(s_signal_buf), "%s", city_tuple->value->cstring);
        text_layer_set_text(s_signal_layer, s_signal_buf);
    } else {
        text_layer_set_text(s_signal_layer, TEXT_SIGNAL_OK);
    }

    // Weather layer — three cases:
    //  1. Full data (temp + conditions): render the normal weather line.
    //  2. Conditions only (no temp): the JS sent a status/error string.
    //  3. Neither: fall back to the generic unknown string.
    if (settings.showWeather && temp_tuple && conditions_tuple) {
        int t = settings.useFahrenheit
            ? (int)temp_tuple->value->int32 * 9 / 5 + 32
            : (int)temp_tuple->value->int32;
        const char *tLabel = settings.useFahrenheit ? TEXT_UNIT_F : TEXT_UNIT_C;

        snprintf(s_temperature_buf, sizeof(s_temperature_buf), FMT_TEMPERATURE, t, tLabel);
        snprintf(s_conditions_buf, sizeof(s_conditions_buf), "%s", conditions_tuple->value->cstring);
        snprintf(s_weather_layer_buf, sizeof(s_weather_layer_buf), FMT_WEATHER, s_temperature_buf, s_conditions_buf);
        text_layer_set_text(s_weather_layer, s_weather_layer_buf);

        // Conditions-only message = JS status/error string
    } else if (settings.showWeather && conditions_tuple && !temp_tuple) {
        snprintf(s_weather_layer_buf, sizeof(s_weather_layer_buf), "%s",
                 conditions_tuple->value->cstring);
        text_layer_set_text(s_weather_layer, s_weather_layer_buf);

    } else {
        text_layer_set_text(s_weather_layer, TEXT_ATMO_UNKNOWN);
    }

    // Settings changes may alter what to display; fetch fresh weather data
    if (showCity_tuple || showWeather_tuple || useFahrenheit_tuple) update_weather();
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped! Reason: %d", (int)reason);
    snprintf(s_weather_layer_buf, sizeof(s_weather_layer_buf),
             "!! DROP ERR %d !!", (int)reason);
    text_layer_set_text(s_weather_layer, s_weather_layer_buf);
    text_layer_set_text(s_signal_layer, TEXT_MSG_DROPPED);
}

static void outbox_failed_handler(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed! Reason: %d", (int)reason);
    snprintf(s_weather_layer_buf, sizeof(s_weather_layer_buf),
             "!! SEND ERR %d !!", (int)reason);
    text_layer_set_text(s_weather_layer, s_weather_layer_buf);
    text_layer_set_text(s_signal_layer, TEXT_SEND_FAILED);
}

static void outbox_sent_handler(DictionaryIterator *iterator, void *context) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

// --- Window lifecycle -------------------------------------------------------
static TextLayer *setup_text_layer(Layer *root, GRect bounds,
                                   const char *text, GFont font) {
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
    Layer *root   = window_get_root_layer(window);
    GRect  bounds = layer_get_bounds(root);
    int    w      = bounds.size.w;
    int    h      = bounds.size.h;
    
    s_font_big   = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_MARATYPE_50));
    s_font_small = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_MARATYPE_16));

    s_canvas_layer = layer_create(bounds);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(root, s_canvas_layer);

    s_top_layer     = setup_text_layer(root, GRect(0, PBL_IF_ROUND_ELSE(30, 22), w, 20), settings.topText, s_font_small);
    s_signal_layer  = setup_text_layer(root, GRect(0, PBL_IF_ROUND_ELSE(50, 42), w, 20), NULL,             s_font_small);
    s_weather_layer = setup_text_layer(root, GRect(0, PBL_IF_ROUND_ELSE(70, 62), w, 20), NULL,             s_font_small);
    s_time_layer    = setup_text_layer(root, GRect(0, h / 2 - 26,                w, 62), NULL,             s_font_big);
    s_date_layer    = setup_text_layer(root, GRect(0, h - PBL_IF_ROUND_ELSE(86, 78), w, 20), NULL,         s_font_small);
    s_steps_layer   = setup_text_layer(root, GRect(0, h - PBL_IF_ROUND_ELSE(66, 58), w, 20), NULL,         s_font_small);
    s_battery_layer = setup_text_layer(root, GRect(0, h - PBL_IF_ROUND_ELSE(46, 38), w, 18), NULL,         s_font_small);

    s_battery_state = battery_state_service_peek();
    s_signal        = connection_service_peek_pebble_app_connection();
    s_quiet         = quiet_time_is_active();

    update_time();
    update_steps();
    update_battery();
    update_signal();
    update_top_header();
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

// --- App lifecycle ----------------------------------------------------------
static void init(void) {
    prv_load_settings();

    s_window = window_create();
    window_set_background_color(s_window, COLOR_VOID);
    window_set_window_handlers(s_window, (WindowHandlers) {
        .load   = window_load,
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