// Writes sounds.json (id, name, category of every sound) from index.html, so the members hub can say "N new patches".
const fs = require('fs'), path = require('path');
const root = path.join(__dirname, '..');
const s = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
const m = /window\.SOUNDS = (\[.*?\]);<\/script>/s.exec(s);
if (!m) { console.error('SOUNDS not found'); process.exit(1); }
const list = JSON.parse(m[1]).filter(x => !x.preview).map(x => ({ id: x.id, name: x.name, cat: x.cat }));
const out = JSON.stringify(list) + '\n';
const f = path.join(root, 'sounds.json');
if (!fs.existsSync(f) || fs.readFileSync(f, 'utf8') !== out) fs.writeFileSync(f, out);
console.log(list.length + ' sounds');
