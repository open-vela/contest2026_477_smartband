#ifndef OPENVELA_UI_WEATHER_H
#define OPENVELA_UI_WEATHER_H

#include <stdbool.h>
#include <stdint.h>

#define OPENVELA_UI_WEATHER_FORECAST_DAYS 3

enum openvela_ui_weather_state_e {
    OPENVELA_UI_WEATHER_IDLE = 0,
    OPENVELA_UI_WEATHER_LOADING,
    OPENVELA_UI_WEATHER_READY,
    OPENVELA_UI_WEATHER_ERROR,
};

struct openvela_ui_weather_day_s {
    char weekday[16];
    char text[48];
    char icon[8];
    char minimum[12];
    char maximum[12];
};

struct openvela_ui_weather_snapshot_s {
    enum openvela_ui_weather_state_e state;
    uint32_t revision;
    int error;
    bool has_data;
    char location_id[16];
    char location_name[48];
    char administrative_area[64];
    char country[24];
    char updated_at[24];
    char observed_at[40];
    char temperature[12];
    char feels_like[12];
    char humidity[12];
    char visibility[12];
    char text[48];
    char icon[8];
    struct openvela_ui_weather_day_s
        forecast[OPENVELA_UI_WEATHER_FORECAST_DAYS];
};

/* Start the single background networking worker.  The worker reads only the
 * non-secret proxy address from /data/etc/openvela_ui/weather.conf.  QWeather
 * credentials remain on the proxy host and are never stored in the image. */
int openvela_ui_weather_start(void);

/* Queue a weather refresh.  A newer request supersedes any older in-flight
 * request, so a late response can never overwrite the currently selected
 * city. */
int openvela_ui_weather_request(const char *location_id,
                                const char *location,
                                const char *administrative_area);

/* Copy the latest immutable result for consumption by the LVGL thread. */
int openvela_ui_weather_snapshot(
    struct openvela_ui_weather_snapshot_s *snapshot);

#endif
