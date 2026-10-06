var Clay = require('@rebble/clay');
var clayConfig = require('./config');

// Clay renders the settings page, remembers the answers on the phone and posts
// them to the watch keyed by the messageKeys in package.json.
var clay = new Clay(clayConfig, null, { autoHandleEvents: true });

// ------------------------------------------------------------- weather ----
// Open-Meteo needs no API key. The watch only gets a condition, the
// temperature in whole degrees C, day/night and wind; the codes below must
// match WeatherKind in src/c/anim.h.

var WX = { CLEAR: 1, PARTLY: 2, CLOUDY: 3, FOG: 4, DRIZZLE: 5, RAIN: 6, SNOW: 7, THUNDER: 8 };

// WMO weather interpretation codes, as Open-Meteo reports them.
function kindFor(code) {
  if (code === 0) return WX.CLEAR;
  if (code <= 2) return WX.PARTLY;
  if (code === 3) return WX.CLOUDY;
  if (code === 45 || code === 48) return WX.FOG;
  if (code >= 51 && code <= 57) return WX.DRIZZLE;
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return WX.RAIN;
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return WX.SNOW;
  if (code >= 95) return WX.THUNDER;
  return WX.CLOUDY;
}

function weatherEnabled() {
  try {
    var saved = JSON.parse(localStorage.getItem('clay-settings') || '{}');
    return saved.Weather !== false;
  } catch (e) {
    return true;
  }
}

function send(lat, lon) {
  var url = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat.toFixed(3) +
    '&longitude=' + lon.toFixed(3) +
    '&current=temperature_2m,weather_code,is_day,wind_speed_10m';
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    try {
      var cur = JSON.parse(this.responseText).current;
      Pebble.sendAppMessage({
        WxCode: kindFor(cur.weather_code),
        WxTemp: Math.round(cur.temperature_2m),
        WxDay: cur.is_day ? 1 : 0,
        WxWind: Math.min(255, Math.round(cur.wind_speed_10m))
      });
    } catch (e) {
      console.log('Orbi: bad weather response ' + e);
    }
  };
  xhr.onerror = function () {
    console.log('Orbi: weather request failed');
  };
  xhr.open('GET', url);
  xhr.send();
}

function fetchWeather() {
  if (!weatherEnabled()) return;
  navigator.geolocation.getCurrentPosition(
    function (pos) {
      localStorage.setItem('orbi-loc', JSON.stringify([pos.coords.latitude, pos.coords.longitude]));
      send(pos.coords.latitude, pos.coords.longitude);
    },
    function () {
      // No fix right now: the last known place is close enough for weather.
      var last = JSON.parse(localStorage.getItem('orbi-loc') || 'null');
      if (last) send(last[0], last[1]);
    },
    { timeout: 15000, maximumAge: 60 * 60 * 1000 }
  );
}

Pebble.addEventListener('ready', fetchWeather);

Pebble.addEventListener('appmessage', function (e) {
  if (e.payload && e.payload.WxRequest) fetchWeather();
});

// A settings change may have just switched weather on.
Pebble.addEventListener('webviewclosed', function () {
  setTimeout(fetchWeather, 1000);
});
