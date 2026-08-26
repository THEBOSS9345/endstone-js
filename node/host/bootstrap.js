const binding = process._linkedBinding('endstone_node');
const util = require('node:util');
const forward = (level) => (...args) => binding.log(level, util.format(...args));
console.log = forward(2);
console.info = forward(2);
console.debug = forward(1);
console.trace = forward(1);
console.warn = forward(3);
console.error = forward(4);
process.on('uncaughtException', (err) => {
  binding.log(5, 'uncaughtException: ' + ((err && err.stack) || err));
});
process.on('unhandledRejection', (err) => {
  binding.log(4, 'unhandledRejection: ' + ((err && err.stack) || err));
});

// The host was compiled against one Node version; libnode at runtime could be another. N-API is
// ABI-stable so this usually still works, but say so rather than let it be mysterious.
if (binding.compiledNodeVersion && process.versions.node !== binding.compiledNodeVersion) {
  binding.log(3,
    `Node version mismatch: the host was built against v${binding.compiledNodeVersion} but libnode ` +
    `reports v${process.versions.node}. Rebuild the host against this libnode if anything misbehaves.`);
}

// A plugin calling process.exit() would take the Minecraft server down with it.
for (const name of ['exit', 'abort', 'reallyExit']) {
  const original = process[name];
  if (typeof original === 'function') {
    process[name] = (...args) => {
      binding.log(4,
        `a plugin called process.${name}(${args.join(', ')}); ignoring - that would stop the server. ` +
        `Use the Endstone API to shut down instead.`);
    };
  }
}

// --- plugin runtime -------------------------------------------------------------------------
// All plugins share this environment and event loop; isolation is by module scope, and each plugin
// gets its own require rooted at its own directory so it resolves its own node_modules.
const nodePath = require('node:path');
const nodeFs = require('node:fs');
const nodeUrl = require('node:url');
const NodeModule = require('node:module');

// --- @endstone-js/server -----------------------------------------------------------------------
// The API surface. Shaped after Endstone's own API (Server, Logger, and later Player and the
// PlayerXxxEvent family) rather than Bedrock's ScriptAPI, so it reads 1:1 with the Endstone docs.
const LEVELS = { trace: 0, debug: 1, info: 2, warning: 3, error: 4, critical: 5 };

function makeLogger(prefix) {
  const emit = (level) => (...args) => {
    const text = util.format(...args);
    binding.pluginLog(level, prefix ? `[${prefix}] ${text}` : text);
  };
  return {
    trace: emit(LEVELS.trace),
    debug: emit(LEVELS.debug),
    info: emit(LEVELS.info),
    warning: emit(LEVELS.warning),
    warn: emit(LEVELS.warning),
    error: emit(LEVELS.error),
    critical: emit(LEVELS.critical),
  };
}

// A ban entry crosses as one 0x1f-delimited record per line: name, uuid, xuid, reason, source, created,
// expiration. Dates are epoch milliseconds, and an empty expiration means the ban is permanent.
const UNIT_SEPARATOR = String.fromCharCode(31);
function readBanRecords(joined) {
  if (typeof joined !== 'string' || joined === '') return [];
  return joined.split(NEWLINE).map((line) => {
    const [target, uuid, xuid, reason, source, created, expires] = line.split(UNIT_SEPARATOR);
    return {
      target, reason, source,
      uniqueId: uuid || null,
      xuid: xuid || null,
      created: created ? new Date(Number(created)) : null,
      expires: expires ? new Date(Number(expires)) : null,
    };
  });
}

// The bridge reads target, reason and source as strings in that order, then a duration in seconds; 0
// means permanent. `expires` accepts a Date or a millisecond timestamp and is converted here, because
// only the duration crosses.
function banTarget(method, target, options) {
  const name = String(target ?? '');
  if (!name) throw new TypeError(`${method}: a target is required`);
  let seconds = 0;
  if (options && options.expires !== undefined && options.expires !== null) {
    const at = options.expires instanceof Date ? options.expires.getTime() : Number(options.expires);
    seconds = Math.max(Math.ceil((at - Date.now()) / 1000), 1);
  } else if (options && options.durationSeconds) {
    seconds = Math.max(Math.ceil(Number(options.durationSeconds)), 1);
  }
  binding.invoke(serverHandle, method, name, String(options?.reason ?? ''), String(options?.source ?? ''), seconds);
}

// "key\x1f<b|i|s>\x1fvalue" per line. The tag is what keeps a block state's JavaScript type honest:
// `upside_down_bit` is a boolean, `facing_direction` an integer, `wall_connection_type_east` a string.
function readBlockStates(joined) {
  if (typeof joined !== 'string' || joined === '') return {};
  const out = {};
  for (const line of joined.split(NEWLINE)) {
    const [key, kind, ...rest] = line.split(UNIT_SEPARATOR);
    const raw = rest.join(UNIT_SEPARATOR);
    out[key] = kind === 'b' ? raw === '1' : kind === 'i' ? Number(raw) : raw;
  }
  return out;
}

// The inverse of readBlockStates. The tag travels with each value because BlockStates is a variant:
// `upside_down_bit` is a boolean, `facing_direction` an integer, `wall_connection_type_east` a string,
// and Bedrock treats "true" the string and true the boolean as different states.
function writeBlockStates(states) {
  if (states === null || typeof states !== 'object') {
    throw new TypeError('blockStates must be an object of state names to values');
  }
  return Object.entries(states).map(([key, value]) => {
    if (typeof value === 'boolean') return `${key}${UNIT_SEPARATOR}b${UNIT_SEPARATOR}${value ? '1' : '0'}`;
    if (typeof value === 'number') return `${key}${UNIT_SEPARATOR}i${UNIT_SEPARATOR}${Math.round(value)}`;
    return `${key}${UNIT_SEPARATOR}s${UNIT_SEPARATOR}${String(value)}`;
  }).join(NEWLINE);
}

const serverBase = {
  get name() { return binding.serverName(); },
  get version() { return binding.serverVersion(); },
  get minecraftVersion() { return binding.serverMinecraftVersion(); },
  get protocolVersion() { return binding.serverProtocolVersion(); },
  get onlinePlayerCount() { return binding.serverOnlinePlayerCount(); },
  get isAvailable() { return binding.apiAvailable(); },
  /** The loaded level, or null before one exists. Safe to keep: the level outlives any callback. */
  get level() { return wrap(binding.serverLevel()); },
  logger: makeLogger(null),
  broadcastMessage(message) { binding.broadcastMessage(String(message)); },
  /**
   * Every player currently online. Built from onlinePlayerCount and getOnlinePlayer because an
   * accessor carries one value and cannot return an array.
   *
   * Dispatch-scoped like any player: read it again next tick rather than keeping the result.
   */
  get onlinePlayers() {
    if (!serverHandle) return [];
    const players = [];
    const count = binding.serverOnlinePlayerCount();
    for (let index = 0; index < count; ++index) {
      const player = wrap(asHandle(binding.invoke(serverHandle, 'getOnlinePlayer', index)) ?? 0);
      if (player) players.push(player);
    }
    return players;
  },
  /**
   * Looks up an online player by name or UUID, or null if they are not online. The only way to reach a
   * player outside an event - but the result is dispatch-scoped like any player, so look it up again
   * next time rather than keeping it.
   */
  getPlayer(nameOrUuid) {
    if (!serverHandle) return null;
    return wrap(asHandle(binding.get(serverHandle, 'player:' + String(nameOrUuid))) ?? 0);
  },
  /**
   * A blank map for a plugin to hand out. Server-owned, so the object is safe to keep.
   * `dimension` is where the map is anchored; it defaults to the overworld.
   */
  createMap(dimension = 'overworld') {
    if (!serverHandle) throw new Error('createMap: the Endstone API is not available yet');
    return wrap(asHandle(binding.invoke(serverHandle, 'createMap', String(dimension))) ?? 0);
  },
  /**
   * A fresh scoreboard, separate from the main one, so a sidebar can differ per player. Assign it with
   * `player.scoreboard = board`. Kept alive by the runtime for as long as the plugin is loaded.
   */
  createScoreboard() {
    if (!serverHandle) throw new Error('createScoreboard: the Endstone API is not available yet');
    return wrap(asHandle(binding.invoke(serverHandle, 'createScoreboard')) ?? 0);
  },
  /**
   * Resolves a Minecraft translation key against the server's language files.
   *
   * `params` fills the `%s`/`%1` placeholders; `locale` overrides the server's own.
   */
  translate(key, params = [], locale = '') {
    const parts = [String(locale), String(key), ...(params ?? []).map(String)];
    return binding.get(serverHandle, 'translate:' + parts.join(UNIT_SEPARATOR));
  },
  /**
   * Registry lookups keyed by id, so a plugin can ask about a type it does not hold an instance of -
   * what a durability bar or a level cap needs before the item exists.
   *
   * An id that is not registered answers -1. That is deliberately not 0, which is a real answer
   * meaning "does not wear out".
   */
  getItemMaxDurability(type) {
    return binding.get(serverHandle, 'itemMaxDurability:' + String(type));
  },
  getItemMaxStackSize(type) {
    return binding.get(serverHandle, 'itemMaxStackSize:' + String(type));
  },
  getEnchantmentMaxLevel(id) {
    return binding.get(serverHandle, 'enchantMaxLevel:' + String(id));
  },
  getEnchantmentStartLevel(id) {
    return binding.get(serverHandle, 'enchantStartLevel:' + String(id));
  },
  /** The server's own locale, e.g. `"en_US"`. */
  get locale() { return binding.get(serverHandle, 'locale'); },
  /**
   * The block states a type has by default, e.g. `{ upside_down_bit: false, direction: 0 }` — without
   * needing a block in the world to read them off.
   */
  blockStates(type) {
    return readBlockStates(binding.get(serverHandle, 'blockStates:' + String(type)));
  },
  /**
   * The network id of a block type's default palette entry, or 0 if the type is unknown.
   *
   * This is what an `UpdateBlockPacket` carries, so it is the piece you need to show a block to one
   * player only. Emitted as text and parsed here, because it shares the prefixed-accessor path.
   */
  blockRuntimeId(type) {
    return Number(binding.get(serverHandle, 'blockRuntimeId:' + String(type)) || 0);
  },
  /** Bans, as plain objects. The lists are read fresh each time, so they include offline players. */
  get bannedPlayers() { return readBanRecords(binding.get(serverHandle, 'banList')); },
  get bannedIps() { return readBanRecords(binding.get(serverHandle, 'ipBanList')); },
  getBanEntry(name) { return readBanRecords(binding.get(serverHandle, 'ban:' + String(name)))[0] ?? null; },
  getIpBanEntry(address) {
    return readBanRecords(binding.get(serverHandle, 'ipBan:' + String(address)))[0] ?? null;
  },
  isBanned(name) { return binding.get(serverHandle, 'isBanned:' + String(name)) === true; },
  isIpBanned(address) { return binding.get(serverHandle, 'isIpBanned:' + String(address)) === true; },
  /**
   * Bans a player by name, or an IP address. Assembled here rather than going through the generic
   * method path because the second argument is an options object, not a value.
   *
   * Banning does not kick anyone who is already connected - call player.kick() as well.
   */
  banPlayer(target, options = {}) { banTarget('banPlayer', target, options); },
  banIp(address, options = {}) { banTarget('banIp', address, options); },
  unbanPlayer(target) { binding.invoke(serverHandle, 'unbanPlayer', String(target)); },
  unbanIp(address) { binding.invoke(serverHandle, 'unbanIp', String(address)); },
  /**
   * Creates a boss bar. Unlike most objects handed to JavaScript this one is not dispatch-scoped: the
   * host owns it, so keep the returned bar and update it from a timer. Call bar.remove() to free it -
   * nothing else does, short of the plugin unloading.
   */
  createBossBar(spec = {}) {
    if (!serverHandle) throw new Error('createBossBar: the Endstone API is not available yet');
    const color = String(spec.color ?? 'white');
    const style = String(spec.style ?? 'solid');
    const bar = wrap(asHandle(binding.invoke(serverHandle, 'createBossBar', String(spec.title ?? ''), color, style)));
    if (!bar) throw new Error('createBossBar: the server refused to create a boss bar');
    if (spec.progress !== undefined) bar.progress = Number(spec.progress);
    if (spec.visible !== undefined) bar.visible = Boolean(spec.visible);
    if (spec.darkenSky) bar.darkenSky = true;
    if (spec.createFog) bar.createFog = true;
    for (const player of spec.players ?? []) bar.addPlayer(player);
    return bar;
  },
};

// Handle-backed objects. A Proxy forwards property reads and writes straight to the generic
// accessors, so `event.player.name` and `event.cancelled = true` work without a binding per property.
// Handles are dispatch-scoped: valid only inside the callback that produced them.
const HANDLE = Symbol('endstone.handle');

// Methods rather than properties, per object type. This mirrors the Endstone side, which dispatches on
// the handle's actual kind - so a name is only callable on a type that really implements it.
//
// One flat set across every kind would be smaller, and wrong in two ways. `typeof block.addPlayer`
// would be 'function', so feature detection lies. And a name that some type exposes as a *property*
// would, once added here for a different type, shadow that property and hand back a function instead of
// its value - silently, on the type that never asked for the method.
//
// Keep this in step with ApiBridge::invoke: each entry is one of its `resolve(target, Kind::X)`
// branches. Adding a method to the bridge and not to the type here leaves it uncallable.
const METHODS_BY_TYPE = {
  CommandSender: ['sendMessage', 'sendErrorMessage', 'addPermission', 'removePermission',
                  'recalculatePermissions'],
  Actor: ['sendMessage', 'remove', 'addScoreboardTag', 'removeScoreboardTag', 'setRotation', 'teleport'],
  Mob: [],
  Player: [
    'sendErrorMessage', 'sendPopup', 'sendTip', 'sendTitle', 'resetTitle', 'sendToast', 'kick',
    'performCommand', 'updateCommands', 'transfer', 'giveExp', 'giveExpLevels', 'playSound',
    'stopSound', 'stopAllSounds', 'spawnParticle', 'sendMap', 'setScoreboard',
  ],
  Block: ['getRelative', 'captureState', 'clone'],
  BlockState: ['update'],
  Level: ['getDimension'],
  Dimension: ['getBlockAt', 'getHighestBlockAt', 'spawnActor', 'getActor', 'dropItem'],
  Server: [
    'dispatchCommand', 'createBossBar', 'reloadData', 'broadcast',
    'banPlayer', 'unbanPlayer', 'banIp', 'unbanIp', 'createMap', 'createScoreboard',
    'getOnlinePlayer', 'getMap', 'shutdown', 'reload',
  ],
  BossBar: ['addPlayer', 'removePlayer', 'removeAll', 'addFlag', 'removeFlag', 'remove'],
  Scoreboard: [
    'addObjective', 'removeObjective', 'setDisplayName', 'setDisplay', 'setDisplaySlot',
    'setSortOrder', 'clearSlot',
    'setScore', 'addScore', 'resetScores',
  ],
  Inventory: ['getItem', 'setItem', 'addItem', 'removeItem', 'clear', 'remove', 'removeStack'],
  PlayerInventory: [
    'setHeldItemSlot', 'setHelmet', 'setChestplate', 'setLeggings', 'setBoots',
    'setItemInMainHand', 'setItemInOffHand',
  ],
  ItemStack: ['removeTag', 'addEnchant', 'removeEnchant', 'removeEnchants', 'clone',
              'setMapView', 'addPage', 'addChargedProjectile'],
  Event: ['cancel', 'getExplodedBlock', 'setKnockback', 'setFrom', 'setTo'],
  MapCanvas: ['setPixel'],
};

// Endstone's own hierarchy, so a Player answers to everything an Actor does.
const TYPE_PARENT = {
  Player: 'Mob', Mob: 'Actor', Actor: 'CommandSender', Item: 'Actor', PlayerInventory: 'Inventory',
};

// Flattened per type on first use. The type of a handle never changes, so this is computed once per
// type rather than per property access.
const methodCache = new Map();
const methodsFor = (typeName) => {
  let names = methodCache.get(typeName);
  if (names) return names;
  names = new Set();
  for (let type = typeName; type; type = TYPE_PARENT[type]) {
    for (const name of METHODS_BY_TYPE[type] ?? []) names.add(name);
  }
  methodCache.set(typeName, names);
  return names;
};

// An item is described by { type, amount, data } rather than by constructing an ItemStack, so the
// flattening is the same trick vectors use: strings and numbers in the order the host reads them.
const ITEM_KEYS = ['amount', 'data'];
const flattenItem = (item) => {
  if (item === null || item === undefined) return [''];  // empty type clears the slot
  if (typeof item === 'string') return [item, 1, 0];
  const type = typeof item.type === 'string' ? item.type : '';
  return [type, ...ITEM_KEYS.map((key) => (Number.isFinite(item[key]) ? item[key] : key === 'amount' ? 1 : 0))];
};

// Derived from size and getItem rather than crossing the ABI: the host has no way to return a boolean
// or an array from a method call, and looping a few dozen slots in JS costs nothing.
// Methods whose arguments include an item description rather than plain scalars.
const ITEM_METHODS = new Set([
  'setItem', 'addItem', 'removeItem',
  'setHelmet', 'setChestplate', 'setLeggings', 'setBoots', 'setItemInMainHand', 'setItemInOffHand',
]);

// A type id matches by name only; a whole stack matches on metadata too - NBT, enchantments, a custom
// name - which is the difference between "a pickaxe" and "the enchanted pickaxe". Passing a stack
// routes to Endstone's own comparison rather than the loop below.
const INVENTORY_HELPERS = {
  contents() {
    const all = [];
    for (let slot = 0; slot < this.size; ++slot) all.push(this.getItem(slot));
    return all;
  },
  contains(typeOrStack) {
    const against = asHandle(typeOrStack);
    if (against !== null) return binding.get(this.handle, 'containsStack:' + against) === true;
    return this.first(typeOrStack) !== -1;
  },
  first(typeOrStack) {
    const against = asHandle(typeOrStack);
    if (against !== null) return Number(binding.get(this.handle, 'firstStack:' + against));
    for (let slot = 0; slot < this.size; ++slot) {
      const item = this.getItem(slot);
      if (item && item.type === typeOrStack) return slot;
    }
    return -1;
  },
  countOf(type) {
    let total = 0;
    for (let slot = 0; slot < this.size; ++slot) {
      const item = this.getItem(slot);
      if (item && item.type === type) total += item.amount;
    }
    return total;
  },
  containsAtLeast(typeOrStack, amount) {
    const against = asHandle(typeOrStack);
    if (against !== null) {
      return binding.get(this.handle, `containsStack:${against},${Math.round(amount)}`) === true;
    }
    return this.countOf(typeOrStack) >= amount;
  },
  /** Every slot holding that type, or that exact stack. Empty array when there are none. */
  all(typeOrStack) {
    const against = asHandle(typeOrStack);
    if (against !== null) {
      const joined = binding.get(this.handle, 'allStacks:' + against);
      return typeof joined === 'string' && joined !== '' ? joined.split(NEWLINE).map(Number) : [];
    }
    const slots = [];
    for (let slot = 0; slot < this.size; ++slot) {
      const item = this.getItem(slot);
      if (item && item.type === typeOrStack) slots.push(slot);
    }
    return slots;
  },
  /**
   * Replaces the whole inventory. Slots past the end of `items` are cleared, so this sets the inventory
   * to exactly what is passed. Repeated setItem rather than one bridge call: the ABI cannot carry an
   * array, and a few dozen slots costs nothing.
   */
  setContents(items) {
    if (!Array.isArray(items)) throw new TypeError('setContents(items): items must be an array');
    for (let slot = 0; slot < this.size; ++slot) {
      this.setItem(slot, slot < items.length ? items[slot] : null);
    }
  },
};

// Which typed accessor answers a member, keyed by type and name.
//
// The bridge dispatches on the property name inside each accessor, so probing all five walks the
// type's branch up to five times before one answers - `player.health` is a failed string dispatch and
// a failed boolean one before get_int hits. The winner never changes for a given type and name, so it
// is remembered and asked for directly from then on. The table is module-level rather than per proxy:
// a nested read mints a fresh proxy every time, and a per-proxy table would be thrown away unused.
const MEMBER_KINDS = [0, 1, 2, 3, 4];  // string, bool, int, double, handle - matches MemberKind
// Built rather than written literally: a NUL in this file would terminate the C++ raw string the
// bootstrap is embedded in, truncating everything after it.
const MEMBER_KEY_SEP = String.fromCharCode(0);
const memberKinds = new Map();

function readMember(handle, type, prop) {
  const key = type + MEMBER_KEY_SEP + prop;
  const known = memberKinds.get(key);
  if (known !== undefined) {
    const value = binding.getAs(handle, prop, known);
    // undefined means that accessor did not answer, which for a known member means the type's shape
    // changed under us; fall through and find it again rather than reporting it missing.
    if (value !== undefined) return value;
  }
  for (const kind of MEMBER_KINDS) {
    if (kind === known) continue;
    const value = binding.getAs(handle, prop, kind);
    if (value !== undefined) {
      memberKinds.set(key, kind);
      return value;
    }
  }
  return undefined;
}

// The separator every list-valued accessor is joined on. Spelled as a char code out of habit from when
// this file lived inside a C++ raw string literal; harmless, and it stays unambiguous next to the 0x1f
// record separator below.
const NEWLINE = String.fromCharCode(10);

// Packet payloads are bytes, and they cross the ABI as latin1 - one code unit per byte, NULs included.
// Accepts a Uint8Array, an ArrayBuffer, or a string that is already in that form.
function toByteString(payload) {
  if (payload === null || payload === undefined) return '';
  if (typeof payload === 'string') return payload;
  const bytes = payload instanceof Uint8Array ? payload
    : payload instanceof ArrayBuffer ? new Uint8Array(payload)
    : ArrayBuffer.isView(payload) ? new Uint8Array(payload.buffer, payload.byteOffset, payload.byteLength)
    : null;
  if (!bytes) throw new TypeError('payload must be a Uint8Array, ArrayBuffer or byte string');
  let out = '';
  // Chunked so a large packet does not blow the argument limit of String.fromCharCode.
  for (let i = 0; i < bytes.length; i += 8192) {
    out += String.fromCharCode.apply(null, bytes.subarray(i, i + 8192));
  }
  return out;
}

/** The inverse: a latin1 byte string back to bytes, for decoding a payload read off an event. */
function toBytes(text) {
  const bytes = new Uint8Array(text.length);
  for (let i = 0; i < text.length; ++i) bytes[i] = text.charCodeAt(i) & 0xff;
  return bytes;
}

/** The host tags nested objects so they can be told apart from plain numbers. */
const asHandle = (value) =>
  value !== null && typeof value === 'object' && typeof value.__esn_handle === 'number'
    ? value.__esn_handle
    : null;

// Vector and rotation arguments arrive as objects ({x,y,z} / {yaw,pitch}). Flatten them back into
// the positional numbers the host reads: x, y, z, then yaw, pitch. Reading through the Proxy works,
// so teleport(p.location, {...}) and teleport({x,y,z}) both behave; a missing member throws, which
// is fine - it just means that key is absent.
// A vector contributes exactly x, y, z; a rotation contributes yaw, pitch. A Location satisfies both,
// and the vector reading wins, so facing is always passed as its own argument - that is what makes
// teleport(location) keep the current facing while teleport(location, rotation) turns the actor.
const POSITION_KEYS = ['x', 'y', 'z'];
const ROTATION_KEYS = ['yaw', 'pitch'];
const readNumber = (obj, key) => {
  try {
    const value = obj[key];
    return typeof value === 'number' ? value : NaN;
  } catch {
    return NaN;
  }
};
// All of the keys must be present, so a partial object is passed through untouched rather than
// silently flattening into the wrong positions.
const numbersOf = (arg, keys) => {
  if (arg === null || typeof arg !== 'object') return null;
  const values = keys.map((key) => readNumber(arg, key));
  return values.every((value) => Number.isFinite(value)) ? values : null;
};
const flatten = (args) => {
  const flat = [];
  for (const arg of args) {
    const spread = numbersOf(arg, POSITION_KEYS) ?? numbersOf(arg, ROTATION_KEYS);
    if (spread) {
      flat.push(...spread);
    } else {
      flat.push(arg);
    }
  }
  return flat;
};

/** A location's dimension name, or null for a plain { x, y, z }. Reading it may throw; that is fine. */
const readDimension = (value) => {
  try {
    const dimension = value.dimension;
    return typeof dimension === 'string' && dimension !== '' ? dimension : null;
  } catch {
    return null;
  }
};

// Rotation writes go through their own flattening: only yaw/pitch are picked, so assigning
// `actor.rotation = actor.location` reads the facing off the location rather than the position.
const flattenRotation = (value) =>
  ['yaw', 'pitch'].filter((key) => Number.isFinite(readNumber(value, key))).map((key) => value[key]);

// Proxies by handle.
//
// A handle id is never reused - the bridge counts up and never hands the same one out twice - so a
// cached proxy always refers to the object it was made for, and once that goes stale using it throws
// exactly as a fresh one would. The cache is therefore purely an optimisation and dropping an entry is
// always safe, which is what lets it be capped rather than tied to dispatch boundaries.
//
// Reuse is what makes the per-proxy caches worth having: without it `event.player.name` followed by
// `event.player.health` builds two proxies, resolves the type twice and shares no bound methods.
const PROXY_LIMIT = 1024;
const proxies = new Map();

function wrap(handle) {
  if (!handle) return null;
  const existing = proxies.get(handle);
  if (existing) return existing;
  // Resolved on first use and kept: one ABI call per object, however many properties are read off it.
  let typeName = null;
  const typeOf = () => (typeName ??= binding.typeName(handle));
  // Bound methods, per handle: a method is built once and reused, rather than a fresh closure on
  // every access. Distinct from the module-level methodCache, which maps a type to its method names.
  const boundMethods = new Map();
  const proxy = new Proxy({ [HANDLE]: handle }, {
    get(_t, prop) {
      if (prop === HANDLE || prop === 'handle') return handle;
      if (typeof prop !== 'string') return undefined;
      // How the host recognises this object when it is passed as an argument to another call.
      if (prop === '__esn_handle') return handle;
      if (prop === 'then') return undefined;            // do not look like a thenable
      if (prop === 'constructor') return Object;
      if (prop === 'toString') return () => `${typeOf()}(${handle})`;
      if (prop === 'endstoneType') return typeOf();
      // The bridge cannot return an array, so the tag list arrives newline-joined.
      // Custom item data. The bridge addresses an NBT key through the property name, so these are all
      // ordinary get/set calls with a "tag:" prefix - no special ABI, and write-through means a change
      // to a stack from an inventory or a hand is saved back where it came from.
      // dropItem(location, item): assembled explicitly because it mixes a vector with an item, which
      // neither the vector nor the item flattening handles on its own.
      if (prop === 'dropItem') {
        return (location, item) => {
          const position = numbersOf(location, POSITION_KEYS);
          if (!position) throw new TypeError('dropItem(location, item): location needs numeric x, y and z');
          const [type, amount, data] = flattenItem(item);
          if (!type) throw new TypeError('dropItem(location, item): item needs a type');
          const result = binding.invoke(handle, 'dropItem', ...position, amount, data, type);
          const nested = asHandle(result);
          return nested === null ? result : wrap(nested);
        };
      }
      if (prop === 'sendForm') {
        return (spec) => {
          if (!spec || typeof spec !== 'object') throw new TypeError('sendForm(spec): spec must be an object');
          const formId = nextFormId++;
          // The player is read now, while this handle is still live, so the result can be delivered
          // with a fresh one. A form is answered on a later tick, by which point the handle that sent
          // it is long stale - and the callback wanting to reply to that same player is the whole
          // point of a form, so the runtime resolves it rather than making every author do it.
          // uniqueId rather than name: it survives a name change and cannot collide.
          let recipient = null;
          try {
            recipient = binding.get(handle, 'uniqueId') ?? null;
          } catch { /* not a player; the callbacks simply get null */ }
          openForms.set(formId, {
            kind: String(spec.type ?? 'action').toLowerCase(),
            onSubmit: spec.onSubmit, onClose: spec.onClose, plugin: activePluginId,
            recipient,
          });
          binding.sendForm(handle, formId, serialiseForm(spec));
          return formId;
        };
      }
      // A map renderer. The function stays here and only its id crosses, the same way a form or a
      // scheduled task works. Endstone owns the renderer once added, so there is no removal.
      if (prop === 'setRenderer' || prop === 'addRenderer') {
        return (draw) => {
          if (typeof draw !== 'function') {
            throw new TypeError('addRenderer(draw): draw must be a function');
          }
          const id = nextRendererId++;
          mapRenderers.set(id, { draw, plugin: activePluginId });
          binding.addMapRenderer(handle, id);
          return id;
        };
      }
      if (prop === 'closeForm') {
        return () => binding.closeForm(handle);
      }
      // Raw packet send. Separate from the generic method path because the payload is binary and the
      // argument strings are NUL-terminated, which would truncate it.
      if (prop === 'sendPacket') {
        return (packetId, payload) => binding.sendPacket(handle, Number(packetId), toByteString(payload));
      }
      // A packet payload is bytes, not text, so it goes through the binary accessor. The generic one
      // decodes as UTF-8, which replaces every byte that is not valid UTF-8 with U+FFFD.
      if (prop === 'payload') {
        return binding.getBytes(handle, 'payload');
      }
      // Permissions. The node rides the accessor name, so these are ordinary reads rather than calls
      // across the method path.
      if (prop === 'hasPermission') {
        return (node) => binding.get(handle, 'permission:' + String(node)) === true;
      }
      if (prop === 'isPermissionSet') {
        return (node) => binding.get(handle, 'permissionSet:' + String(node)) === true;
      }
      if (prop === 'getTag') {
        return (key) => binding.get(handle, 'tag:' + String(key));
      }
      if (prop === 'setTag') {
        return (key, value) => {
          if (value === undefined || value === null) {
            binding.invoke(handle, 'removeTag', String(key));
            return;
          }
          binding.set(handle, 'tag:' + String(key), value);
        };
      }
      if (prop === 'removeTag') {
        return (key) => binding.invoke(handle, 'removeTag', String(key));
      }
      if (prop === 'hasTag') {
        return (key) => binding.get(handle, 'tag:' + String(key)) !== undefined;
      }
      // Item metadata that is a list. Lore crosses newline-joined both ways, so a line containing a
      // newline is not representable - which matches the client, where each entry is its own line.
      if (prop === 'lore') {
        const joined = binding.get(handle, 'loreList');
        return typeof joined === 'string' && joined !== '' ? joined.split(NEWLINE) : [];
      }
      // A writable book's pages, newline-joined like lore. A page containing a newline is therefore
      // not representable, which matches the client - each entry is its own page.
      if (prop === 'pages') {
        const joined = binding.get(handle, 'pageList');
        return typeof joined === 'string' && joined !== '' ? joined.split(NEWLINE) : [];
      }
      // { "minecraft:sharpness": 5 }, keyed by enchantment id.
      if (prop === 'enchants') {
        const joined = binding.get(handle, 'enchantList');
        if (typeof joined !== 'string' || joined === '') return {};
        const out = {};
        for (const line of joined.split(NEWLINE)) {
          const comma = line.lastIndexOf(',');
          if (comma > 0) out[line.slice(0, comma)] = Number(line.slice(comma + 1));
        }
        return out;
      }
      if (prop === 'getEnchantLevel') {
        return (id) => Number(binding.get(handle, 'enchantLevel:' + String(id)) ?? 0);
      }
      if (prop === 'hasEnchant') {
        return (id) => binding.get(handle, 'enchant:' + String(id)) === true;
      }
      if (prop === 'hasConflictingEnchant') {
        return (id) => binding.get(handle, 'conflicts:' + String(id)) === true;
      }
      if (prop === 'tagKeys') {
        return () => {
          const joined = binding.get(handle, 'tagKeyList');
          return typeof joined === 'string' && joined !== '' ? joined.split(NEWLINE) : [];
        };
      }
      // Whether two stacks would stack together: type, data and NBT, ignoring the amount. The other
      // stack rides the accessor name because a method call cannot return a boolean.
      if (prop === 'isSimilar') {
        return (other) => {
          const against = asHandle(other);
          if (against === null) {
            throw new TypeError('isSimilar(other): other must be an item stack from the API');
          }
          return binding.get(handle, 'similarTo:' + against) === true;
        };
      }
      // What a scoreboard already holds. Everything else about a scoreboard is keyed by objective
      // name, so these three are the only way to discover those names in the first place.
      if (prop === 'objectives') {
        const joined = binding.get(handle, 'objectiveList');
        if (typeof joined !== 'string' || joined === '') return [];
        return joined.split(NEWLINE).map((line) => {
          const [name, displayName, modifiable, displaySlot, sortOrder] = line.split(UNIT_SEPARATOR);
          return {
            name, displayName, modifiable: modifiable === '1',
            displaySlot: displaySlot || null,
            sortOrder: sortOrder || null,
          };
        });
      }
      if (prop === 'entries') {
        const joined = binding.get(handle, 'entryList');
        return typeof joined === 'string' && joined !== '' ? joined.split(NEWLINE) : [];
      }
      if (prop === 'getScores') {
        return (entry) => {
          const joined = binding.get(handle, 'scores:' + String(entry));
          if (typeof joined !== 'string' || joined === '') return {};
          const out = {};
          for (const line of joined.split(NEWLINE)) {
            const [objective, value] = line.split(UNIT_SEPARATOR);
            out[objective] = Number(value);
          }
          return out;
        };
      }
      // A block's states, e.g. { upside_down_bit: false, facing_direction: 3 }. The type travels with
      // each value because BlockStates is a variant - otherwise "true" the string and true the boolean
      // would be indistinguishable by the time they got here.
      if (prop === 'blockStates') {
        return readBlockStates(binding.get(handle, 'blockStatesList'));
      }
      // A boss bar's viewers, as names: handing back Player objects would tie them to a dispatch scope
      // that has long since ended by the time a timer updates the bar.
      // Who will see a chat message, as names. getRecipients hands back a copy on the C++ side, so
      // this is an observation - filtering it does not change who receives the message.
      if (prop === 'recipients') {
        const joined = binding.get(handle, 'recipientNameList');
        return typeof joined === 'string' && joined !== '' ? joined.split(NEWLINE) : [];
      }
      if (prop === 'playerNames') {
        const joined = binding.get(handle, 'playerNameList');
        return typeof joined === 'string' && joined !== '' ? joined.split(NEWLINE) : [];
      }
      if (prop === 'scoreboardTags') {
        const joined = binding.get(handle, 'scoreboardTagList');
        return typeof joined === 'string' && joined !== '' ? joined.split(NEWLINE) : [];
      }
      // A height without allocating a Block for it. Its arguments ride the accessor name, so it is a
      // method here rather than a property.
      if (prop === 'getHighestBlockYAt') {
        return (position) => {
          const spread = numbersOf(position, ['x', 'z']);
          if (!spread) throw new TypeError('getHighestBlockYAt(position): needs numeric x and z');
          return Number(binding.get(handle, `highestBlockYAt:${Math.round(spread[0])},${Math.round(spread[1])}`));
        };
      }
      // Skin and cape sizes. The pixels stay on the C++ side - a plugin cannot render an Image - so only
      // the dimensions cross; cape width 0 means no cape.
      if (prop === 'skin') {
        return {
          id: binding.get(handle, 'skinId'),
          capeId: binding.get(handle, 'capeId'),
          width: Number(binding.get(handle, 'skinWidth') ?? 0),
          height: Number(binding.get(handle, 'skinHeight') ?? 0),
          capeWidth: Number(binding.get(handle, 'skinCapeWidth') ?? 0),
          capeHeight: Number(binding.get(handle, 'skinCapeHeight') ?? 0),
        };
      }
      // The bridge emits "x,z" per line; turn it into the objects the types promise.
      if (prop === 'loadedChunks') {
        const joined = binding.get(handle, 'loadedChunkList');
        if (typeof joined !== 'string' || joined === '') return [];
        return joined.split(NEWLINE).map((line) => {
          const [x, z] = line.split(',');
          return { x: Number(x), z: Number(z) };
        });
      }
      // teleport is the one method whose second argument is an options object rather than a value,
      // so it is assembled here instead of going through flatten(). Rotation and dimension are both
      // left out when absent, and the host then keeps the actor's current ones.
      if (prop === 'teleport') {
        return (location, options = {}) => {
          const position = numbersOf(location, POSITION_KEYS);
          if (!position) {
            throw new TypeError('teleport(location): location needs numeric x, y and z');
          }
          const rotation = numbersOf(options && options.rotation, ROTATION_KEYS);
          const args = rotation ? [...position, ...rotation] : [...position];
          const dimension = options && options.dimension;
          if (dimension !== undefined && dimension !== null) args.push(String(dimension));
          binding.invoke(handle, 'teleport', ...args);
        };
      }
      // Inventory helpers are plain JavaScript over size/getItem, bound to this proxy.
      if (INVENTORY_HELPERS[prop] && typeOf().endsWith('Inventory')) {
        return INVENTORY_HELPERS[prop].bind(proxy);
      }
      // A method, but only if this type actually has it - otherwise it falls through to the property
      // path, and an unknown name ends up reported as a missing member rather than as a function that
      // throws when called.
      if (methodsFor(typeOf()).has(prop)) {
        let cached = boundMethods.get(prop);
        if (cached) return cached;
        // Item arguments describe a stack; slot indices stay as plain numbers before them.
        if (ITEM_METHODS.has(prop)) {
          cached = (...args) => {
            const flat = [];
            for (const arg of args) {
              if (typeof arg === 'number') flat.push(arg);
              else flat.push(...flattenItem(arg));
            }
            const result = binding.invoke(handle, prop, ...flat);
            const nested = asHandle(result);
            return nested === null ? result : wrap(nested);
          };
        } else {
          // Everything else passes through as-is except vector/rotation objects, which flatten into the
          // positional numbers the host reads; the host sorts strings, numbers and handles into arrays.
          cached = (...args) => {
            const result = binding.invoke(handle, prop, ...flatten(args));
            const nested = asHandle(result);
            return nested === null ? result : wrap(nested);
          };
        }
        boundMethods.set(prop, cached);
        return cached;
      }
      const value = readMember(handle, typeOf(), prop);
      const nested = asHandle(value);
      return nested === null ? value : wrap(nested);
    },
    set(_t, prop, value) {
      if (typeof prop !== 'string') return false;
      // Bytes, as above - and a Uint8Array is accepted so a decoded payload can be rebuilt and put back.
      if (prop === 'payload') {
        binding.setBytes(handle, 'payload', toByteString(value));
        return true;
      }
      // Lore is an array on this side and one newline-joined string on the other; null clears it.
      if (prop === 'lore') {
        const lines = value === null || value === undefined ? [] : value;
        if (!Array.isArray(lines)) throw new TypeError('lore must be an array of strings, or null');
        binding.set(handle, 'loreList', lines.map(String).join(NEWLINE));
        return true;
      }
      if (prop === 'pages') {
        const pages = value === null || value === undefined ? [] : value;
        if (!Array.isArray(pages)) throw new TypeError('pages must be an array of strings, or null');
        binding.set(handle, 'pageList', pages.map(String).join(NEWLINE));
        return true;
      }
      // A handle-valued write: the ABI's setters carry only scalars, so it goes through a method, the
      // same trick `rotation` uses for a two-number field.
      if (prop === 'scoreboard') {
        if (asHandle(value) === null) {
          throw new TypeError('scoreboard must be a scoreboard from server.createScoreboard()');
        }
        binding.invoke(handle, 'setScoreboard', value);
      } else if (prop === 'rotation') {
        // The `rotation` field is two numbers behind one object; route it through the same
        // dispatch as a method so the host reads yaw, pitch.
        binding.invoke(handle, 'setRotation', ...flattenRotation(value));
      } else if (prop === 'pixels') {
        // A whole 128x128 RGBA frame in one crossing. Setting pixels one at a time would cross the
        // ABI 16384 times per draw, per viewer.
        binding.setBytes(handle, 'pixels', toByteString(value));
      } else if (prop === 'blockStates') {
        // Only the states named are changed; the rest of the block's palette entry is kept, so
        // turning a stair round does not reset whether it is upside down.
        binding.set(handle, 'blockStatesList', writeBlockStates(value));
      } else if (prop === 'from' || prop === 'to') {
        // A Location is six numbers plus a dimension, so it routes through a method like `rotation`.
        // Facing and dimension are optional and default to whatever the event already had.
        const position = numbersOf(value, POSITION_KEYS);
        if (!position) throw new TypeError(`${prop} must have numeric x, y and z`);
        const rotation = numbersOf(value, ROTATION_KEYS);
        const args = rotation ? [...position, ...rotation] : [...position];
        const dimension = readDimension(value);
        if (dimension) args.push(dimension);
        binding.invoke(handle, prop === 'from' ? 'setFrom' : 'setTo', ...args);
      } else if (prop === 'knockback') {
        // Same trick for a vector-valued field: the ABI's setters only carry scalars.
        const vector = numbersOf(value, POSITION_KEYS);
        if (!vector) throw new TypeError('knockback must have numeric x, y and z');
        binding.invoke(handle, 'setKnockback', ...vector);
      } else {
        binding.set(handle, prop, value);
      }
      return true;
    },
    has(_t, prop) { return typeof prop === 'string'; },
  });
  // Cleared wholesale rather than evicted one at a time: entries are equally worthless once the
  // dispatch that made them ends, and rebuilding a proxy costs one type lookup.
  if (proxies.size >= PROXY_LIMIT) proxies.clear();
  proxies.set(handle, proxy);
  return proxy;
}

// Anything not defined above is read off the Server itself through the generic accessors, so a new
// server property costs a case in the bridge and nothing here. The handle is persistent - the server
// outlives every callback - so it is fetched once.
const serverHandle = binding.apiAvailable() ? binding.serverSelf() : 0;
const server = new Proxy(serverBase, {
  get(target, prop) {
    if (prop in target) return target[prop];
    if (!serverHandle || typeof prop !== 'string') return undefined;
    // Methods as well as properties: reading a method name has to hand back something callable, or
    // server.dispatchCommand(...) would be `undefined is not a function`. The type is known statically
    // here - this proxy only ever wraps the Server.
    if (methodsFor('Server').has(prop)) {
      return (...args) => {
        const result = binding.invoke(serverHandle, prop, ...flatten(args));
        const nested = asHandle(result);
        return nested === null ? result : wrap(nested);
      };
    }
    const value = readMember(serverHandle, 'Server', prop);
    const nested = asHandle(value);
    return nested === null ? value : wrap(nested);
  },
  has(target, prop) { return prop in target || typeof prop === 'string'; },
});

// --- events ---------------------------------------------------------------------------------
// Named after Endstone's event classes minus the "Event" suffix, camelCased, so the mapping back to
// the Endstone documentation is mechanical.
const PRIORITIES = { lowest: 0, low: 1, normal: 2, high: 3, highest: 4, monitor: 5 };
// Subscriptions carry the plugin that made them so a reload can drop a plugin's handlers wholesale.
// A null plugin tag belongs to the runtime itself and survives reloads.
const handlers = new Map();
let activePluginId = null;

function dispatchEvent(subscription, handle) {
  const entry = handlers.get(subscription);
  if (!entry) return;
  const event = wrap(handle);
  for (const { handler } of entry) {
    try {
      handler(event);
    } catch (err) {
      binding.log(4, `event handler threw: ${(err && err.stack) || err}`);
    }
  }
}

// Every Endstone event. Subscription is generic - the name is passed straight through to Endstone's
// PluginManager - so this list is about discoverability and typo-catching, not capability.
const EVENT_NAMES = [
  // Player
  'playerJoin', 'playerQuit', 'playerChat', 'playerCommand', 'playerDeath', 'playerLogin',
  'playerKick', 'playerInteract', 'playerInteractActor', 'playerMove', 'playerJump',
  'playerTeleport', 'playerPortal', 'playerRespawn', 'playerGameModeChange', 'playerItemConsume',
  'playerItemHeld', 'playerPickupItem', 'playerDropItem', 'playerBedEnter', 'playerBedLeave',
  'playerDimensionChange', 'playerEmote', 'playerSkinChange',
  // Actor
  'actorDamage', 'actorDeath', 'actorExplode', 'actorKnockback', 'actorRemove', 'actorSpawn',
  'actorTeleport',
  // Block
  'blockBreak', 'blockPlace', 'blockCook', 'blockExplode', 'blockForm', 'blockFromTo', 'blockGrow',
  'blockPistonExtend', 'blockPistonRetract', 'leavesDecay',
  // Server
  'serverLoad', 'serverCommand', 'serverListPing', 'broadcastMessage', 'scriptMessage',
  'packetReceive', 'packetSend', 'pluginEnable', 'pluginDisable', 'mapInitialize',
  // World
  'chunkLoad', 'chunkUnload', 'thunderChange', 'weatherChange',
];

const toEndstoneName = (camel) => camel.charAt(0).toUpperCase() + camel.slice(1) + 'Event';

const events = {};
for (const name of EVENT_NAMES) {
  events[name] = (handler, options = {}) => {
    if (typeof handler !== 'function') throw new TypeError(`${name}: handler must be a function`);
    const priority = PRIORITIES[String(options.priority ?? 'normal').toLowerCase()] ?? PRIORITIES.normal;
    const subscription = binding.subscribe(toEndstoneName(name), priority, options.ignoreCancelled === true);
    if (!handlers.has(subscription)) handlers.set(subscription, []);
    const record = { handler, plugin: activePluginId };
    handlers.get(subscription).push(record);
    return {
      unsubscribe() {
        const list = handlers.get(subscription);
        if (!list) return;
        for (let i = list.length - 1; i >= 0; --i) {
          if (list[i] === record) list.splice(i, 1);
        }
        if (list.length === 0) {
          handlers.delete(subscription);
          binding.unsubscribe(subscription);
        }
      },
    };
  };
}

// --- commands -----------------------------------------------------------------------------------
// Commands are registered in code rather than declared in a manifest, so a plugin can add or drop one
// at any time and a reload picks up the change. Every command is a slash command, reached as `/name`
// from a player or the console. Registering a name the server already uses shadows it, since the
// command line is intercepted and cancelled before it reaches the server.
const commandsByName = new Map();
let commandRouters = null;
// Non-null only while a plugin's module is being evaluated. Commands registered into it are declared
// to Endstone, which is what gets them into Bedrock's registry - and therefore into the client's
// command list, with autocomplete and validated arguments. Anything registered later can only be
// intercepted.
let collectingDeclarations = null;
// True until the bootstrap has finished. The runtime's own commands are registered during it and can
// never be declared - there is no plugin module for them to be declared in - so they are exempt from
// the "registered too late" warning, which would otherwise give advice that cannot be followed.
let bootstrapping = true;

// Console commands need a sender too. `isConsole` tells a handler which it has - a wrapped Player
// returns undefined for it, since unknown members read as undefined.
const consoleSender = {
  name: 'Console',
  isConsole: true,
  isOp: true,
  // The console runs at Endstone's Console permission level, which is above operator: it holds
  // every node, so a declared permission must never gate it.
  permissionLevel: 'console',
  hasPermission: () => true,
  isPermissionSet: () => true,
  sendMessage(text) { binding.log(2, String(text).replace(/§./g, '')); },
  sendErrorMessage(text) { binding.log(4, String(text).replace(/§./g, '')); },
};

const splitArgs = (rest) => {
  const trimmed = String(rest ?? '').trim();
  return trimmed === '' ? [] : trimmed.split(/\s+/);
};

const lower = (value) => String(value).toLowerCase();

/** Matches a bare command word against registered names and aliases. */
function findCommand(token) {
  const key = lower(token);
  const direct = commandsByName.get(key);
  if (direct) return direct;
  for (const command of commandsByName.values()) {
    if (command.aliases.includes(key)) return command;
  }
  return null;
}

function runCommand(command, sender, args, raw) {
  if (command.op && sender.isOp !== true) {
    // Logged as well as replied to: a refusal that only appears in the player's chat is impossible
    // to tell apart from a command that never fired at all.
    binding.log(2, `refused '/${command.name}' for ${sender.name}: operators only`);
    sender.sendMessage('§cYou do not have permission to use that command.');
    return;
  }
  // A declared permission is checked here as well as being handed to Endstone, so a command
  // registered too late to reach the registry is still gated rather than open to everyone.
  for (const node of command.permissions) {
    if (typeof sender.hasPermission === 'function' && sender.hasPermission(node)) continue;
    binding.log(2, `refused '/${command.name}' for ${sender.name}: missing ${node}`);
    sender.sendMessage('§cYou do not have permission to use that command.');
    return;
  }
  try {
    command.handler(sender, args, raw);
  } catch (err) {
    binding.log(4, `command '${command.name}' threw: ${(err && err.stack) || err}`);
    sender.sendMessage('§cThat command failed. See the server log.');
  }
}

function ensureCommandRouters() {
  if (commandRouters || !binding.apiAvailable()) return;
  // The routers belong to the runtime, not to whichever plugin happened to register first, so the
  // attribution is cleared - otherwise reloading that plugin would drop command handling entirely.
  const previous = activePluginId;
  activePluginId = null;
  try {
    const route = (event, sender) => {
      const line = String(event.command ?? '').trim().replace(/^\//, '');
      const match = /^(\S+)([\s\S]*)$/.exec(line);
      if (!match) return;
      const command = findCommand(match[1]);
      if (!command || command.declared) return;
      event.cancelled = true;
      runCommand(command, sender, splitArgs(match[2]), line);
    };
    commandRouters = [
      // Lowest priority so a plugin watching playerCommand still sees the line first.
      // Declared commands are dispatched by Endstone through onCommand, so the router must not
      // cancel them here - it would swallow the command before Endstone ever ran it.
      events.playerCommand((event) => route(event, event.player), { priority: 'lowest' }),
      events.serverCommand((event) => route(event, consoleSender), { priority: 'lowest' }),
    ];
  }
  finally {
    activePluginId = previous;
  }
}

const commands = {
  register(name, handler, options = {}) {
    if (typeof name !== 'string' || name.trim() === '') {
      throw new TypeError('commands.register: name must be a non-empty string');
    }
    if (typeof handler !== 'function') {
      throw new TypeError(`commands.register('${name}'): handler must be a function`);
    }
    const key = lower(name.trim());
    if (commandsByName.has(key)) {
      throw new Error(`commands.register: '${key}' is already registered`);
    }
    // `usages` is the real thing - Endstone parses each one to build the client's argument list.
    // `usage` stays accepted as the singular shorthand. An empty list means a bare "/name".
    const usages = options.usages ? options.usages.map(String)
      : options.usage ? [String(options.usage)]
      : [];
    const record = {
      name: key,
      handler,
      description: String(options.description ?? ''),
      usages,
      aliases: (options.aliases ?? []).map(lower),
      op: options.op === true,
      permissions: (options.permissions ?? []).map(String),
      plugin: activePluginId,
      declared: false,
    };

    if (collectingDeclarations) {
      record.declared = true;
      record.plugin = collectingDeclarations.pluginId;
      collectingDeclarations.records.push(record);
    }
    else if (!bootstrapping) {
      // Registered after the module finished loading, so it missed the description Endstone builds at
      // load time. It still runs, via interception - it just cannot appear in the client's list.
      binding.log(3,
        `command '/${key}' was registered after loading, so it will not appear in the client's ` +
        `command list or autocomplete. Move the commands.register call to the top level of your ` +
        `plugin module to have it registered properly.`);
    }

    commandsByName.set(key, record);
    ensureCommandRouters();
    return {
      unregister() {
        if (commandsByName.get(key) === record) commandsByName.delete(key);
      },
    };
  },

  list() {
    return [...commandsByName.values()].map((command) => ({
      name: command.name,
      description: command.description,
      usages: [...command.usages],
      aliases: [...command.aliases],
      op: command.op,
      permissions: [...command.permissions],
      declared: command.declared,
    }));
  },
};

/**
 * Drops the commands a plugin registered, so a reload does not leave duplicates behind.
 *
 * Returns the names that were declared to Endstone. Those stay registered on the Endstone side for the
 * life of the server, so a reload has to re-declare them rather than treat them as new.
 */
function dropCommands(pluginId) {
  const wasDeclared = new Set();
  for (const [key, record] of commandsByName) {
    if (record.plugin !== pluginId) continue;
    if (record.declared) wasDeclared.add(key);
    commandsByName.delete(key);
  }
  return wasDeclared;
}

/**
 * Runs a declared command, called from C++ when Endstone dispatches one.
 *
 * Returns false when nothing handled it, which makes Endstone show the command's usage.
 */
function runDeclaredCommand(pluginId, name, senderHandle, args) {
  const command = commandsByName.get(lower(name)) ?? findCommand(name);
  if (!command) return false;
  const sender = wrap(senderHandle) ?? consoleSender;
  runCommand(command, sender, args, [name, ...args].join(' '));
  return true;
}

// --- scheduler -----------------------------------------------------------------------------------
// Endstone's own scheduler, measured in server ticks and run on the server thread. Distinct from
// setTimeout/setInterval, which are Node's and are driven by the event loop: a task scheduled here
// runs in step with the world, so it can touch the API safely and its timing is tied to the tick rate
// rather than to wall-clock milliseconds.
const scheduledTasks = new Map();

// Map renderers, by the id the bridge knows them by.
const mapRenderers = new Map();
let nextRendererId = 1;

/** Draws one map for one viewer. Called from C++ on the thread that sends the map packet. */
function renderMap(renderer, canvasHandle, playerHandle) {
  const entry = mapRenderers.get(renderer);
  if (!entry) return;
  try {
    entry.draw(wrap(canvasHandle), wrap(playerHandle));
  } catch (err) {
    binding.log(4, `map renderer threw: ${(err && err.stack) || err}`);
  }
}

/** Drops a plugin's renderers. Endstone still owns them, so this only stops ours from running. */
function dropRenderers(pluginId) {
  for (const [id, entry] of mapRenderers) {
    if (entry.plugin === pluginId) mapRenderers.delete(id);
  }
}

function runTask(task) {
  const entry = scheduledTasks.get(task);
  if (!entry) return;
  if (entry.once) scheduledTasks.delete(task);
  try {
    entry.handler();
  } catch (err) {
    const msg = (err && err.stack) || err;
    binding.log(4, `scheduled task threw: ${msg}`);
    if (msg && typeof msg === 'string' && msg.includes('no longer valid')) {
      binding.log(3,
        `Hint: scheduled tasks fire on a later tick. You cannot use a 'sender' or 'player' handle ` +
        `captured when the task was created. Instead, capture the player's name (a string) and use ` +
        `server.getPlayer(name) inside the task to get a fresh handle.`);
    }
  }
}

const asTicks = (value, fallback) => {
  const ticks = Number(value);
  return Number.isFinite(ticks) && ticks >= 0 ? Math.floor(ticks) : fallback;
};

function schedule(handler, delay, period, plugin) {
  if (typeof handler !== 'function') {
    throw new TypeError('scheduler: handler must be a function');
  }
  const task = binding.scheduleTask(delay, period);
  if (typeof task !== 'number') {
    throw new Error('scheduler: the server refused to schedule the task');
  }
  scheduledTasks.set(task, { handler, once: period === 0, plugin });
  return {
    id: task,
    cancel() {
      if (!scheduledTasks.has(task)) return;
      scheduledTasks.delete(task);
      binding.cancelTask(task);
    },
  };
}

/** Drops a plugin's scheduled tasks, so a reload does not leave the old ones running. */
function dropTasks(pluginId) {
  for (const [task, entry] of scheduledTasks) {
    if (entry.plugin === pluginId) {
      scheduledTasks.delete(task);
      binding.cancelTask(task);
    }
  }
}

const scheduler = {
  runTimer(handler, period = 20, delay = 0) {
    return schedule(handler, asTicks(delay, 0), Math.max(1, asTicks(period, 20)), activePluginId);
  },
  runLater(handler, delay = 1) {
    return schedule(handler, asTicks(delay, 1), 0, activePluginId);
  },
  runNextTick(handler) {
    return schedule(handler, 0, 0, activePluginId);
  },
};

// --- forms ---------------------------------------------------------------------------------------
// A form is described by a plain object and serialised into the record format the bridge parses. The
// two separators are control bytes that never appear in form text, so nothing needs escaping.
const RECORD_SEP = String.fromCharCode(0x1e);
const FIELD_SEP = String.fromCharCode(0x1f);
const openForms = new Map();
let nextFormId = 1;

const formField = (value) => String(value ?? '').replace(/[]/g, ' ');

function serialiseForm(spec) {
  const kind = String(spec.type ?? 'action').toLowerCase();
  const records = [];
  if (kind === 'message') {
    records.push(['message', formField(spec.title), formField(spec.content),
                  formField(spec.button1 ?? 'Yes'), formField(spec.button2 ?? 'No')]);
  } else if (kind === 'modal') {
    records.push(['modal', formField(spec.title), '', formField(spec.submitButton ?? '')]);
    for (const control of spec.controls ?? []) {
      const type = String(control.type ?? '').toLowerCase();
      if (type === 'toggle') {
        records.push(['toggle', formField(control.label), control.defaultValue ? '1' : '0']);
      } else if (type === 'slider') {
        records.push(['slider', formField(control.label), String(control.min ?? 0), String(control.max ?? 10),
                      String(control.step ?? 1),
                      control.defaultValue === undefined ? '' : String(control.defaultValue)]);
      } else if (type === 'dropdown' || type === 'stepslider') {
        records.push([type, formField(control.label),
                      control.defaultIndex === undefined ? '' : String(control.defaultIndex),
                      ...(control.options ?? []).map(formField)]);
      } else if (type === 'textinput') {
        records.push(['textinput', formField(control.label), formField(control.placeholder ?? ''),
                      control.defaultValue === undefined ? '' : formField(control.defaultValue)]);
      } else if (type === 'header' || type === 'label') {
        records.push([type, formField(control.text ?? control.label)]);
      } else if (type === 'divider') {
        records.push(['divider']);
      }
    }
  } else {
    records.push(['action', formField(spec.title), formField(spec.content)]);
    for (const button of spec.buttons ?? []) {
      if (typeof button === 'string') { records.push(['button', formField(button), '']); continue; }
      const type = String(button.type ?? 'button').toLowerCase();
      if (type === 'divider') records.push(['divider']);
      else if (type === 'header' || type === 'label') records.push([type, formField(button.text)]);
      else records.push(['button', formField(button.text), formField(button.icon ?? '')]);
    }
  }
  return records.map((fields) => fields.join(FIELD_SEP)).join(RECORD_SEP);
}

/** Called from C++ when a player submits or dismisses a form. */
function formResult(formId, closed, data) {
  const entry = openForms.get(formId);
  if (!entry) return;
  openForms.delete(formId);
  // Resolved fresh on this tick. Null when they have since disconnected, which a handler should check
  // before replying - the form outlived them.
  const player = entry.recipient ? server.getPlayer(entry.recipient) : null;
  try {
    if (closed) {
      if (entry.onClose) entry.onClose(player);
      return;
    }
    if (!entry.onSubmit) return;
    if (entry.kind === 'modal') {
      // A modal form answers with a JSON array, one entry per control that takes a value.
      let parsed = data;
      try { parsed = JSON.parse(data); } catch { /* hand back the raw string if it is not JSON */ }
      entry.onSubmit(parsed, player);
    } else {
      entry.onSubmit(Number(data), player);
    }
  } catch (err) {
    const msg = (err && err.stack) || err;
    binding.log(4, `form handler threw: ${msg}`);
    if (msg && typeof msg === 'string' && msg.includes('no longer valid')) {
      binding.log(3,
        `Hint: form callbacks fire on a later tick, so the handle that sent the form is stale by then. ` +
        `The callback is passed a fresh player for exactly this - onSubmit(data, player) and ` +
        `onClose(player) - so use that instead of the one you captured.`);
    }
  }
}

/** Drops a plugin's pending form callbacks, so a reload does not fire stale handlers. */
function dropForms(pluginId) {
  for (const [id, entry] of openForms) {
    if (entry.plugin === pluginId) openForms.delete(id);
  }
}

// --- packet decoding -----------------------------------------------------------------------------
// Payloads are decoded against the schema Endstone's protocol-dumper extracted from BDS itself
// (node/protocol/protocol.json, generated by node/scripts/generate_protocol.py). Only the ~19 wire
// primitives are implemented here; every composite - Vec3, ActorRuntimeID, ItemStack descriptors -
// is defined in the schema in terms of those, so it composes rather than being hand-written.
//
// Signed varints are zigzag-encoded: that is Bedrock's convention, and the schema's separate
// `zigzag32` spelling is decoded identically. Getting this wrong would flip the sign of negative
// coordinates rather than fail loudly, so it is called out here.

class BinaryReader {
  constructor(bytes) {
    this.bytes = bytes;
    this.view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    this.offset = 0;
  }

  get remaining() { return this.bytes.length - this.offset; }

  need(count) {
    if (this.offset + count > this.bytes.length) {
      throw new RangeError(`payload ended early: wanted ${count} byte(s) at ${this.offset} of ${this.bytes.length}`);
    }
  }

  bool() { return this.uint8() !== 0; }
  uint8() { this.need(1); return this.bytes[this.offset++]; }
  int8() { this.need(1); return this.view.getInt8(this.offset++); }
  uint16() { this.need(2); const v = this.view.getUint16(this.offset, true); this.offset += 2; return v; }
  int16() { this.need(2); const v = this.view.getInt16(this.offset, true); this.offset += 2; return v; }
  uint32() { this.need(4); const v = this.view.getUint32(this.offset, true); this.offset += 4; return v; }
  int32() { this.need(4); const v = this.view.getInt32(this.offset, true); this.offset += 4; return v; }
  int32be() { this.need(4); const v = this.view.getInt32(this.offset, false); this.offset += 4; return v; }
  uint64() { this.need(8); const v = this.view.getBigUint64(this.offset, true); this.offset += 8; return v; }
  int64() { this.need(8); const v = this.view.getBigInt64(this.offset, true); this.offset += 8; return v; }
  float() { this.need(4); const v = this.view.getFloat32(this.offset, true); this.offset += 4; return v; }
  double() { this.need(8); const v = this.view.getFloat64(this.offset, true); this.offset += 8; return v; }

  /** Unsigned LEB128, capped at 5 bytes for a 32-bit value. */
  uvarint32() {
    let result = 0;
    for (let shift = 0; shift < 35; shift += 7) {
      const byte = this.uint8();
      result |= (byte & 0x7f) << shift;
      if ((byte & 0x80) === 0) return result >>> 0;
    }
    throw new RangeError('uvarint32 is longer than 5 bytes');
  }

  uvarint64() {
    let result = 0n;
    for (let shift = 0n; shift < 70n; shift += 7n) {
      const byte = BigInt(this.uint8());
      result |= (byte & 0x7fn) << shift;
      if ((byte & 0x80n) === 0n) return result;
    }
    throw new RangeError('uvarint64 is longer than 10 bytes');
  }

  // Zigzag maps signed values onto unsigned so small negatives stay small on the wire.
  varint32() { const raw = this.uvarint32(); return (raw >>> 1) ^ -(raw & 1); }
  varint64() { const raw = this.uvarint64(); return (raw >> 1n) ^ -(raw & 1n); }

  string() {
    const length = this.uvarint32();
    this.need(length);
    const slice = this.bytes.subarray(this.offset, this.offset + length);
    this.offset += length;
    return UTF8_DECODER.decode(slice);
  }
}

const UTF8_DECODER = new TextDecoder('utf-8');

const PRIMITIVE_READERS = {
  bool: (r) => r.bool(),
  int8: (r) => r.int8(), uint8: (r) => r.uint8(),
  int16: (r) => r.int16(), uint16: (r) => r.uint16(),
  int32: (r) => r.int32(), uint32: (r) => r.uint32(), int32_be: (r) => r.int32be(),
  int64: (r) => r.int64(), uint64: (r) => r.uint64(),
  float: (r) => r.float(), float32: (r) => r.float(),
  double: (r) => r.double(), float64: (r) => r.double(),
  varint32: (r) => r.varint32(), uvarint32: (r) => r.uvarint32(),
  varint64: (r) => r.varint64(), uvarint64: (r) => r.uvarint64(),
  zigzag32: (r) => r.varint32(), zigzag64: (r) => r.varint64(),
  string: (r) => r.string(),
};

// Raised when the schema describes something this decoder cannot follow. Carries the reason so the
// caller learns why decoding stopped rather than being handed values read from the wrong offset.
class Undecodable extends Error {
  constructor(reason, where) {
    super(`${reason}${where ? ` at '${where}'` : ''}`);
    this.reason = reason;
    this.where = where;
  }
}

let protocolSchema = null;

function loadProtocolSchema() {
  if (protocolSchema) return protocolSchema;
  const beside = binding.scriptPath ? nodePath.join(nodePath.dirname(binding.scriptPath), 'protocol.json') : null;
  if (!beside || !nodeFs.existsSync(beside)) {
    throw new Error(
      'packet decoding needs protocol.json next to the Node host. Generate it with ' +
      '"python node/scripts/generate_protocol.py" and stage it into the plugin data folder.');
  }
  protocolSchema = JSON.parse(nodeFs.readFileSync(beside, 'utf8'));
  return protocolSchema;
}

function readField(reader, field, schema, depth) {
  const type = field.t;
  if (depth > 24) throw new Undecodable('schema nests too deeply', field.n);
  if (typeof type !== 'string') {
    // {switch,cases} unions, {key,value} maps and nested repeats need a discriminant rule the schema
    // does not carry, so following them would be guesswork.
    throw new Undecodable(
      type && type.key ? 'map fields are not described well enough to decode'
        : type && type.switch ? 'tagged unions are not described well enough to decode'
        : 'this field shape is not supported', field.n);
  }
  if (PRIMITIVE_READERS[type]) return PRIMITIVE_READERS[type](reader);
  if (type === 'CompoundTag' || type === 'NBT') throw new Undecodable('NBT fields are not decoded yet', field.n);

  const composite = schema.types[type.replace(/::/g, '__')];
  if (!composite) throw new Undecodable(`unknown wire type '${type}'`, field.n);
  return readFields(reader, composite, schema, depth + 1);
}

function readFields(reader, fields, schema, depth) {
  const out = {};
  for (const field of fields) {
    // The schema records that a field may be absent but not how presence is signalled - for most of
    // them it depends on a flag field it does not model - so an optional field is where decoding
    // honestly has to stop.
    if (field.o) throw new Undecodable('this packet has an optional field, whose presence the schema does not describe', field.n);

    let value;
    if (field.r) {
      const count = PRIMITIVE_READERS[field.r] ? PRIMITIVE_READERS[field.r](reader) : reader.uvarint32();
      const items = [];
      for (let i = 0; i < Number(count); ++i) items.push(readField(reader, field, schema, depth));
      value = items;
    } else {
      value = readField(reader, field, schema, depth);
    }
    // A field with a fixed value is wire padding: read past it, do not report it.
    if (field.n !== undefined && field.c === undefined) out[field.n] = value;
  }
  return out;
}

// Encoding is the mirror of decoding and inherits the same schema limits: a packet whose layout the
// schema cannot fully describe cannot be built from an object either, and encode() says so rather than
// emitting a payload the client would reject.
class BinaryWriter {
  constructor() {
    this.bytes = [];
  }

  get length() { return this.bytes.length; }
  toBytes() { return new Uint8Array(this.bytes); }

  raw(values) { for (const v of values) this.bytes.push(v & 0xff); return this; }
  bool(value) { this.bytes.push(value ? 1 : 0); return this; }
  uint8(value) { this.bytes.push(Number(value) & 0xff); return this; }
  int8(value) { return this.uint8(value); }

  fixed(value, size, signed, littleEndian = true) {
    const buffer = new ArrayBuffer(size);
    const view = new DataView(buffer);
    if (size === 2) signed ? view.setInt16(0, Number(value), littleEndian) : view.setUint16(0, Number(value), littleEndian);
    else if (size === 4) signed ? view.setInt32(0, Number(value), littleEndian) : view.setUint32(0, Number(value), littleEndian);
    else signed ? view.setBigInt64(0, BigInt(value), littleEndian) : view.setBigUint64(0, BigInt(value), littleEndian);
    return this.raw(new Uint8Array(buffer));
  }

  int16(v) { return this.fixed(v, 2, true); }
  uint16(v) { return this.fixed(v, 2, false); }
  int32(v) { return this.fixed(v, 4, true); }
  uint32(v) { return this.fixed(v, 4, false); }
  int32be(v) { return this.fixed(v, 4, true, false); }
  int64(v) { return this.fixed(v, 8, true); }
  uint64(v) { return this.fixed(v, 8, false); }

  float(value) {
    const buffer = new ArrayBuffer(4);
    new DataView(buffer).setFloat32(0, Number(value), true);
    return this.raw(new Uint8Array(buffer));
  }

  double(value) {
    const buffer = new ArrayBuffer(8);
    new DataView(buffer).setFloat64(0, Number(value), true);
    return this.raw(new Uint8Array(buffer));
  }

  uvarint32(value) {
    let v = Number(value) >>> 0;
    do {
      const byte = v & 0x7f;
      v >>>= 7;
      this.bytes.push(v ? byte | 0x80 : byte);
    } while (v);
    return this;
  }

  uvarint64(value) {
    let v = BigInt(value) & 0xffffffffffffffffn;
    do {
      const byte = Number(v & 0x7fn);
      v >>= 7n;
      this.bytes.push(v ? byte | 0x80 : byte);
    } while (v);
    return this;
  }

  varint32(value) { const v = Number(value) | 0; return this.uvarint32(((v << 1) ^ (v >> 31)) >>> 0); }
  varint64(value) { const v = BigInt(value); return this.uvarint64((v << 1n) ^ (v >> 63n)); }

  string(value) {
    const encoded = UTF8_ENCODER.encode(String(value ?? ''));
    this.uvarint32(encoded.length);
    return this.raw(encoded);
  }
}

const UTF8_ENCODER = new TextEncoder();

const PRIMITIVE_WRITERS = {
  bool: (w, v) => w.bool(v),
  int8: (w, v) => w.int8(v), uint8: (w, v) => w.uint8(v),
  int16: (w, v) => w.int16(v), uint16: (w, v) => w.uint16(v),
  int32: (w, v) => w.int32(v), uint32: (w, v) => w.uint32(v), int32_be: (w, v) => w.int32be(v),
  int64: (w, v) => w.int64(v), uint64: (w, v) => w.uint64(v),
  float: (w, v) => w.float(v), float32: (w, v) => w.float(v),
  double: (w, v) => w.double(v), float64: (w, v) => w.double(v),
  varint32: (w, v) => w.varint32(v), uvarint32: (w, v) => w.uvarint32(v),
  varint64: (w, v) => w.varint64(v), uvarint64: (w, v) => w.uvarint64(v),
  zigzag32: (w, v) => w.varint32(v), zigzag64: (w, v) => w.varint64(v),
  string: (w, v) => w.string(v),
};

function writeField(writer, field, value, schema, depth) {
  const type = field.t;
  if (depth > 24) throw new Undecodable('schema nests too deeply', field.n);
  if (typeof type !== 'string') {
    throw new Undecodable(
      type && type.key ? 'map fields cannot be encoded from the schema'
        : type && type.switch ? 'tagged unions cannot be encoded from the schema'
        : 'this field shape cannot be encoded', field.n);
  }
  if (PRIMITIVE_WRITERS[type]) {
    if (value === undefined || value === null) throw new Undecodable('missing value', field.n);
    PRIMITIVE_WRITERS[type](writer, value);
    return;
  }
  if (type === 'CompoundTag' || type === 'NBT') throw new Undecodable('NBT fields cannot be encoded yet', field.n);

  const composite = schema.types[type.replace(/::/g, '__')];
  if (!composite) throw new Undecodable(`unknown wire type '${type}'`, field.n);
  if (value === undefined || value === null) throw new Undecodable('missing value', field.n);
  writeFields(writer, composite, value, schema, depth + 1);
}

function writeFields(writer, fields, source, schema, depth) {
  for (const field of fields) {
    if (field.o) {
      throw new Undecodable(
        'this packet has an optional field, whose presence the schema does not describe', field.n);
    }
    // A constant is fixed on the wire, so it is written from the schema rather than from the caller.
    if (field.c !== undefined) {
      writeField(writer, field, field.c, schema, depth);
      continue;
    }
    const value = field.n === undefined ? undefined : source[field.n];
    if (field.r) {
      if (!Array.isArray(value)) throw new Undecodable('expected an array', field.n);
      const count = PRIMITIVE_WRITERS[field.r] ? field.r : 'uvarint32';
      PRIMITIVE_WRITERS[count](writer, value.length);
      for (const item of value) writeField(writer, field, item, schema, depth);
      continue;
    }
    writeField(writer, field, value, schema, depth);
  }
}

const packets = {
  /**
   * Decodes a payload against the schema for its packet id.
   *
   * Always returns a result rather than throwing: `complete` says whether every field was read, and
   * when it is false `stoppedAt`/`reason` say where and why. A partial result is still trustworthy up
   * to that point - the fields present were read at the right offsets.
   */
  decode(packetId, payload) {
    const schema = loadProtocolSchema();
    const entry = schema.packets[String(Number(packetId))];
    if (!entry) {
      return { id: Number(packetId), name: null, fields: {}, complete: false, reason: 'unknown packet id' };
    }
    const bytes = payload instanceof Uint8Array ? payload : toBytes(String(payload));
    const reader = new BinaryReader(bytes);
    const result = { id: Number(packetId), name: entry.n, fields: {}, complete: false };
    try {
      result.fields = readFields(reader, entry.f, schema, 0);
      result.complete = true;
      result.trailingBytes = reader.remaining;
    } catch (err) {
      result.reason = err instanceof Undecodable ? err.reason : String(err && err.message || err);
      if (err instanceof Undecodable && err.where) result.stoppedAt = err.where;
      result.bytesRead = reader.offset;
    }
    return result;
  },

  /**
   * Builds a payload from a packet object, the mirror of decode().
   *
   * Returns `{ ok, payload, reason, stoppedAt }`. A packet whose layout the schema cannot fully
   * describe cannot be built this way - `ok` is false and nothing is emitted, rather than a
   * half-formed payload the client would reject.
   */
  encode(packetId, fields) {
    const schema = loadProtocolSchema();
    const entry = schema.packets[String(Number(packetId))];
    if (!entry) return { ok: false, reason: 'unknown packet id' };
    const writer = new BinaryWriter();
    try {
      writeFields(writer, entry.f, fields ?? {}, schema, 0);
      return { ok: true, payload: writer.toBytes(), name: entry.n };
    } catch (err) {
      const out = { ok: false, name: entry.n,
                    reason: err instanceof Undecodable ? err.reason : String((err && err.message) || err) };
      if (err instanceof Undecodable && err.where) out.stoppedAt = err.where;
      return out;
    }
  },

  /** A writer for building a payload by hand, for packets encode() cannot describe. */
  writer() { return new BinaryWriter(); },

  /** The schema's packet name for an id, or null. */
  nameOf(packetId) {
    const entry = loadProtocolSchema().packets[String(Number(packetId))];
    return entry ? entry.n : null;
  },

  /** Which BDS release the loaded schema describes. */
  get schemaVersion() { return loadProtocolSchema().ref; },

  /** A payload as bytes, for hand-decoding or hashing. */
  toBytes(payload) { return payload instanceof Uint8Array ? payload : toBytes(String(payload)); },

  /** A reader over a payload, for packets the schema cannot describe end to end. */
  reader(payload) { return new BinaryReader(packets.toBytes(payload)); },
};

const endstoneModule = {
  server, events, commands, scheduler, packets, logger: server.logger, LogLevel: LEVELS, EventPriority: PRIORITIES,
};

// Handed to the virtual module's source through a well-known symbol rather than a bare global.
const API_SYMBOL = Symbol.for('endstone.api');
Object.defineProperty(globalThis, API_SYMBOL, {
  value: endstoneModule, enumerable: false, configurable: false, writable: false,
});

// A real ES module with named exports, served from memory. registerHooks is synchronous and applies
// to require() as well as import(), so one hook covers CommonJS and ESM plugins alike. CommonJS
// plugins get the namespace through Node's require(esm) support.
const VIRTUAL_URL = 'endstone:server';
// The named exports are generated from the API object rather than listed by hand: a hand-written list
// silently goes out of date, and the failure lands on the plugin author as "does not provide an export
// named 'x'" rather than on whoever added x.
const VIRTUAL_SOURCE = [
  `const api = globalThis[Symbol.for('endstone.api')];`,
  ...Object.keys(endstoneModule).map((name) => `export const ${name} = api.${name};`),
  `export default api;`,
  ``,
].join('\n');

NodeModule.registerHooks({
  resolve(specifier, context, nextResolve) {
    if (specifier === '@endstone-js/server') {
      return { url: VIRTUAL_URL, shortCircuit: true };
    }
    return nextResolve(specifier, context);
  },
  load(url, context, nextLoad) {
    if (url === VIRTUAL_URL) {
      return { format: 'module', source: VIRTUAL_SOURCE, shortCircuit: true };
    }
    return nextLoad(url, context);
  },
});
const plugins = new Map();
const pendingLoads = new Map();
let nextPluginId = 1;
let nextToken = 1;
let nextReloadCopy = 1;

const asArray = (value) =>
  value == null ? [] : Array.isArray(value) ? value.map(String) : [String(value)];

function readManifest(dir) {
  // Strip a UTF-8 BOM: editors and shells on Windows add one, and JSON.parse rejects it.
  const raw = nodeFs.readFileSync(nodePath.join(dir, 'package.json'), 'utf8').replace(/^﻿/, '');
  return JSON.parse(raw);
}

function ensureDependencies(dir, manifest) {
  const declared = Object.keys(manifest.dependencies || {});
  if (declared.length === 0) return;
  if (nodeFs.existsSync(nodePath.join(dir, 'node_modules'))) return;

  const endstone = manifest.endstone || {};
  if (endstone.autoInstall !== true) {
    // Dependencies are the user's to install; say so plainly instead of letting them meet a raw
    // MODULE_NOT_FOUND from deep inside the loader.
    binding.log(3,
      `plugin '${manifest.name}' declares ${declared.length} dependency/ies (${declared.join(', ')}) ` +
      `but has no node_modules. Run "npm install" in ${dir} - loading will fail if the plugin ` +
      `imports them. Set endstone.autoInstall to true in package.json to install automatically.`);
    return;
  }

  // Opt-in only, and synchronous on purpose: the plugin must not start before its dependencies
  // exist. This blocks the server thread, which is why it is not the default.
  binding.log(2, `installing dependencies for '${manifest.name}' (npm install in ${dir})...`);
  try {
    const { execFileSync } = require('node:child_process');
    const npm = process.platform === 'win32' ? 'npm.cmd' : 'npm';
    execFileSync(npm, ['install', '--omit=dev', '--no-audit', '--no-fund'], {
      cwd: dir,
      stdio: 'pipe',
      shell: process.platform === 'win32',
    });
    binding.log(2, `dependencies installed for '${manifest.name}'`);
  } catch (err) {
    binding.log(4,
      `npm install failed for '${manifest.name}': ${(err && err.message) || err}. ` +
      `Install manually with "npm install" in ${dir}.`);
  }
}

async function loadPluginAsync(target) {
  const isDirectory = nodeFs.statSync(target).isDirectory();
  let dir, entry, manifest;
  if (isDirectory) {
    dir = target;
    manifest = readManifest(target);
    entry = nodePath.resolve(dir, manifest.main || 'index.js');
  } else {
    dir = nodePath.dirname(target);
    entry = target;
    manifest = { name: nodePath.basename(target, nodePath.extname(target)) };
  }

  ensureDependencies(dir, manifest);

  // ESM and CommonJS are both first class, and both go through require().
  //
  // Not import(): the bootstrap has no file identity, so dynamic import() from it is routed to
  // Node's *embedder* import callback, which resolves only node: builtins and fails on a file URL
  // with ERR_UNKNOWN_BUILTIN_MODULE. require() has no such problem, and since Node 22.12 it loads
  // ES modules synchronously and returns their namespace, so one path serves both formats.
  const isEsm = manifest.type === 'module' || /\.mjs$/i.test(entry);
  const pluginRequire = NodeModule.createRequire(nodePath.join(dir, 'package.json'));

  // The plugin gets its id before its module runs, so commands registered at the top level can be
  // attributed to it and travel back with the metadata Endstone builds its description from.
  const id = nextPluginId++;
  const declarations = { pluginId: id, records: [] };
  const previousCollector = collectingDeclarations;
  collectingDeclarations = declarations;
  let loaded;
  try {
    loaded = pluginRequire(entry);
  }
  finally {
    collectingDeclarations = previousCollector;
  }
  // An ES module namespace exposes its default export as .default.
  const instance = (loaded && loaded.default) || loaded;
  const endstone = manifest.endstone || {};

  plugins.set(id, { id, dir, entry, instance, manifest, esm: isEsm, dirPlugin: isDirectory });
  return {
    id,
    // Endstone requires lowercase [a-z0-9_]; strip any npm scope and normalize separators.
    name: String(manifest.name || '').replace(/^@[^/]+\//, '').replace(/-/g, '_').toLowerCase(),
    version: String(manifest.version || '0.0.0'),
    apiVersion: endstone.apiVersion == null ? null : String(endstone.apiVersion),
    description: manifest.description == null ? null : String(manifest.description),
    website: manifest.homepage == null ? null : String(manifest.homepage),
    loadOrder: endstone.load == null ? null : String(endstone.load).toLowerCase(),
    authors: asArray(manifest.author ?? manifest.authors),
    depend: asArray(endstone.depend),
    // Endstone registers these with Bedrock, so a usage like "/gm <survival|creative>" becomes a real
    // enum argument the client completes. A usage must start with "/" + the command name.
    commands: declarations.records.map((command) => ({
      name: command.name,
      description: command.description,
      usages: command.usages.length ? command.usages : [`/${command.name}`],
      aliases: command.aliases,
      permissions: command.permissions,
    })),
  };
}

// One plugin load failure becomes a concise, actionable report. The first line says what happened and
// where; the full stack survives separately as `detail` for the cases where that is not enough.
function formatLoadError(err, target) {
  if (!err) return "unknown load failure";
  const name = err.name || "Error";
  const raw = (err.message || String(err)).trim();
  const code = err.code;

  if (code === "ERR_REQUIRE_ASYNC_MODULE") {
    return `${target}: the plugin uses top-level await, which an embedded Node.js host cannot load. ` +
           "Move the awaited work into onEnable() instead.";
  }
  if (code === "MODULE_NOT_FOUND" || code === "ERR_MODULE_NOT_FOUND") {
    const missing = /['"]([^'"]+)['"]/.exec(raw);
    const from = (err.requireStack && err.requireStack[0]) || "";
    return `${target}: cannot find module${missing ? ` '${missing[1]}'` : ""}` +
           `${from ? ` required from ${from}` : ""}. Run "npm install" in the plugin folder, or fix the import path.`;
  }
  if (code === "ERR_REQUIRE_ESM") {
    return `${target}: this file is an ES module and must be imported, not required. ` +
           'Add "type": "module" to the plugin\'s package.json or rename it to .mjs.';
  }

  // First stack frame that points at a real file rather than into Node itself.
  let frame = "";
  if (typeof err.stack === "string") {
    for (const line of err.stack.split("\n").slice(1)) {
      const trimmed = line.trim();
      if (!trimmed || /node:internal|internal\//.test(trimmed)) continue;
      frame = trimmed;
      break;
    }
  }

  if (name === "SyntaxError") {
    const at = /\((\S+?):(\d+):(\d+)\)$/.exec(frame) || /^(\S+?):(\d+):(\d+)$/.exec(frame);
    return `${target}: syntax error${at ? ` in ${at[1]} at line ${at[2]}, column ${at[3]}` : ""}: ${raw}`;
  }
  if (frame) {
    return `${target}: ${raw} at ${frame.replace(/^at\s+/, "")}`;
  }
  return `${target}: ${raw}`;
}

// Loading is asynchronous because ESM imports are. The host starts a load, then pumps the loop and
// polls until the result appears.
function beginLoad(target) {
  const token = nextToken++;
  pendingLoads.set(token, null);
  void (async () => {
    try {
      pendingLoads.set(token, { ok: true, meta: await loadPluginAsync(target) });
    } catch (err) {
      pendingLoads.set(token, {
        ok: false,
        message: formatLoadError(err, target),
        detail: (err && err.stack) || String(err),
      });
    }
  })();
  return token;
}

function pollLoad(token) {
  const result = pendingLoads.get(token);
  if (result == null) return null;
  pendingLoads.delete(token);
  return result;
}

const HOOKS = ['onLoad', 'onEnable', 'onDisable'];

function invokePlugin(id, hook) {
  const record = plugins.get(id);
  if (!record) throw new Error(`unknown plugin id ${id}`);
  const name = HOOKS[hook];
  const target = record.instance;
  if (!target || typeof target[name] !== 'function') return;
  // A hook may be async. The server does not wait for it, but a rejection is still reported rather
  // than surfacing later as an unhandledRejection with no context.
  activePluginId = id;
  try {
    const result = target[name]();
    if (result && typeof result.then === 'function') {
      result.catch((err) => {
        binding.log(4, `${record.manifest.name}.${name}() rejected: ${(err && err.stack) || err}`);
      });
    }
  } catch (err) {
    binding.log(4, `${record.manifest.name}.${name}() threw: ${(err && err.stack) || err}`);
  }
  finally {
    activePluginId = null;
  }
}

function unloadPlugin(id) {
  // Commands are dropped here but subscriptions are not: unload runs during shutdown, and
  // binding.unsubscribe would call back into a plugin manager that may already be tearing down.
  dropCommands(id);
  dropTasks(id);
  dropForms(id);
  dropRenderers(id);
  plugins.delete(id);
}

// --- plugin reload ------------------------------------------------------------------------------
// The Node host is not re-creatable, so a reload re-runs the plugin's own module instead: disable
// the old instance, drop its event subscriptions, purge its files from the require cache, then
// require the entry again and run onLoad/onEnable. The esn_plugin handle and the plugin's identity
// in Endstone never change, so the C++ side needs no involvement. Plugins must clear their own
// timers in onDisable, since only event subscriptions are tracked here.

function dropSubscriptions(pluginId) {
  for (const [subscription, list] of handlers) {
    let removed = false;
    for (let i = list.length - 1; i >= 0; --i) {
      if (list[i].plugin === pluginId) {
        list.splice(i, 1);
        removed = true;
      }
    }
    if (removed && list.length === 0) {
      handlers.delete(subscription);
      binding.unsubscribe(subscription);
    }
  }
}

function purgeModuleCache(dir) {
  // Local code under the plugin's directory is what a reload must pick up; node_modules deps are
  // left cached so shared state is not disturbed.
  //
  // Both the given path and its realpath count as roots: a plugin directory is often a symlink into
  // a checkout, and Node keys the require cache by realpath, so matching only the link path would
  // purge nothing and the reload would silently re-run the old code.
  const roots = [nodePath.resolve(dir)];
  try {
    const real = nodeFs.realpathSync(dir);
    if (real !== roots[0]) roots.push(real);
  } catch { /* the directory may have gone away; the resolved path is enough */ }
  const under = (file) => roots.some((root) => file.startsWith(root + nodePath.sep));
  const inNodeModules = (file) =>
    roots.some((root) => file.startsWith(nodePath.join(root, 'node_modules') + nodePath.sep));
  // The bootstrap's `require` is the embedder's stripped require with no .cache property; the real
  // registry is Module._cache.
  const cache = NodeModule._cache || {};
  for (const filename of Object.keys(cache)) {
    if (under(filename) && !inNodeModules(filename)) delete cache[filename];
  }
  const pathCache = NodeModule._pathCache || {};
  for (const key of Object.keys(pathCache)) delete pathCache[key];
}

/** Runs a lifecycle hook, tagging the plugin for subscription attribution and reporting rejections. */
function runHook(record, id, name) {
  const target = record.instance;
  if (!target || typeof target[name] !== 'function') return;
  activePluginId = id;
  try {
    const result = target[name]();
    if (result && typeof result.then === 'function') {
      result.catch((err) => {
        binding.log(4, `${record.manifest.name}.${name}() rejected: ${(err && err.stack) || err}`);
      });
    }
  } catch (err) {
    binding.log(4, `${record.manifest.name}.${name}() threw: ${(err && err.stack) || err}`);
  }
  finally {
    activePluginId = null;
  }
}

/** Recursively copies a plugin directory, leaving node_modules (and its contents) out. */
function copyPluginTree(dir, copy) {
  for (const entry of nodeFs.readdirSync(dir, { withFileTypes: true })) {
    if (entry.name === 'node_modules') continue;
    const src = nodePath.join(dir, entry.name);
    const dst = nodePath.join(copy, entry.name);
    if (entry.isDirectory()) {
      nodeFs.mkdirSync(dst, { recursive: true });
      copyPluginTree(src, dst);
    }
    else if (entry.isFile()) {
      nodeFs.copyFileSync(src, dst);
    }
  }
}

// The ES module loader keys its cache by realpath and exposes no way to clear it, so an in-place
// re-require returns the module from the first load. Loading from a uniquely named sibling copy gives
// the loader a fresh realpath: the plugin's edits are re-read, while node_modules (junctioned through)
// stays shared and cached, matching the CommonJS reload path.
function shadowCopyForReload(record) {
  const root = nodePath.dirname(record.dir);
  const prefix = `.${nodePath.basename(record.dir)}-reload-`;
  for (const stale of nodeFs.readdirSync(root)) {
    if (stale.startsWith(prefix)) {
      nodeFs.rmSync(nodePath.join(root, stale), { recursive: true, force: true });
    }
  }
  const copy = nodePath.join(root, prefix + nextReloadCopy++);
  nodeFs.mkdirSync(copy, { recursive: true });
  copyPluginTree(record.dir, copy);
  const realNodeModules = nodePath.join(record.dir, 'node_modules');
  if (nodeFs.existsSync(realNodeModules)) {
    nodeFs.symlinkSync(realNodeModules, nodePath.join(copy, 'node_modules'),
      process.platform === 'win32' ? 'junction' : 'dir');
  }
  return copy;
}

/** Reloads every loaded JavaScript plugin, or just the one named. Returns a per-plugin report. */
function reloadPlugins(filter) {
  const wanted = [...plugins.values()].filter((record) => {
    if (!filter) return true;
    if (typeof filter === 'number') return record.id === filter;
    return record.manifest.name === filter || nodePath.basename(record.dir) === filter;
  });
  const report = [];
  for (const record of wanted) {
    try {
      runHook(record, record.id, 'onDisable');
      dropSubscriptions(record.id);
      dropTasks(record.id);
      dropForms(record.id);
      dropRenderers(record.id);
      const wasDeclared = dropCommands(record.id);

      // Pick up edits to package.json, including a changed main entry point.
      if (nodeFs.existsSync(nodePath.join(record.dir, 'package.json'))) {
        record.manifest = readManifest(record.dir);
      }
      record.entry = nodePath.resolve(record.dir, record.manifest.main || 'index.js');
      purgeModuleCache(record.dir);

      let loadDir = record.dir;
      let entry = record.entry;
      if (record.esm && record.dirPlugin) {
        try {
          loadDir = shadowCopyForReload(record);
          entry = nodePath.resolve(loadDir, record.manifest.main || 'index.js');
        }
        catch (copyErr) {
          binding.log(3,
            `reload of '${record.manifest.name}': shadow copy failed ` +
            `(${(copyErr && copyErr.message) || copyErr}); reloading in place - ESM edits may not apply`);
        }
      }

      // Collect again, so top-level registrations are re-attached to this plugin.
      const declarations = { pluginId: record.id, records: [] };
      const previousCollector = collectingDeclarations;
      collectingDeclarations = declarations;
      let loaded;
      try {
        const pluginRequire = NodeModule.createRequire(nodePath.join(loadDir, 'package.json'));
        loaded = pluginRequire(entry);
      }
      finally {
        collectingDeclarations = previousCollector;
      }
      record.instance = (loaded && loaded.default) || loaded;

      // A name Endstone never saw cannot reach Bedrock's registry now - the description was built at
      // load. It still works by interception, so say so rather than failing the reload.
      for (const declared of declarations.records) {
        if (!wasDeclared.has(declared.name)) {
          declared.declared = false;
          binding.log(3,
            `'/${declared.name}' is new since the server started, so it will not appear in the ` +
            `client's command list until the server restarts. It works in the meantime.`);
        }
      }

      runHook(record, record.id, 'onLoad');
      runHook(record, record.id, 'onEnable');
      report.push({ name: record.manifest.name, ok: true });
    }
    catch (err) {
      binding.log(4, `reload of '${record.manifest.name}' failed: ${(err && err.stack) || err}`);
      report.push({ name: record.manifest.name, ok: false });
    }
  }
  // Which commands a player is allowed to see can have changed; push the refreshed list so it takes
  // effect without anyone reconnecting.
  if (binding.apiAvailable()) binding.updateCommands();
  return report;
}

binding.setRuntime({
  beginLoad, pollLoad, invoke: invokePlugin, unload: unloadPlugin, dispatchEvent,
  reload: reloadPlugins, command: runDeclaredCommand, task: runTask, formResult, renderMap,
});

// --- the built-in reload command ----------------------------------------------------------------
// Registered by the runtime rather than by a plugin, so it works on every server and survives a
// reload of anything - and it is the first user of the command API. Operator-only: reloading every
// plugin is not something an ordinary player should be able to do.
if (binding.apiAvailable()) {
  // Named jsreload, not reload: registering `reload` would intercept and cancel the server's own
  // /reload before it ever ran.
  commands.register('jsreload', (sender, args) => {
    const report = reloadPlugins(args[0]);
    const ok = report.filter((r) => r.ok).length;
    const summary = args[0]
      ? `Reloaded ${ok}/${report.length} matching plugin(s).`
      : `Reloaded ${ok}/${report.length} plugin(s).`;
    binding.log(2, `[reload] ${summary}`);
    sender.sendMessage(`§a${summary}`);
    for (const entry of report) {
      binding.log(2, `[reload] ${entry.name}: ${entry.ok ? 'ok' : 'failed'}`);
      if (!entry.ok) {
        sender.sendMessage(`§c${entry.name}: failed to reload`);
      }
    }
  }, {
    description: 'Reloads JavaScript plugins, or just the one named.',
    usage: '/jsreload [plugin]',
    op: true,
  });
}

// From here on, a registration that is not part of a plugin's module load really is too late, and the
// warning's advice - move it to the top level - is something the author can act on.
bootstrapping = false;

// Optional standalone entry point for a script run without BDS; plugin mode does not require it.
if (binding.scriptPath && nodeFs.existsSync(binding.scriptPath)) {
  const publicRequire = NodeModule.createRequire(binding.scriptPath);
  globalThis.require = publicRequire;
  publicRequire(binding.scriptPath);
}
