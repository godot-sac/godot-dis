// Checks addons/dis/data/sidc_map.json against milsymbol (the reference renderer used by godot-milsymbol):
// every SIDC must be valid, and the icon part names are printed next to the DIS category for review.
// Usage: node tools/check_sidc_map.mjs [path to milsymbol package]
//        (default: ../godot-milsymbol/tools/export/node_modules/milsymbol, installed by that repo's npm install)
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const milsymbolDir = path.resolve(process.argv[2] ?? path.join(here, "..", "..", "godot-milsymbol", "tools", "export",
  "node_modules", "milsymbol"));
const src = pathToFileURL(path.join(milsymbolDir, "src")).href + "/";
const ms = (await import(pathToFileURL(path.join(milsymbolDir, "dist", "milsymbol.js")).href)).default;
const { ms: msCore } = await import(src + "ms.js");

const data = JSON.parse(fs.readFileSync(path.join(here, "..", "addons", "dis", "data", "sidc_map.json"), "utf8"));

// Icon part names per symbol set and entity code, read from milsymbol's SIDC tables with a recording proxy.
const FILES = { "01": "air", "05": "space", "10": "landunit", "15": "landequipment", "30": "sea", "35": "subsurface" };
const icn = new Proxy({}, { get: (_, key) => new String(key) });
const names = {};
for (const [set, file] of Object.entries(FILES)) {
  const table = (await import(src + "numbersidc/sidc/" + file + ".js")).default;
  const main = {};
  const m1 = {};
  try {
    table.icons(main, m1, {}, {}, set, icn, false);
  } catch {
    // some entries post-process parts; the names recorded before that are enough
  }
  names[set] = { main, m1 };
}
const describe = (parts) => (Array.isArray(parts) ? parts : [parts])
  .filter((p) => p instanceof String).map(String).join(" + ");

let failures = 0;
const entries = Object.entries(data.map).concat(Object.entries(data.domain_fallback).map(([d, v]) => [`domain ${d}`, v]));
for (const [key, [set, entity, modifier]] of entries) {
  const sidc = `1003${set}0000${entity}${modifier}00`;
  const symbol = new ms.Symbol(sidc);
  const valid = symbol.isValid();
  if (!valid) failures++;
  const icon = entity === "000000" ? "(frame only)" : describe(names[set]?.main[entity] ?? []);
  const mod = modifier !== "00" ? " | m1: " + describe(names[set]?.m1[modifier] ?? []) : "";
  console.log(`${valid ? "ok  " : "BAD "} ${key.padEnd(9)} ${(data.names[key] ?? "").padEnd(42)} ${sidc}  ${icon}${mod}`);
}
console.log(failures === 0 ? `\nall ${entries.length} SIDCs valid` : `\n${failures} invalid SIDCs`);
process.exit(failures === 0 ? 0 : 1);
