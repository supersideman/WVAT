// 从 Linux pinctrl 表里提取 STM32F4 各引脚的 I2S2 复用功能（AF 号 + 引脚名）
// 说明：F429 与 F411 在这些 SPI2/I2S2 引脚上的复用时序一致，用作交叉验证。
const https = require('https');

function get(url) {
  return new Promise((resolve) => {
    https.get(url, { headers: { 'User-Agent': 'node' } }, (r) => {
      let d = '';
      r.on('data', (c) => (d += c));
      r.on('end', () => resolve({ status: r.statusCode, body: d }));
    }).on('error', () => resolve({ status: 0, body: '' }));
  });
}

(async () => {
  const url = 'https://raw.githubusercontent.com/torvalds/linux/master/drivers/pinctrl/stm32/pinctrl-stm32f429.c';
  const r = await get(url);
  if (r.status !== 200) {
    console.log('HTTP', r.status);
    return;
  }

  const blocks = r.body.split('STM32_PIN(').slice(1);
  const rows = [];
  for (const b of blocks) {
    if (!/I2S2/.test(b)) continue;
    const head = b.slice(0, 40).replace(/\s+/g, ' ');
    const pin = (head.match(/^\s*([A-Z]+\d+)/) || [])[1] || head.trim();
    const funcs = [...b.matchAll(/STM32_FUNCTION\((\d+),\s*"([^"]+)"\)/g)].map((m) => `${m[1]}:${m[2]}`);
    const i2s = funcs.filter((f) => /I2S2/.test(f));
    if (i2s.length) rows.push(`${pin.padEnd(6)} ${i2s.join('  |  ')}`);
  }

  console.log('=== 带 I2S2 复用的引脚（AF号:功能名）===');
  rows.forEach((x) => console.log('  ' + x));
})();
