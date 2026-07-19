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
