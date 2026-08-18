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

// Sends a status-only update to the watch.
// 'city' appears in the signal/city text layer.
// 'conditions' (without temperature) appears in the weather layer as a raw status string.
function sendStatus(city, conditions) {
    var dict = {};
    if (city)       dict['CITY']       = city;
    if (conditions) dict['CONDITIONS'] = conditions;
    Pebble.sendAppMessage(dict,
        function() { console.log('Status sent: ' + city + ' / ' + conditions); },
        function(e) { console.log('Status send failed: ' + JSON.stringify(e)); }
    );
}

function locationSuccess(pos) {
    // Step 3 of 4: GPS lock acquired, now fetching weather + city data.
    sendStatus('>> POSITION FIX <<', '>> SAMPLING ATMO <<');

    var urlWeather = 'https://api.open-meteo.com/v1/forecast?' +
        'latitude='  + pos.coords.latitude +
        '&longitude=' + pos.coords.longitude +
        '&current=temperature_2m,weather_code';

    var urlCity = 'https://api.bigdatacloud.net/data/reverse-geocode-client?' +
        'latitude='  + pos.coords.latitude +
        '&longitude=' + pos.coords.longitude +
        '&localityLanguage=en';

    try {
        // --- Weather XHR ---
        var xhrWeather = new XMLHttpRequest();
        xhrWeather.open('GET', urlWeather, false);
        xhrWeather.send();

        if (xhrWeather.status !== 200) {
            console.log('Weather XHR failed, HTTP ' + xhrWeather.status);
            sendStatus('!! ATMO XHR FAIL !!',
                       '!! HTTP ' + xhrWeather.status + ' !!');
            return;
        }

        // --- City XHR ---
        var xhrCity = new XMLHttpRequest();
        xhrCity.open('GET', urlCity, false);
        xhrCity.send();

        if (xhrCity.status !== 200) {
            console.log('City XHR failed, HTTP ' + xhrCity.status);
            sendStatus('!! CITY XHR FAIL !!',
                       '!! HTTP ' + xhrCity.status + ' !!');
            return;
        }

        // --- Parse responses ---
        var weatherJson = JSON.parse(xhrWeather.responseText);
        var temperature = Math.round(weatherJson.current.temperature_2m);
        var conditions  = weatherCodeToCondition(weatherJson.current.weather_code);

        var city = JSON.parse(xhrCity.responseText).city;

        // Step 4 of 4: Send complete weather data to watch.
        var dictionary = {
            'TEMPERATURE': temperature,
            'CONDITIONS':  conditions,
            'CITY':        city === undefined ? '!! CITY UNDEFINED !!' : city
        };

        Pebble.sendAppMessage(dictionary,
            function(e) { console.log('Weather info sent!'); },
            function(e) {
                console.log('Error sending weather info: ' + JSON.stringify(e));
                // Can't resend via sendAppMessage here (we're already in its error cb),
                // but log is the best we can do at this point.
            }
        );

    } catch (e) {
        // Catches JSON parse errors, network exceptions, or any other thrown error.
        console.log('Exception in locationSuccess: ' + e.message);
        var errSnippet = e.message ? e.message.substring(0, 18) : 'UNKNOWN';
        sendStatus('!! EXCEPTION !!', '!!' + errSnippet + '!!');
    }
}

function locationError(err) {
    console.log('Error requesting location! Code: ' + err.code + ' - ' + err.message);
    // Send both CITY (signal layer) and CONDITIONS (weather layer) so both fields update.
    Pebble.sendAppMessage(
        {
            'CITY':       '!! GEOSYNC FAIL[' + err.code + '] !!',
            'CONDITIONS': '!! GPS ERR ' + err.code + ': ' +
                          (err.code === 1 ? 'DENIED' :
                           err.code === 2 ? 'UNAVAIL' :
                           err.code === 3 ? 'TIMEOUT' : 'UNKNOWN') + ' !!'
        },
        function(e) { console.log('Geoloc error status sent.'); },
        function(e) { console.log('Error sending geoloc error status: ' + JSON.stringify(e)); }
    );
}

function getWeather() {
    // Step 1 of 4: Check that the Geolocation API is available at all.
    if (!navigator.geolocation) {
        console.log('Geolocation API not available!');
        sendStatus('!! NO GEOLOC API !!', '!! GEOLOC UNAVAIL !!');
        return;
    }

    // Step 2 of 4: Geolocation API present; beginning position request.
    sendStatus('>LOCATING...<', '>ACQUIRING GPS<');

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
