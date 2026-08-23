module.exports = [
    {
        "type": "heading",
        "defaultValue": "Watchface Settings"
    },
    {
        "type": "text",
        "defaultValue": "Customize your watchface appearance and preferences."
    },
    {
        "type": "section",
        "items": [
            {
                "type": "heading",
                "defaultValue": "Weather"
            },
            {
                "type": "toggle",
                "messageKey": "showCity",
                "defaultValue": true,
                "label": "Show City Name"
            },
            {
                "type": "toggle",
                "messageKey": "showWeather",
                "defaultValue": true,
                "label": "Show Weather Information"
            },
            {
                "type": "toggle",
                "messageKey": "useFahrenheit",
                "defaultValue": true,
                "label": "Use Fahrenheit"
            },
            {
                "type": "slider",
                "messageKey": "weatherInterval",
                "min": 10,
                "max": 30,
                "step": 10,
                "defaultValue": 30,
                "label": "Weather Update Interval",
                "description": "How often weather data updates. This happens when the current minute is cleanly divisible by this number (including 00). " +
                "For example, if this is set to 20, weather data will update when the time reads XX:20, XX:40, and XX:00. NOTE: weather " +
                "data always refreshes when the watchface is resumed — on watch restart, setting active watchface, returning from a menu " +
                "or app to the watchface, etc."
            }
        ]
    },
    {
        "type": "section",
        "items": [
            {
                "type": "heading",
                "defaultValue": "Misc"
            },
            {
                "type": "toggle",
                "messageKey": "changeBacklight",
                "defaultValue": true,
                "label": "Change Backlight Colors",
                "description": "Change backlight to Marathon Chartreuse during normal state, and Alert Magenta when Bluetooth is disconnected."
            },
            {
                "type": "input",
                "messageKey": "topText",
                "label": "Text to show at the top of the watchface",
                "defaultValue": "RUNNER // MONITOR",
                "attributes":  {
                    "placeholder": "RUNNER // MONITOR",
                    "limit": 25
                }
            }
        ]
    },
    {
        "type": "submit",
        "defaultValue": "Save Settings"
    }
];
