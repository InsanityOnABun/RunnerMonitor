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

// Generic async XHR helper.
// Calls onSuccess(responseText) on HTTP 200, onFailure(statusOrErrorString) otherwise.
function xhrRequest(url, onSuccess, onFailure) {
    var xhr = new XMLHttpRequest();
    xhr.onload = function() {
        if (xhr.status === 200) {
            onSuccess(xhr.responseText);
        } else {
            onFailure(xhr.status);
        }
    };
    xhr.onerror = function() {
        onFailure('NET ERR');
    };
    xhr.ontimeout = function() {
        onFailure('TIMEOUT');
    };
    xhr.open('GET', url, true);
    xhr.send();
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

    // Results collected from the two parallel requests.
    var results = { weather: null, city: null };
    var pending = 2;
    var failed  = false; // Ensures we only report the first failure once.

    function checkDone() {
        pending--;
        if (pending > 0 || failed) {
            return;
        }

        // Step 4 of 4: Send complete weather data to watch.
        var dictionary = {
            'TEMPERATURE': results.weather.temperature,
            'CONDITIONS':  results.weather.conditions,
            'CITY':        results.city === undefined ? '!! CITY UNDEFINED !!' : results.city
        };

        Pebble.sendAppMessage(dictionary,
            function(e) { console.log('Weather info sent!'); },
            function(e) {console.log('Error sending weather info: ' + JSON.stringify(e));}
        );
    }

    function fail(cityMsg, condMsg) {
        if (failed) return;
        failed = true;
        sendStatus(cityMsg, condMsg);
    }

    // --- Weather XHR (async) ---
    xhrRequest(urlWeather,
        function(responseText) {
            try {
                var weatherJson = JSON.parse(responseText);
                results.weather = {
                    temperature: Math.round(weatherJson.current.temperature_2m),
                    conditions:  weatherCodeToCondition(weatherJson.current.weather_code)
                };
                checkDone();
            } catch (e) {
                console.log('Exception parsing weather: ' + e.message);
                var errSnippet = e.message ? e.message.substring(0, 18) : 'UNKNOWN';
                fail('!! EXCEPTION !!', '!!' + errSnippet + '!!');
            }
        },
        function(status) {
            console.log('Weather XHR failed, HTTP ' + status);
            fail('!! ATMO XHR FAIL !!', '!! HTTP ' + status + ' !!');
        }
    );

    // --- City XHR (async) ---
    xhrRequest(urlCity,
        function(responseText) {
            try {
                results.city = JSON.parse(responseText).city;
                checkDone();
            } catch (e) {
                console.log('Exception parsing city: ' + e.message);
                var errSnippet = e.message ? e.message.substring(0, 18) : 'UNKNOWN';
                fail('!! EXCEPTION !!', '!!' + errSnippet + '!!');
            }
        },
        function(status) {
            console.log('City XHR failed, HTTP ' + status);
            fail('!! CITY XHR FAIL !!', '!! HTTP ' + status + ' !!');
        }
    );
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
    sendStatus('>> LOCATING <<', '>> ACQUIRING GPS <<');

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
