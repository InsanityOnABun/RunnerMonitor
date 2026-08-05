var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

function weatherCodeToCondition(code) {
    if (code === 0) return 'Clear';
    if (code <= 3) return 'Cloudy';
    if (code <= 48) return 'Fog';
    if (code <= 55) return 'Drizzle';
    if (code <= 57) return 'Fz. Drizzle';
    if (code <= 65) return 'Rain';
    if (code <= 67) return 'Fz. Rain';
    if (code <= 75) return 'Snow';
    if (code <= 77) return 'Snow Grains';
    if (code <= 82) return 'Showers';
    if (code <= 86) return 'Snow Shwrs';
    if (code === 95) return 'T-Storm';
    if (code <= 99) return 'T-Storm';
    return 'Unknown';
}

function locationSuccess(pos) {
    var urlWeather = 'https://api.open-meteo.com/v1/forecast?' +
        'latitude=' + pos.coords.latitude +
        '&longitude=' + pos.coords.longitude +
        '&current=temperature_2m,weather_code';

    var urlCity = 'https://api.bigdatacloud.net/data/reverse-geocode-client?' +
        'latitude=' + pos.coords.latitude +
        '&longitude=' + pos.coords.longitude +
        '&localityLanguage=en';

    var xhrWeather = new XMLHttpRequest();
    xhrWeather.open("GET", urlWeather, false);
    xhrWeather.send();

    var xhrCity = new XMLHttpRequest();
    xhrCity.open("GET", urlCity, false);
    xhrCity.send();

    var weatherJson = JSON.parse(xhrWeather.responseText);
    
    var temperature = Math.round(weatherJson.current.temperature_2m);
    var conditions = weatherCodeToCondition(weatherJson.current.weather_code);

    var city = JSON.parse(xhrCity.responseText).city;

    var dictionary = {
        'TEMPERATURE': temperature,
        'CONDITIONS': conditions,
        'CITY': city === undefined ? "!! PARSE ERROR !!" : city
    };

    Pebble.sendAppMessage(dictionary,
                          function(e) { console.log('Weather info sent!'); },
                          function(e) { console.log('Error sending weather info!'); }
                         );
}

function locationError(err) {
    console.log('Error requesting location!');
    var dictionary = {
        'CITY': "!! GEOSYNC FAILURE [" + err.code + "] !!"
    };

    Pebble.sendAppMessage(dictionary,
                          function(e) { console.log('No-GPS weather info sent!'); },
                          function(e) { console.log('Error sending no-GPS weather info!'); }
                         );
}

function getWeather() {
    navigator.geolocation.getCurrentPosition(
        locationSuccess,
        locationError,
        { timeout: 15000, maximumAge: 60000 }
    );
}

Pebble.addEventListener('ready',
                        function(e) {
                            console.log('PebbleKit JS ready!');
                            getWeather();
                        }
                       );

Pebble.addEventListener('appmessage',
                        function(e) {
                            console.log('AppMessage received!');
                            if (e.payload['REQUEST_WEATHER']) {
                                getWeather();
                            }
                        }
                       );