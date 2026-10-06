// Clay settings page for Orbi.
//
// Keys must match `messageKeys` in package.json and src/c/settings.c, and the
// defaults here repeat settings.c so the page opens showing what the watch is
// actually doing on a first run.

function hours() {
  var out = [];
  for (var h = 0; h < 24; h++) {
    var label = (h % 12 === 0 ? 12 : h % 12) + (h < 12 ? ' am' : ' pm');
    out.push({ label: label, value: String(h) });
  }
  return out;
}

module.exports = [
  { type: 'heading', defaultValue: 'Orbi', size: 1 },
  {
    type: 'text',
    defaultValue: 'A glossy little orb who lives on your wrist. He reacts to the weather, ' +
      'wears hats for the season, and pulls faces when you flick your wrist or tap him.'
  },

  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Clock' },
      {
        type: 'select',
        messageKey: 'ClockFormat',
        label: 'Format',
        defaultValue: '0',
        options: [
          { label: 'Match watch setting', value: '0' },
          { label: '12 hour', value: '1' },
          { label: '24 hour', value: '2' }
        ]
      },
      { type: 'toggle', messageKey: 'LeadingZero', label: 'Leading zero', defaultValue: true },
      {
        type: 'select',
        messageKey: 'ClockStyle',
        label: 'Style',
        defaultValue: '0',
        options: [
          { label: 'White on a black pill', value: '0' },
          { label: 'Plain black', value: '1' },
          { label: 'Hidden (just Orbi)', value: '2' }
        ]
      }
    ]
  },

  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Date' },
      {
        type: 'select',
        messageKey: 'DateFormat',
        label: 'Format',
        defaultValue: '1',
        options: [
          { label: 'Hidden', value: '0' },
          { label: 'Tue 06 Oct', value: '1' },
          { label: 'Tue 06', value: '2' },
          { label: '06 Oct', value: '3' },
          { label: 'Oct 06', value: '4' },
          { label: '06/10', value: '5' },
          { label: '10/06', value: '6' }
        ]
      },
      {
        type: 'toggle',
        messageKey: 'ShowTemp',
        label: 'Show the temperature after the date',
        description: 'Needs weather turned on below.',
        defaultValue: true
      }
    ]
  },

  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Battery' },
      {
        type: 'select',
        messageKey: 'BatteryStyle',
        label: 'Battery percentage',
        defaultValue: '1',
        options: [
          { label: 'Hidden', value: '0' },
          { label: 'Always shown', value: '1' },
          { label: 'Only when low or charging', value: '2' }
        ]
      }
    ]
  },

  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Battery saving' },
      {
        type: 'text',
        defaultValue: 'Animation is what costs battery. Orbi only draws frames while he is ' +
          'moving, so the calmer he is, the longer the watch lasts.'
      },
      {
        type: 'select',
        messageKey: 'Live',
        label: 'Liveliness',
        defaultValue: '2',
        options: [
          { label: 'Still: never moves', value: '0' },
          { label: 'Calm: only when you flick or tap', value: '1' },
          { label: 'Lively: puts on a show every few minutes', value: '2' },
          { label: 'Always moving (heavy on the battery)', value: '3' }
        ]
      },
      {
        type: 'slider',
        messageKey: 'ActEvery',
        label: 'Minutes between shows (Lively)',
        defaultValue: 2,
        min: 1,
        max: 15,
        step: 1
      },
      {
        type: 'select',
        messageKey: 'SaverPct',
        label: 'Go still when the battery is at or below',
        defaultValue: '20',
        options: [
          { label: 'Never', value: '0' },
          { label: '10%', value: '10' },
          { label: '20%', value: '20' },
          { label: '30%', value: '30' },
          { label: '50%', value: '50' }
        ]
      },
      {
        type: 'toggle',
        messageKey: 'Quiet',
        label: 'Quiet hours',
        description: 'Orbi puts his nightcap on and sleeps. A flick wakes him for a moment.',
        defaultValue: false
      },
      { type: 'select', messageKey: 'QuietStart', label: 'Sleeps at', defaultValue: '23', options: hours() },
      { type: 'select', messageKey: 'QuietEnd', label: 'Wakes at', defaultValue: '7', options: hours() },
      { type: 'toggle', messageKey: 'WakeFlick', label: 'React to a wrist flick', defaultValue: true },
      { type: 'toggle', messageKey: 'WakeTap', label: 'React to a tap on the screen', defaultValue: true },
      {
        type: 'toggle',
        messageKey: 'WakeRaise',
        label: 'Say hello when you raise your wrist',
        description: 'Reads the accelerometer all the time, so it costs some battery.',
        defaultValue: false
      }
    ]
  },

  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Orbi' },
      {
        type: 'select',
        messageKey: 'Theme',
        label: 'Colour',
        defaultValue: '0',
        options: [
          { label: 'Classic yellow', value: '0' },
          { label: 'Tangerine', value: '1' },
          { label: 'Bubblegum', value: '2' },
          { label: 'Mint', value: '3' },
          { label: 'Sky', value: '4' }
        ]
      },
      { type: 'toggle', messageKey: 'Props', label: 'Hats, shades and umbrellas', defaultValue: true },
      {
        type: 'toggle',
        messageKey: 'Seasonal',
        label: 'Seasonal hats',
        description: 'A witch hat for Halloween, a Santa hat in December.',
        defaultValue: true
      },
      { type: 'toggle', messageKey: 'Hourly', label: 'Party on the hour', defaultValue: true },
      {
        type: 'toggle',
        messageKey: 'BtAlert',
        label: 'Worry when the phone disconnects',
        description: 'Vibrates, and Orbi looks around for it.',
        defaultValue: true
      }
    ]
  },

  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Weather' },
      {
        type: 'toggle',
        messageKey: 'Weather',
        label: 'React to the weather',
        description: 'Rain, snow, sun, fog, wind and thunder, from your phone\'s location ' +
          '(Open-Meteo, updated every 30 minutes).',
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'Fahrenheit',
        label: 'Temperature in Fahrenheit',
        defaultValue: false
      }
    ]
  },

  { type: 'submit', defaultValue: 'Save' }
];
