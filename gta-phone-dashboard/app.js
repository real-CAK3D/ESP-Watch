const SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";

let rxCharacteristic = null;
let lastGpsCommand = "";
let watchId = null;
let cameraStream = null;
let parkedFix = JSON.parse(localStorage.getItem("gtaNavParked") || "null");
let savedLandmarks = JSON.parse(localStorage.getItem("gtaNavLandmarks") || "[]");
let tripHistory = JSON.parse(localStorage.getItem("gtaNavTripHistory") || "[]");
let linkedPhoneContacts = JSON.parse(localStorage.getItem("gtaNavPhoneContacts") || "[]");
let linkedRecentCalls = JSON.parse(localStorage.getItem("gtaNavRecentCalls") || "[]");
let linkedGalleryItems = JSON.parse(localStorage.getItem("gtaNavGalleryItems") || "[]");
let linkedCalendarEvents = JSON.parse(localStorage.getItem("gtaNavCalendarEvents") || "[]");
let simulatedMail = JSON.parse(localStorage.getItem("gtaNavSimMail") || "[]");
let simulatedMissedCalls = JSON.parse(localStorage.getItem("gtaNavSimMissedCalls") || "[]");
let appSettings = JSON.parse(localStorage.getItem("gtaNavSettings") || "null") || {
  homeAddress: "",
  workAddress: "",
  timezone: Intl.DateTimeFormat().resolvedOptions().timeZone || "America/New_York",
  manualDate: "",
  manualTime: "",
  manualYear: "",
  alarmTime: "07:00",
  alarmLabel: "Wake",
  alarmEnabled: false,
  clockFace: "digital",
  mockBridge: false,
  vehicle: "",
  fuelType: "Regular",
  tankGallons: 14,
  mpg: 24,
  gasPrice: 3.49,
  travelMode: "drive",
};
let liveFix = null;
let gpsOrigin = null;
let gpsAnchorSent = false;
let mapData = null;
let mapView = { scale: 1, offsetX: 0, offsetY: 0 };
let artView = { zoom: 1, offsetX: 0, offsetY: 0 };
let userMapAdjusted = false;
let mapDrag = null;
let testMoveMeters = 0;
let testFix = null;
let currentHeading = 0;
let lastHeadingPushMs = 0;
let lastHeadingRenderMs = 0;
let addressSuggestTimer = null;
let lastTownName = "";
let headingManualUntil = 0;
let activeDestination = null;
let selectedLandmarkTypeId = "safe_house";
let selectedWatchAddress = localStorage.getItem("gtaNavSelectedWatch") || "";
let commandHistory = JSON.parse(localStorage.getItem("gtaNavCommandHistory") || "[]");
let latestWeather = JSON.parse(localStorage.getItem("gtaNavWeather") || "null");
const bleScanHits = new Map();
const KNOWN_WATCH_ADDRESS = "28:84:85:B4:F2:7C";
const LIVE_MAP_SCALE = 2.25;
const OVERVIEW_MAP_SCALE = 0.9;
const timezoneOptions = [
  "America/New_York",
  "America/Chicago",
  "America/Denver",
  "America/Los_Angeles",
  "America/Phoenix",
  "America/Anchorage",
  "Pacific/Honolulu",
  "UTC",
];
const themeChoices = {
  theme1: { label: "Theme 1", watch: "theme1" },
  theme2: { label: "Theme 2", watch: "theme2" },
  theme3: { label: "Theme 3", watch: "theme3" },
};
const clockFaceChoices = [
  ["digital", "Digital"],
  ["analog", "Analog"],
  ["classic", "Classic"],
  ["onehand", "One Hand"],
  ["basic", "Basic"],
  ["luxury", "Rolex"],
];
const savedTheme = localStorage.getItem("gtaNavTheme") || "theme1";
document.body.dataset.theme = savedTheme;

appSettings = {
  timezone: Intl.DateTimeFormat().resolvedOptions().timeZone || "America/New_York",
  manualDate: "",
  manualTime: "",
  manualYear: "",
  alarmTime: "07:00",
  alarmLabel: "Wake",
  alarmEnabled: false,
  clockFace: "digital",
  mockBridge: false,
  travelMode: "drive",
  ...appSettings,
};

const panels = {
  gps: document.querySelector("#gpsPanel"),
  contacts: document.querySelector("#contactsPanel"),
  texts: document.querySelector("#textsPanel"),
  mail: document.querySelector("#mailPanel"),
  web: document.querySelector("#webPanel"),
  jobs: document.querySelector("#jobsPanel"),
  mechanic: document.querySelector("#mechanicPanel"),
  camera: document.querySelector("#cameraPanel"),
  gallery: document.querySelector("#galleryPanel"),
  bank: document.querySelector("#bankPanel"),
  save: document.querySelector("#savePanel"),
  calendar: document.querySelector("#calendarPanel"),
  settings: document.querySelector("#settingsPanel"),
  debug: document.querySelector("#debugPanel"),
};

const debugEvents = [];

function addDebug(line) {
  const text = String(line || "").trim();
  if (!text) return;
  const parts = text.split("\n").map((part) => part.trim()).filter(Boolean);
  for (const part of parts) {
    if (debugEvents[debugEvents.length - 1] !== part) {
      debugEvents.push(part);
    }
  }
  while (debugEvents.length > 80) debugEvents.shift();
  const log = document.querySelector("#debugLog");
  if (log) {
    log.textContent = debugEvents.slice(-45).join("\n");
    log.scrollTop = log.scrollHeight;
  }
}

function updateDebugSnapshot() {
  const bridge = document.querySelector("#bridgeStatus")?.textContent || "Offline";
  const gps = document.querySelector("#gpsStatus")?.textContent || "Idle";
  const command = lastGpsCommand || "None";
  const map = mapData?.features?.length ? `${mapData.features.length} roads` : "Unavailable";
  const bridgeNode = document.querySelector("#debugBridge");
  const gpsNode = document.querySelector("#debugGps");
  const commandNode = document.querySelector("#debugCommand");
  const mapNode = document.querySelector("#debugMap");
  if (bridgeNode) bridgeNode.textContent = bridge;
  if (gpsNode) gpsNode.textContent = gps;
  if (commandNode) commandNode.textContent = command.split(",", 1)[0] || command;
  if (mapNode) mapNode.textContent = map;
}

window.nativeDebug = (text) => {
  addDebug(text);
  updateDebugSnapshot();
};

const mechanicLines = [
  "Dispatch here. Want your ride marked?",
  "Your wheels are pinned. Check the map.",
  "Ride location is live. Go collect it.",
  "Marker is down. Your car is waiting.",
  "Parking marker is set. Try not to lose this one.",
];

const contacts = [
  { name: "Mechanic", number: "328-555-0153", role: "Parking and vehicle marker", avatar: "LS" },
  { name: "Mors Mutual-ish", number: "611-555-0149", role: "Definitely-not-official insurance", avatar: "MM" },
  { name: "Downtown Cab Co-ish", number: "323-555-5555", role: "Pickup jokes and route handoff", avatar: "DC" },
  { name: "Merryweather-ish", number: "273-555-0120", role: "Security, excuses, and hold music", avatar: "MW" },
  { name: "Pegasus-ish", number: "328-555-0122", role: "Lifestyle toys and delivery-ish support", avatar: "PG" },
  { name: "Ammu-Nation-ish", number: "1-999-932-7667", role: "On-hold supply store parody", avatar: "A" },
  { name: "Safe House", number: "+15557654321", role: "Home base", avatar: "H" },
  { name: "Roadside", number: "+18005551212", role: "Roadside help placeholder", avatar: "RD" },
  { name: "Los Santos Customs-ish", number: "+15557772687", role: "Repairs, paint, and bad financial choices", avatar: "LS" },
  { name: "Maze Bank-ish", number: "+15556293265", role: "Route budget and account alerts", avatar: "$" },
  { name: "Emergency Services", number: "", role: "Emergency placeholder. No real-world dialing.", avatar: "!" },
  { name: "GTA-Nav Support", number: "+15550004282", role: "Bridge test number", avatar: "GN" },
];

function combinedContacts() {
  const seen = new Set();
  return [...contacts, ...linkedPhoneContacts].filter((contact) => {
    const key = normalizeNumber(contact.number || "") || `name:${String(contact.name || "").toLowerCase()}`;
    if (!key || seen.has(key)) return false;
    seen.add(key);
    return true;
  });
}

const conversationAgents = {
  mechanic: {
    name: "Mechanic",
    opener: "You lose the car again or are we pretending this is a strategy?",
    fallback: [
      "I can mark the last parked car, talk cars, or send the route. Tiny menu, big attitude.",
      "Muscle cars are loud, heavy, and emotionally unavailable. Good choice, usually.",
      "If it has four wheels and a starter problem, somebody will call it a classic.",
    ],
  },
  insurance: {
    name: "Mors Mutual-ish",
    opener: "Claims desk. Tell me what you did to this poor vehicle.",
    fallback: [
      "Policy says we care deeply. The fine print says we care from a safe distance.",
      "Was it stolen, crashed, exploded, or creatively parked upside down?",
      "After reviewing absolutely nothing, my professional opinion is: try another insurance company.",
    ],
  },
  roadside: {
    name: "Roadside",
    opener: "Roadside here. I can give you a route, a shrug, or both.",
    fallback: [
      "If the car is smoking, stop driving it. If you are smoking, open a window.",
      "I can route you to the parked marker once the phone has saved one.",
      "Flat tire, dead battery, bad vibes. We handle two of those.",
    ],
  },
  support: {
    name: "GTA-Nav Support",
    opener: "Bridge support online. Say BLE, weather, route, theme, or clock.",
    fallback: [
      "Mock bridge is for testing without the watch. Real BLE is for the actual beautiful headache.",
      "Weather comes from the phone and mirrors to the watch when you sync it.",
      "The watch stays on its page. The map quietly stays live when you flip to it.",
    ],
  },
};
let selectedConversationId = localStorage.getItem("gtaNavTextContact") || "mechanic";
let conversationHistory = JSON.parse(localStorage.getItem("gtaNavTextHistory") || "null") || {
  mechanic: [{ from: "agent", text: conversationAgents.mechanic.opener }],
  insurance: [{ from: "agent", text: conversationAgents.insurance.opener }],
  roadside: [{ from: "agent", text: conversationAgents.roadside.opener }],
  support: [{ from: "agent", text: conversationAgents.support.opener }],
};

const landmarkTypes = [
  { id: "safe_house", label: "Safe House", glyph: "H", color: "#61d76b" },
  { id: "pay_spray", label: "Pay 'n' Spray", glyph: "P", color: "#5ac8ff" },
  { id: "maze_bank", label: "Maze Bank", glyph: "$", color: "#f4d65e" },
  { id: "cluckin_bell", label: "Cluckin Bell", glyph: "C", color: "#ffbf4a" },
  { id: "burger_shot", label: "Burger Shot", glyph: "B", color: "#ffbf4a" },
  { id: "up_n_atom", label: "Up-n-Atom", glyph: "U", color: "#ffbf4a" },
  { id: "taco_bomb", label: "Taco Bomb", glyph: "Tb", color: "#ffbf4a" },
  { id: "cool_beans", label: "Cool Beans", glyph: "Cb", color: "#d6b07a" },
  { id: "fleeca", label: "Fleeca", glyph: "F", color: "#61d76b" },
  { id: "gruppe_sechs", label: "Gruppe Sechs", glyph: "6", color: "#6fa8ff" },
  { id: "post_op", label: "Post OP", glyph: "PO", color: "#f4d65e" },
  { id: "ammu_nation", label: "Ammu-Nation", glyph: "A", color: "#f66d5f" },
  { id: "garage", label: "Garage", glyph: "G", color: "#5ac8ff" },
  { id: "mod_shop", label: "Mod Shop", glyph: "M", color: "#5ac8ff" },
  { id: "los_santos_customs", label: "Los Santos Customs", glyph: "LS", color: "#5ac8ff" },
  { id: "barber", label: "Barber", glyph: "B", color: "#f0f0e6" },
  { id: "clothes", label: "Clothes", glyph: "T", color: "#f0f0e6" },
  { id: "hospital", label: "Hospital", glyph: "+", color: "#ff6b6b" },
  { id: "police", label: "Police", glyph: "L", color: "#6fa8ff" },
  { id: "airport", label: "Airport", glyph: "^", color: "#f0f0e6" },
  { id: "store", label: "Store", glyph: "S", color: "#ffbf4a" },
  { id: "binco", label: "Binco", glyph: "Bc", color: "#f0f0e6" },
  { id: "suburban", label: "Suburban", glyph: "Sb", color: "#f0f0e6" },
  { id: "ponsonbys", label: "Ponsonbys", glyph: "Pb", color: "#f0f0e6" },
  { id: "tattoo", label: "Tattoo", glyph: "Tt", color: "#f0f0e6" },
  { id: "tennis", label: "Tennis", glyph: "Te", color: "#f4d65e" },
  { id: "golf", label: "Golf", glyph: "Go", color: "#61d76b" },
  { id: "race", label: "Race", glyph: "R", color: "#f4d65e" },
  { id: "mission", label: "Mission", glyph: "?", color: "#f4d65e" },
  { id: "waypoint", label: "Waypoint", glyph: "W", color: "#f4d65e" },
  { id: "parking", label: "Parking", glyph: "K", color: "#5ac8ff" },
  { id: "mechanic", label: "Mechanic", glyph: "J", color: "#5ac8ff" },
  { id: "job", label: "Job", glyph: "Jb", color: "#f4d65e" },
];

const gtaBrandRules = [
  { match: /\bmcdonald'?s\b|\bkfc\b|\bpopeyes\b|\bchicken\b|\bchick[- ]?fil[- ]?a\b/i, name: "Cluckin' Bell", type: "cluckin_bell" },
  { match: /\bburger\s*king\b|\bwendy'?s\b|\bfive guys\b|\bsmashburger\b|\bburger\b/i, name: "Burger Shot", type: "burger_shot" },
  { match: /\bin[- ]?n[- ]?out\b|\bsonic\b|\bdairy queen\b|\ba\s*&\s*w\b/i, name: "Up-n-Atom Burger", type: "up_n_atom" },
  { match: /\btaco bell\b|\btaco\b|\bmexican\b|\bchipotle\b|\bmoe'?s\b/i, name: "Taco Bomb", type: "taco_bomb" },
  { match: /\bstarbucks\b|\bdunkin\b|\bcoffee\b|\bcafe\b|\bcafé\b/i, name: "Cool Beans", type: "cool_beans" },
  { match: /\bcar wash\b|\bauto wash\b|\bwash\b|\bdetail\b/i, name: "Pay 'n' Spray", type: "pay_spray" },
  { match: /\bauto repair\b|\bgarage\b|\bmechanic\b|\btires?\b|\boil change\b|\bjiffy lube\b|\bmidas\b|\bvalvoline\b/i, name: "Los Santos Customs", type: "los_santos_customs" },
  { match: /\bgas\b|\bfuel\b|\bshell\b|\bmobil\b|\bsunoco\b|\bcircle k\b|\birving\b|\bcumberland\b/i, name: "RON", type: "store" },
  { match: /\bbank of america\b|\bchase\b|\bwells fargo\b|\bcredit union\b|\bbank\b|\batm\b/i, name: "Maze Bank", type: "maze_bank" },
  { match: /\btd bank\b|\bcredit card\b|\bcard\b/i, name: "Fleeca", type: "fleeca" },
  { match: /\bsecurity\b|\bguard\b|\balarm\b/i, name: "Gruppe Sechs", type: "gruppe_sechs" },
  { match: /\bpost office\b|\busps\b|\bups\b|\bfedex\b|\bmail\b/i, name: "Post OP", type: "post_op" },
  { match: /\bgun\b|\bsporting goods\b|\bfirearm\b|\brange\b/i, name: "Ammu-Nation", type: "ammu_nation" },
  { match: /\bwalmart\b|\btarget\b|\bdollar general\b|\bmarket\b|\bgrocery\b|\bstore\b|\bshop\b/i, name: "24/7", type: "store" },
  { match: /\bhospital\b|\bmedical\b|\ber\b|\burgent care\b|\bclinic\b/i, name: "Mount Zonah Medical", type: "hospital" },
  { match: /\bpolice\b|\bsheriff\b|\bpublic safety\b/i, name: "LSPD", type: "police" },
  { match: /\bairport\b|\bairfield\b/i, name: "Los Santos International", type: "airport" },
  { match: /\bbarber\b|\bhair\b|\bsalon\b/i, name: "Herr Kutz", type: "barber" },
  { match: /\btattoo\b/i, name: "Blazing Tattoo", type: "tattoo" },
  { match: /\bclothes\b|\bapparel\b|\bfashion\b|\bthrift\b/i, name: "Binco", type: "binco" },
  { match: /\binsurance\b/i, name: "Mors Mutual-ish", type: "maze_bank" },
];

const seededParodyPoiTemplates = [
  { name: "Cluckin' Bell", type: "cluckin_bell", north: 620, east: -480 },
  { name: "Burger Shot", type: "burger_shot", north: -460, east: 520 },
  { name: "Pay 'n' Spray", type: "pay_spray", north: 360, east: 780 },
  { name: "Los Santos Customs", type: "los_santos_customs", north: -780, east: -240 },
  { name: "Maze Bank", type: "maze_bank", north: 880, east: 260 },
  { name: "Fleeca", type: "fleeca", north: -260, east: -760 },
  { name: "Cool Beans", type: "cool_beans", north: 160, east: -1080 },
  { name: "Taco Bomb", type: "taco_bomb", north: -980, east: 780 },
  { name: "24/7", type: "store", north: 1040, east: -90 },
  { name: "Post OP", type: "post_op", north: -80, east: 1140 },
  { name: "Gruppe Sechs", type: "gruppe_sechs", north: 1240, east: 720 },
  { name: "Binco", type: "binco", north: -1180, east: -620 },
];
let seededParodyLandmarks = [];

const secretResponses = {
  "19993673767": "Weather cheat? Nice try. Sending you to the Web page instead.",
  "2735550150": "Signal bounced. Unknown contact, but the watch is still listening.",
  "3285550101": "Garage ping received. Mark a parked car to unlock this route.",
  "6115550100": "Dispatch test line connected. No in-game audio is stored here.",
  "9485550100": "Taxi-style pickup requested. GTA-Nav can route, not drive.",
};

const gtaStyle = {
  background: "#000000",
  landscape: "#ffffff",
  manMade: "#1d1d1d",
  manMadeStroke: "#000000",
  park: "#788c40",
  highway: "#bebebe",
  arterial: "#aeaeae",
  local: "#777777",
  water: "#7088b0",
  player: "#f4f1df",
  route: "#f3aa23",
};
let snazzyStyleLoaded = false;

const mapCanvas = document.querySelector("#pauseMapCanvas");
const mapCtx = mapCanvas.getContext("2d");
const mapFallback = document.querySelector("#mapFallback");

async function loadLocalJson(path, globalName) {
  if (window[globalName]) {
    return window[globalName];
  }
  if (window.GtaNavNative?.getAssetText) {
    const nativeText = window.GtaNavNative.getAssetText(path);
    if (nativeText) {
      return JSON.parse(nativeText);
    }
  }
  try {
    const res = await fetch(path);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return await res.json();
  } catch (fetchError) {
    return new Promise((resolve, reject) => {
      const request = new XMLHttpRequest();
      request.open("GET", path, true);
      request.onload = () => {
        if (request.status === 0 || (request.status >= 200 && request.status < 300)) {
          try {
            resolve(JSON.parse(request.responseText));
          } catch (parseError) {
            reject(parseError);
          }
        } else {
          reject(new Error(`HTTP ${request.status}`));
        }
      };
      request.onerror = () => reject(fetchError);
      request.send();
    });
  }
}

function snazzyColor(rules, featureType, elementType, fallback) {
  const rule = rules.find((item) => item.featureType === featureType && item.elementType === elementType);
  const color = rule?.stylers?.find((item) => item.color)?.color;
  return color || fallback;
}

async function loadSnazzyStyle() {
  if (snazzyStyleLoaded) return;
  const rules = await loadLocalJson("map-data/snazzy-gta5-style.json", "GTA_NAV_SNAZZY_STYLE");
  gtaStyle.background = snazzyColor(rules, "landscape.natural", "geometry", gtaStyle.background);
  gtaStyle.landscape = snazzyColor(rules, "landscape", "geometry", gtaStyle.landscape);
  gtaStyle.manMade = snazzyColor(rules, "landscape.man_made", "geometry", gtaStyle.manMade);
  gtaStyle.manMadeStroke = snazzyColor(rules, "landscape.man_made", "geometry.stroke", gtaStyle.manMadeStroke);
  gtaStyle.park = snazzyColor(rules, "poi.park", "geometry.fill", gtaStyle.park);
  gtaStyle.highway = snazzyColor(rules, "road.highway", "geometry", gtaStyle.highway);
  gtaStyle.arterial = snazzyColor(rules, "road.arterial", "geometry", gtaStyle.arterial);
  gtaStyle.local = snazzyColor(rules, "road.local", "geometry", gtaStyle.local);
  gtaStyle.water = snazzyColor(rules, "water", "geometry.fill", gtaStyle.water);
  snazzyStyleLoaded = true;
}

function mapProjectionCenter() {
  const fix = liveFix || testFix;
  if (fix) {
    return { lat: fix.latitude, lon: fix.longitude };
  }
  return mapData?.center || { lat: 44.09785, lon: -70.1942 };
}

function fixInsideMapBounds(fix) {
  if (!mapData?.bounds || !fix) return false;
  return fix.latitude >= mapData.bounds.minLat &&
    fix.latitude <= mapData.bounds.maxLat &&
    fix.longitude >= mapData.bounds.minLon &&
    fix.longitude <= mapData.bounds.maxLon;
}

function normalizeHeading(heading) {
  return ((heading % 360) + 360) % 360;
}

function headingDelta(from, to) {
  return ((normalizeHeading(to) - normalizeHeading(from) + 540) % 360) - 180;
}

function distanceMeters(a, b) {
  if (!a || !b) return Number.POSITIVE_INFINITY;
  const r = 6371000;
  const lat1 = a.latitude * Math.PI / 180;
  const lat2 = b.latitude * Math.PI / 180;
  const dLat = (b.latitude - a.latitude) * Math.PI / 180;
  const dLon = (b.longitude - a.longitude) * Math.PI / 180;
  const h = Math.sin(dLat / 2) ** 2 +
    Math.cos(lat1) * Math.cos(lat2) * Math.sin(dLon / 2) ** 2;
  return 2 * r * Math.atan2(Math.sqrt(h), Math.sqrt(1 - h));
}

function mapBiasFix() {
  if (liveFix) return liveFix;
  if (gpsOrigin) return gpsOrigin;
  if (mapData?.center) {
    return { latitude: mapData.center.lat, longitude: mapData.center.lon };
  }
  return { latitude: 44.09785, longitude: -70.1942 };
}

function mercator(lat, lon, center = mapProjectionCenter()) {
  const metersPerLon = 111320 * Math.cos(center.lat * Math.PI / 180);
  return {
    x: (lon - center.lon) * metersPerLon,
    y: (center.lat - lat) * 111320,
  };
}

function metersPerLonAt(lat) {
  return 111320 * Math.cos(lat * Math.PI / 180);
}

function gpsOffsetFromOrigin(fix) {
  if (!fix || !gpsOrigin) {
    return { east: 0, north: 0 };
  }
  return {
    east: (fix.longitude - gpsOrigin.longitude) * metersPerLonAt(gpsOrigin.latitude),
    north: (fix.latitude - gpsOrigin.latitude) * 111320,
  };
}

function projectPoint(lat, lon) {
  const p = mercator(lat, lon);
  return {
    x: mapCanvas.width / 2 + (p.x * mapView.scale) + mapView.offsetX,
    y: mapCanvas.height / 2 + (p.y * mapView.scale) + mapView.offsetY,
  };
}

function fitMapToBounds() {
  if (!mapData) return;
  const center = mapData.center;
  const nw = mercator(mapData.bounds.maxLat, mapData.bounds.minLon, center);
  const se = mercator(mapData.bounds.minLat, mapData.bounds.maxLon, center);
  const width = Math.max(1, se.x - nw.x);
  const height = Math.max(1, se.y - nw.y);
  mapView.scale = Math.min(mapCanvas.width / width, mapCanvas.height / height) * 1.48;
  mapView.offsetX = -((nw.x + se.x) / 2) * mapView.scale;
  mapView.offsetY = -((nw.y + se.y) / 2) * mapView.scale;
}

function centerLiveMap(force = false) {
  if (!mapData || !liveFix) return;
  if (userMapAdjusted && !force) return;
  mapView.scale = fixInsideMapBounds(liveFix) ? LIVE_MAP_SCALE : LIVE_MAP_SCALE * 1.15;
  mapView.offsetX = 0;
  mapView.offsetY = 0;
  artView.zoom = 1.6;
  artView.offsetX = 0;
  artView.offsetY = 0;
}

function strokeFeature(feature, width, color, outline = "#513315") {
  const pts = feature.points;
  if (!pts || pts.length < 2) return;
  mapCtx.beginPath();
  pts.forEach(([lat, lon], index) => {
    const p = projectPoint(lat, lon);
    if (index === 0) mapCtx.moveTo(p.x, p.y);
    else mapCtx.lineTo(p.x, p.y);
  });
  mapCtx.lineJoin = "round";
  mapCtx.lineCap = "round";
  mapCtx.strokeStyle = outline;
  mapCtx.lineWidth = width + 5;
  mapCtx.stroke();
  mapCtx.strokeStyle = color;
  mapCtx.lineWidth = width;
  mapCtx.stroke();
}

function roadColor(feature) {
  if (feature.weight >= 5) return gtaStyle.highway;
  if (feature.weight >= 3) return gtaStyle.arterial;
  return gtaStyle.local;
}

function roadWidth(feature) {
  if (feature.weight >= 5) return 7.5;
  if (feature.weight >= 3) return 5.4;
  return 3.2;
}

function fillFeature(feature, fill, stroke) {
  const pts = feature.points;
  if (!pts || pts.length < 3) return;
  mapCtx.beginPath();
  pts.forEach(([lat, lon], index) => {
    const p = projectPoint(lat, lon);
    if (index === 0) mapCtx.moveTo(p.x, p.y);
    else mapCtx.lineTo(p.x, p.y);
  });
  mapCtx.closePath();
  mapCtx.fillStyle = fill;
  mapCtx.fill();
  if (stroke) {
    mapCtx.strokeStyle = stroke;
    mapCtx.lineWidth = 1.5;
    mapCtx.stroke();
  }
}

function drawGtaStyledOsmMap() {
  if (!mapData?.features) return false;
  const features = mapData.features;

  mapCtx.fillStyle = gtaStyle.background;
  mapCtx.fillRect(0, 0, mapCanvas.width, mapCanvas.height);

  features
    .filter((feature) => feature.kind === "water")
    .forEach((feature) => fillFeature(feature, gtaStyle.water, "#000000"));

  features
    .filter((feature) => feature.kind === "land")
    .forEach((feature) => fillFeature(feature, gtaStyle.park, "#000000"));

  features
    .filter((feature) => feature.kind === "building")
    .forEach((feature) => fillFeature(feature, gtaStyle.manMade, gtaStyle.manMadeStroke));

  features
    .filter((feature) => feature.kind === "road")
    .sort((a, b) => a.weight - b.weight)
    .forEach((feature) => strokeFeature(feature, roadWidth(feature), roadColor(feature), "#050505"));

  return true;
}

function drawScreenBlip(x, y, type = "player", heading = 0) {
  mapCtx.save();
  mapCtx.translate(x, y);
  if (type === "player") {
    mapCtx.rotate((heading * Math.PI) / 180);
    mapCtx.shadowColor = "rgba(0,0,0,0.55)";
    mapCtx.shadowBlur = 4;
    mapCtx.lineJoin = "round";
    mapCtx.beginPath();
    mapCtx.moveTo(0, -24);
    mapCtx.lineTo(15, 17);
    mapCtx.lineTo(0, 9);
    mapCtx.lineTo(-15, 17);
    mapCtx.closePath();
    mapCtx.strokeStyle = "#050505";
    mapCtx.lineWidth = 6;
    mapCtx.stroke();
    mapCtx.shadowBlur = 0;
    mapCtx.fillStyle = gtaStyle.player;
    mapCtx.fill();
    mapCtx.beginPath();
    mapCtx.arc(0, 1, 4, 0, Math.PI * 2);
    mapCtx.fillStyle = "#050505";
    mapCtx.fill();
  } else {
    mapCtx.beginPath();
    mapCtx.arc(0, 0, 9, 0, Math.PI * 2);
    mapCtx.fillStyle = "#5ac8ff";
    mapCtx.strokeStyle = "#050505";
    mapCtx.lineWidth = 4;
    mapCtx.stroke();
    mapCtx.fill();
  }
  mapCtx.restore();
}

function drawGpsBlip(fix, type = "player") {
  if (!fix) return;
  const margin = 28;
  let projected;
  if (type === "player" && fix === liveFix) {
    projected = { x: mapCanvas.width / 2, y: mapCanvas.height / 2 };
  } else if (mapData) {
    projected = projectPoint(fix.latitude, fix.longitude);
  } else {
    const offset = gpsOffsetFromOrigin(fix);
    const pixelsPerMeter = 2.2 * artView.zoom;
    projected = {
      x: mapCanvas.width / 2 + offset.east * pixelsPerMeter + artView.offsetX * 0.08,
      y: mapCanvas.height / 2 - offset.north * pixelsPerMeter + artView.offsetY * 0.08,
    };
  }
  const x = Math.max(margin, Math.min(mapCanvas.width - margin, projected.x));
  const y = Math.max(margin, Math.min(mapCanvas.height - margin, projected.y));
  drawScreenBlip(x, y, type, fix.heading || 0);
}

function landmarkTypeFor(id) {
  return landmarkTypes.find((type) => type.id === id) || landmarkTypes[0];
}

function parodyPlaceFor(name, fallbackType = "waypoint") {
  const raw = String(name || "");
  const rule = gtaBrandRules.find((item) => item.match.test(raw));
  if (!rule) {
    return { name: raw, type: fallbackType };
  }
  return { name: rule.name, type: rule.type };
}

function normalizeLandmarkBrand(landmark) {
  const parody = parodyPlaceFor(landmark.name || landmark.displayName || "", landmark.type || "waypoint");
  return {
    ...landmark,
    name: parody.name || landmark.name,
    type: parody.type || landmark.type,
    realName: landmark.realName || landmark.name,
  };
}

function fixFromMapOffset(north, east) {
  const center = mapBiasFix();
  return {
    latitude: center.latitude + north / 111320,
    longitude: center.longitude + east / metersPerLonAt(center.latitude),
  };
}

function buildSeededParodyLandmarks() {
  if (!mapData && !gpsOrigin) return [];
  seededParodyLandmarks = seededParodyPoiTemplates.map((poi, index) => {
    const fix = fixFromMapOffset(poi.north, poi.east);
    return {
      ...poi,
      latitude: fix.latitude,
      longitude: fix.longitude,
      savedAt: 0,
      seeded: true,
      index,
    };
  });
  return seededParodyLandmarks;
}

function visibleLandmarksForMap() {
  if (!seededParodyLandmarks.length && (mapData || gpsOrigin)) {
    buildSeededParodyLandmarks();
  }
  return [...seededParodyLandmarks, ...savedLandmarks.map(normalizeLandmarkBrand)];
}

function drawLandmark(landmark) {
  if (!mapData && !gpsOrigin) return;
  const bias = mapBiasFix();
  if (liveFix && distanceMeters(bias, landmark) > 80000) return;
  const margin = 22;
  let projected;
  if (mapData) {
    projected = projectPoint(landmark.latitude, landmark.longitude);
  } else {
    const offset = gpsOffsetFromOrigin(landmark);
    const pixelsPerMeter = 2.2 * artView.zoom;
    projected = {
      x: mapCanvas.width / 2 + offset.east * pixelsPerMeter + artView.offsetX * 0.08,
      y: mapCanvas.height / 2 - offset.north * pixelsPerMeter + artView.offsetY * 0.08,
    };
  }
  const x = Math.max(margin, Math.min(mapCanvas.width - margin, projected.x));
  const y = Math.max(margin, Math.min(mapCanvas.height - margin, projected.y));
  const type = landmarkTypeFor(landmark.type);
  mapCtx.save();
  mapCtx.translate(x, y);
  mapCtx.fillStyle = "#050505";
  mapCtx.beginPath();
  mapCtx.arc(0, 0, 15, 0, Math.PI * 2);
  mapCtx.fill();
  mapCtx.fillStyle = type.color;
  mapCtx.beginPath();
  mapCtx.arc(0, 0, 10, 0, Math.PI * 2);
  mapCtx.fill();
  mapCtx.fillStyle = "#050505";
  mapCtx.font = "bold 14px Arial";
  mapCtx.textAlign = "center";
  mapCtx.textBaseline = "middle";
  mapCtx.fillText(type.glyph, 0, 1);
  if (artView.zoom < 1.7) {
    mapCtx.fillStyle = "rgba(5,5,5,0.86)";
    const label = landmark.name || type.label;
    const width = Math.min(170, mapCtx.measureText(label).width + 16);
    mapCtx.fillRect(14, -13, width, 24);
    mapCtx.strokeStyle = type.color;
    mapCtx.strokeRect(14, -13, width, 24);
    mapCtx.fillStyle = "#fff4cb";
    mapCtx.font = "12px Arial";
    mapCtx.textAlign = "left";
    mapCtx.fillText(label, 21, 0);
  }
  mapCtx.restore();
}

function drawActiveRoute() {
  if (!activeDestination || !liveFix || !mapData) return;
  const start = projectPoint(liveFix.latitude, liveFix.longitude);
  const end = projectPoint(activeDestination.latitude, activeDestination.longitude);
  const clampedStart = { x: mapCanvas.width / 2, y: mapCanvas.height / 2 };
  const dx = end.x - clampedStart.x;
  const dy = end.y - clampedStart.y;
  const distance = Math.hypot(dx, dy);
  const maxDistance = Math.max(mapCanvas.width, mapCanvas.height) * 0.58;
  const scale = distance > maxDistance ? maxDistance / distance : 1;
  const routeEnd = {
    x: clampedStart.x + dx * scale,
    y: clampedStart.y + dy * scale,
  };
  mapCtx.save();
  mapCtx.lineJoin = "round";
  mapCtx.lineCap = "round";
  mapCtx.strokeStyle = "#050505";
  mapCtx.lineWidth = 11;
  mapCtx.beginPath();
  mapCtx.moveTo(start.x, start.y);
  mapCtx.lineTo(clampedStart.x, clampedStart.y);
  mapCtx.lineTo(routeEnd.x, routeEnd.y);
  mapCtx.stroke();
  mapCtx.strokeStyle = gtaStyle.route;
  mapCtx.lineWidth = 5;
  mapCtx.stroke();
  mapCtx.restore();
}

function drawPauseMap() {
  const w = mapCanvas.width;
  const h = mapCanvas.height;
  if (mapFallback) {
    mapFallback.hidden = true;
  }
  mapCtx.fillStyle = gtaStyle.background;
  mapCtx.fillRect(0, 0, w, h);

  if (drawGtaStyledOsmMap()) {
    mapCtx.fillStyle = "rgba(0, 0, 0, 0.10)";
    mapCtx.fillRect(0, 0, w, h);
  } else {
    mapCtx.fillStyle = "#050505";
    mapCtx.fillRect(0, 0, w, h);
    mapCtx.fillStyle = "#fff4cb";
    mapCtx.font = "bold 22px Arial";
    mapCtx.textAlign = "center";
    mapCtx.fillText("MAP DATA LOADING", w / 2, h / 2);
  }

  visibleLandmarksForMap().forEach(drawLandmark);
  if (parkedFix) drawGpsBlip(parkedFix, "parked");
  drawActiveRoute();
  if (liveFix) drawGpsBlip(liveFix, "player");
  if (mapData && liveFix && !fixInsideMapBounds(liveFix)) {
    mapCtx.fillStyle = "rgba(5, 5, 5, 0.86)";
    mapCtx.fillRect(18, h - 48, 320, 30);
    mapCtx.strokeStyle = gtaStyle.route;
    mapCtx.strokeRect(18, h - 48, 320, 30);
    mapCtx.fillStyle = "#fff4cb";
    mapCtx.font = "bold 14px Arial";
    mapCtx.textAlign = "left";
    mapCtx.fillText("LIVE GPS OUTSIDE OFFLINE MAP PACK", 28, h - 28);
  }
}

function bindMapGestures() {
  const map = document.querySelector("#pauseMap");
  map.addEventListener("pointerdown", (event) => {
    map.setPointerCapture(event.pointerId);
    mapDrag = {
      x: event.clientX,
      y: event.clientY,
      offsetX: mapView.offsetX,
      offsetY: mapView.offsetY,
      artOffsetX: artView.offsetX,
      artOffsetY: artView.offsetY,
    };
  });
  map.addEventListener("pointermove", (event) => {
    if (!mapDrag) return;
    userMapAdjusted = true;
    const rect = map.getBoundingClientRect();
    const dx = (event.clientX - mapDrag.x) * (mapCanvas.width / rect.width);
    const dy = (event.clientY - mapDrag.y) * (mapCanvas.height / rect.height);
    mapView.offsetX = mapDrag.offsetX + dx;
    mapView.offsetY = mapDrag.offsetY + dy;
    artView.offsetX = mapDrag.artOffsetX + dx;
    artView.offsetY = mapDrag.artOffsetY + dy;
    drawPauseMap();
  });
  map.addEventListener("pointerup", () => {
    mapDrag = null;
  });
  map.addEventListener("wheel", (event) => {
    event.preventDefault();
    zoomPauseMap(event.deltaY < 0 ? 1.12 : 0.89);
  }, { passive: false });
}

function zoomPauseMap(factor) {
  userMapAdjusted = true;
  const next = mapView.scale * factor;
  mapView.scale = Math.max(0.02, Math.min(4.5, next));
  artView.zoom = Math.max(0.7, Math.min(4.8, artView.zoom * factor));
  drawPauseMap();
}

function resetPauseMapZoom() {
  userMapAdjusted = false;
  if (liveFix) {
    centerLiveMap(true);
  } else {
    fitMapToBounds();
  }
  drawPauseMap();
}

async function loadPauseMap() {
  try {
    if (window.GTA_NAV_SNAZZY_STYLE) {
      const rules = window.GTA_NAV_SNAZZY_STYLE;
      gtaStyle.background = snazzyColor(rules, "landscape.natural", "geometry", gtaStyle.background);
      gtaStyle.landscape = snazzyColor(rules, "landscape", "geometry", gtaStyle.landscape);
      gtaStyle.manMade = snazzyColor(rules, "landscape.man_made", "geometry", gtaStyle.manMade);
      gtaStyle.manMadeStroke = snazzyColor(rules, "landscape.man_made", "geometry.stroke", gtaStyle.manMadeStroke);
      gtaStyle.park = snazzyColor(rules, "poi.park", "geometry.fill", gtaStyle.park);
      gtaStyle.highway = snazzyColor(rules, "road.highway", "geometry", gtaStyle.highway);
      gtaStyle.arterial = snazzyColor(rules, "road.arterial", "geometry", gtaStyle.arterial);
      gtaStyle.local = snazzyColor(rules, "road.local", "geometry", gtaStyle.local);
      gtaStyle.water = snazzyColor(rules, "water", "geometry.fill", gtaStyle.water);
      snazzyStyleLoaded = true;
    } else {
      await loadSnazzyStyle();
    }
    mapData = window.GTA_NAV_MAP_DATA || await loadLocalJson("map-data/lewiston-style-map.json", "GTA_NAV_MAP_DATA");
    if (!mapData?.features?.length) {
      throw new Error("Bundled map payload is empty");
    }
    fitMapToBounds();
    drawPauseMap();
    document.querySelector("#mapMode").textContent =
      `Snazzy GTA / ${mapData.counts.road} roads`;
    document.querySelector("#gpsReadout").textContent = "Map data loaded. Start GPS to center on your location.";
  } catch (error) {
    document.querySelector("#mapMode").textContent = "map data unavailable";
    document.querySelector("#gpsReadout").textContent = `Map data failed: ${error.message || error}`;
    drawPauseMap();
    if (mapFallback) {
      mapFallback.hidden = false;
      mapFallback.textContent = "Map data unavailable";
    }
  }
}

function setClock() {
  document.querySelector("#clock").textContent = new Date().toLocaleTimeString([], {
    hour: "2-digit",
    minute: "2-digit",
  });
}

function showPanel(name) {
  document.querySelector("#homeGrid").hidden = Boolean(name);
  Object.entries(panels).forEach(([key, panel]) => {
    panel.hidden = key !== name;
  });
  if (name === "gps") {
    requestAnimationFrame(drawPauseMap);
  }
  updateDebugSnapshot();
}

function setBleState(text) {
  const normalized = String(text || "offline").toLowerCase();
  const linked =
    normalized.includes("linked") &&
    !normalized.includes("not linked") &&
    !normalized.includes("no ") &&
    !normalized.includes("offline") &&
    !normalized.includes("local only");
  document.querySelector("#bleState").textContent = text;
  document.querySelector("#bridgeStatus").textContent = linked ? "Watch linked" : text;
  if (linked) {
    bleScanHits.clear();
    const list = document.querySelector("#bleScanList");
    if (list) {
      list.hidden = false;
      list.innerHTML = `<p class="hint">GTA-Nav watch linked. Live bridge is active.</p>`;
    }
  }
  updateDebugSnapshot();
  const signal = document.querySelector("#watchSignal");
  if (signal) {
    signal.className = "bars";
    if (linked) signal.classList.add("signal-linked");
    else if (normalized.includes("pair") || normalized.includes("scan") || normalized.includes("connect")) signal.classList.add("signal-scanning");
    else if (normalized.includes("fail") || normalized.includes("off") || normalized.includes("no ")) signal.classList.add("signal-failed");
    else signal.classList.add("signal-offline");
  }
}

function clearBleScanList() {
  bleScanHits.clear();
  const list = document.querySelector("#bleScanList");
  if (list) {
    list.hidden = false;
    list.innerHTML = `<p class="hint">Scanning nearby BLE devices...</p>`;
  }
}

function renderBleScanList() {
  const list = document.querySelector("#bleScanList");
  if (!list) return;
  list.hidden = false;
  list.innerHTML = "";
  const hits = Array.from(bleScanHits.values())
    .sort((a, b) => (b.score - a.score) || (b.rssi - a.rssi));
  if (!hits.length) {
    const empty = document.createElement("p");
    empty.className = "hint";
    empty.textContent = "No nearby BLE devices seen yet.";
    list.append(empty);
    return;
  }
  hits.slice(0, 12).forEach((hit) => {
    const button = document.createElement("button");
    button.className = "ble-row";
    const isKnown = String(hit.address || "").toUpperCase() === KNOWN_WATCH_ADDRESS;
    const label = isKnown && !String(hit.label || "").toLowerCase().includes("gta-nav")
      ? `GTA-Nav watch direct`
      : hit.label;
    button.innerHTML = `<span><b>${label}</b><small>${hit.address}</small></span><span>${hit.rssi} dBm</span>`;
    button.addEventListener("click", () => {
      selectedWatchAddress = hit.address;
      localStorage.setItem("gtaNavSelectedWatch", selectedWatchAddress);
      setBleState("connecting");
      addDebug(`ui selected ${hit.label || "device"} ${hit.address}`);
      if (window.GtaNavNative?.connectDevice) {
        window.GtaNavNative.connectDevice(hit.address);
      }
    });
    list.append(button);
  });
}

window.nativeBleScanHit = (address, label, rssi, score) => {
  const state = String(document.querySelector("#bleState")?.textContent || "").toLowerCase();
  if (state.includes("linked") && !state.includes("not linked")) {
    return;
  }
  bleScanHits.set(address, { address, label: label || address, rssi, score });
  addDebug(`scan ${label || address} ${address} ${rssi}dBm score=${score}`);
  renderBleScanList();
};

function setGpsStatus(text) {
  document.querySelector("#gpsStatus").textContent = text;
  updateDebugSnapshot();
}

function updateCapabilityStatus() {
  if (window.GtaNavNative) {
    setGpsStatus("Native GPS ready");
    setBleState(window.GtaNavNative.getBridgeStatus ? window.GtaNavNative.getBridgeStatus() || "Offline" : "Offline");
    if (window.GtaNavNative.getDebugLog) addDebug(window.GtaNavNative.getDebugLog());
    return;
  }
  const secure = window.isSecureContext ? "secure" : "not secure";
  const gps = "geolocation" in navigator ? "GPS ok" : "no GPS API";
  const ble = "bluetooth" in navigator ? "BLE ok" : "no Web BLE";
  setGpsStatus(`${gps} / ${secure}`);
  document.querySelector("#bridgeStatus").textContent = ble;
}

function normalizeNumber(value) {
  return value.replace(/[^\d+]/g, "").replace(/^\+1/, "1");
}

function renderContacts() {
  const list = document.querySelector("#contactList");
  list.innerHTML = "";
  combinedContacts().forEach((contact) => {
    const row = document.createElement("div");
    row.className = "contact";
    const avatar = contact.avatar || initialsFor(contact.name);
    row.innerHTML = `<i class="contact-avatar">${escapeHtml(avatar)}</i><b>${escapeHtml(contact.name)}</b><small>${escapeHtml(contact.role || "Phone contact")}</small><span>${escapeHtml(contact.number || "No real-world number")}</span><div class="contact-actions"><button class="call-contact" type="button">Call</button><button class="contact-action" type="button">Action</button></div>`;
    row.querySelector(".call-contact").addEventListener("click", () => startCall(contact));
    const action = row.querySelector(".contact-action");
    action.addEventListener("click", async () => {
      if (contact.name === "Mechanic" || contact.name === "Garage") {
        showPanel("mechanic");
        document.querySelector("#mechanicRouteHint").textContent = "Want me to show you directions to the parked car?";
        return;
      }
      if (contact.name === "Mors Mutual-ish") {
        selectedConversationId = "insurance";
        localStorage.setItem("gtaNavTextContact", selectedConversationId);
        renderTexts();
        showPanel("texts");
        return;
      }
      if (contact.name === "Roadside" || contact.name === "GTA-Nav Support") {
        selectedConversationId = contact.name === "Roadside" ? "roadside" : "support";
        localStorage.setItem("gtaNavTextContact", selectedConversationId);
        renderTexts();
        showPanel("texts");
        return;
      }
      document.querySelector("#dialResponse").textContent = `${contact.name} is ready. Use Settings to add route data.`;
    });
    list.append(row);
  });
}

function initialsFor(value) {
  return String(value || "?")
    .split(/\s+/)
    .filter(Boolean)
    .slice(0, 2)
    .map((part) => part[0]?.toUpperCase() || "")
    .join("") || "?";
}

function renderGalleryItems() {
  const gallery = document.querySelector("#gallery");
  if (!gallery) return;
  gallery.innerHTML = "";
  linkedGalleryItems.slice(0, 24).forEach((item) => {
    const card = document.createElement("article");
    card.className = "message-card";
    card.innerHTML = `<b>${escapeHtml(item.album || "Phone Album")}</b><p>${escapeHtml(item.label || "Photo")}<br><small>${escapeHtml(item.date || "")}</small></p>`;
    gallery.append(card);
  });
}

function renderPhoneLinkStatus() {
  const node = document.querySelector("#phoneLinkStatus");
  if (node) {
    node.textContent = `${linkedPhoneContacts.length} contacts / ${linkedRecentCalls.length} calls linked`;
  }
  const galleryNode = document.querySelector("#galleryLinkStatus");
  if (galleryNode) {
    galleryNode.textContent = `${linkedGalleryItems.length} gallery items linked`;
  }
  const calendarNode = document.querySelector("#calendarLinked");
  if (calendarNode) {
    calendarNode.textContent = `${linkedCalendarEvents.length} linked`;
  }
}

function loadNativeJson(methodName, fallback) {
  try {
    if (!window.GtaNavNative || typeof window.GtaNavNative[methodName] !== "function") {
      return fallback;
    }
    const raw = window.GtaNavNative[methodName]();
    return raw ? JSON.parse(raw) : fallback;
  } catch (error) {
    addDebug(`${methodName} failed ${error.message || error}`);
    return fallback;
  }
}

async function linkPhoneContacts() {
  linkedPhoneContacts = loadNativeJson("getPhoneContactsJson", linkedPhoneContacts).slice(0, 40);
  localStorage.setItem("gtaNavPhoneContacts", JSON.stringify(linkedPhoneContacts));
  renderContacts();
  renderPhoneLinkStatus();
  await syncPhonebookToWatch();
}

async function linkRecentCalls() {
  linkedRecentCalls = loadNativeJson("getRecentCallsJson", linkedRecentCalls).slice(0, 24);
  localStorage.setItem("gtaNavRecentCalls", JSON.stringify(linkedRecentCalls));
  renderPhoneLinkStatus();
  await syncPhonebookToWatch();
}

async function linkPhoneGallery() {
  linkedGalleryItems = loadNativeJson("getGalleryItemsJson", linkedGalleryItems).slice(0, 24);
  localStorage.setItem("gtaNavGalleryItems", JSON.stringify(linkedGalleryItems));
  renderGalleryItems();
  renderPhoneLinkStatus();
  await syncGalleryToWatch();
}

async function linkPhoneCalendar() {
  linkedCalendarEvents = loadNativeJson("getCalendarEventsJson", linkedCalendarEvents).slice(0, 12);
  localStorage.setItem("gtaNavCalendarEvents", JSON.stringify(linkedCalendarEvents));
  renderCalendarPanel();
  renderPhoneLinkStatus();
  await syncCalendarEventsToWatch();
}

async function syncPhonebookToWatch() {
  const book = [...contacts.slice(0, 8), ...linkedPhoneContacts].slice(0, 12);
  for (let i = 0; i < book.length; i++) {
    const contact = book[i];
    await sendCommand(`CONTACT,${i},${csvSafe(contact.name)},${csvSafe(contact.number || "")},${csvSafe(contact.role || "Phone contact")}`);
  }
  const calls = linkedRecentCalls.slice(0, 8);
  for (let i = 0; i < calls.length; i++) {
    await sendCommand(`CALL,${i},${csvSafe(calls[i].name || calls[i].number || "Recent")},${csvSafe(calls[i].number || "")},${csvSafe(calls[i].kind || "call")}`);
  }
  addDebug("phonebook synced to watch");
}

async function syncGalleryToWatch() {
  const items = linkedGalleryItems.slice(0, 12);
  for (let i = 0; i < items.length; i++) {
    await sendCommand(`PHOTO,${i},${csvSafe(items[i].album || "Phone Album")},${csvSafe(items[i].label || "Photo")},${csvSafe(items[i].date || "")}`);
  }
  addDebug("gallery album synced to watch");
}

async function syncTextThreadsToWatch() {
  const rows = [];
  Object.entries(conversationHistory).forEach(([id, messages]) => {
    const agent = conversationAgents[id]?.name || id;
    const last = [...messages].reverse().find((message) => message.from === "agent" || message.from === "user");
    if (last) rows.push({ from: agent, text: last.text });
  });
  for (let i = 0; i < Math.min(rows.length, 8); i++) {
    await sendCommand(`MSG,${i},${csvSafe(rows[i].from)},${csvSafe(rows[i].text)}`);
  }
  addDebug("text threads synced to watch");
}

async function syncCalendarEventsToWatch() {
  const items = linkedCalendarEvents.slice(0, 4);
  for (let i = 0; i < items.length; i++) {
    const when = items[i].begin ? new Date(items[i].begin).toLocaleString([], { month: "short", day: "numeric", hour: "numeric", minute: "2-digit" }) : "";
    await sendCommand(`MSG,${4 + i},Calendar,${csvSafe(`${when} ${items[i].title || "Event"}`)}`);
  }
  if (items.length) addDebug("calendar events synced to watch");
}

function saveConversationHistory() {
  localStorage.setItem("gtaNavTextHistory", JSON.stringify(conversationHistory));
}

function parkedCarText() {
  if (!parkedFix) {
    return "No parked marker yet. Save the car first, then blame me with coordinates.";
  }
  return `Your ride was last tagged at ${parkedFix.latitude.toFixed(5)}, ${parkedFix.longitude.toFixed(5)}. Want me to show you directions?`;
}

function weatherText() {
  if (!latestWeather) {
    return "Weather is approved but not synced yet. Hit Sync Live Weather and I'll push it to the watch.";
  }
  return `${latestWeather.city}: ${Math.round(latestWeather.temp)}F and ${latestWeather.label}. High ${Math.round(latestWeather.high)}, low ${Math.round(latestWeather.low)}.`;
}

function randomLine(lines) {
  return lines[Math.floor(Math.random() * lines.length)];
}

function escapeHtml(value) {
  return String(value).replace(/[&<>"']/g, (char) => ({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    '"': "&quot;",
    "'": "&#39;",
  })[char]);
}

async function conversationReply(agentId, text) {
  const normalized = text.toLowerCase();
  if (agentId === "mechanic") {
    if (/where|park|car|ride|vehicle|route|direction/.test(normalized)) {
      if (parkedFix && /route|direction|show|take|go/.test(normalized)) {
        await routeParkedLocation();
      }
      return parkedCarText();
    }
    if (/muscle|sport|classic|truck|car|engine|fast/.test(normalized)) {
      return randomLine([
        "Muscle is for noise, sports are for corners, classics are for pretending leaks are personality.",
        "Best car is the one you can find after parking it. Revolutionary technology, apparently.",
        "A cheap sedan with a full tank beats a supercar with no route and one sad tire.",
      ]);
    }
  }
  if (agentId === "insurance") {
    if (/explode|exploded|crash|wreck|stolen|burn|total|claim|accident|hit/.test(normalized)) {
      return randomLine([
        "After careful review, it sounds like you're shit out of luck. Try another insurance company.",
        "Your claim has been denied with impressive speed. You're fucked, respectfully.",
        "We value your business, which is why we're keeping all of our money.",
      ]);
    }
    if (/help|cover|policy|insurance|pay/.test(normalized)) {
      return "Sure, I can help. First question: did anything expensive happen? Second question: can we pretend it did not?";
    }
  }
  if (agentId === "roadside" && /route|direction|where|park|car/.test(normalized)) {
    if (parkedFix) await routeParkedLocation();
    return parkedCarText();
  }
  if (/weather|rain|temp|forecast/.test(normalized)) return weatherText();
  if (/ble|connect|bridge|watch/.test(normalized)) {
    return `Bridge is ${document.querySelector("#bridgeStatus")?.textContent || "unknown"}. Mock bridge can spoof commands until the watch is in your hand.`;
  }
  if (/theme/.test(normalized)) {
    return `Phone is on ${themeChoices[document.body.dataset.theme]?.label || "Theme 1"}. Sync Watch Now mirrors it.`;
  }
  if (/clock|face|time/.test(normalized)) {
    return `Watch clock face is set to ${clockFaceChoices.find(([id]) => id === appSettings.clockFace)?.[1] || "Digital"}.`;
  }
  return randomLine(conversationAgents[agentId]?.fallback || conversationAgents.support.fallback);
}

function renderTexts() {
  const select = document.querySelector("#textContact");
  const transcript = document.querySelector("#textTranscript");
  if (!select || !transcript) return;
  if (!select.children.length) {
    Object.entries(conversationAgents).forEach(([id, agent]) => {
      const option = document.createElement("option");
      option.value = id;
      option.textContent = agent.name;
      select.append(option);
    });
  }
  select.value = selectedConversationId;
  transcript.innerHTML = "";
  const messages = conversationHistory[selectedConversationId] || [];
  messages.slice(-24).forEach((message) => {
    const bubble = document.createElement("div");
    bubble.className = `bubble ${message.from === "user" ? "user" : "agent"}`;
    bubble.innerHTML = message.from === "user"
      ? escapeHtml(message.text)
      : `<b>${conversationAgents[selectedConversationId]?.name || "Contact"}</b>${escapeHtml(message.text)}`;
    transcript.append(bubble);
  });
  transcript.scrollTop = transcript.scrollHeight;
}

const ambientTextLines = [
  ["mechanic", "Your car is still tagged. Probably. The map believes in itself."],
  ["insurance", "Reminder: we expect the unexpected, then bill you for the expected."],
  ["roadside", "If you hear grinding, turn the radio up or stop driving. Your call."],
  ["support", "Bridge heartbeat looks alive. If the watch ignores it, reconnect from Settings."],
];

const ambientMailTemplates = [
  ["Maze Bank-ish", "Route fee estimate posted. Walking remains tragically free."],
  ["Los Santos Customs-ish", "Paint, tires, repair, bad decisions. We finance all but the shame."],
  ["Safe House Registry", "Quick Save slot is ready whenever you are."],
  ["Eyefind-ish", "Your browser shortcut is ready. Search responsibly, or at least entertainingly."],
];

const ambientCallTemplates = [
  { name: "Mechanic", number: "328-555-0153", kind: "missed" },
  { name: "Mors Mutual-ish", number: "611-555-0149", kind: "missed" },
  { name: "Downtown Cab Co-ish", number: "323-555-5555", kind: "missed" },
  { name: "GTA-Nav Support", number: "+15550004282", kind: "missed" },
];

function renderMail() {
  const list = document.querySelector("#mailList");
  if (!list) return;
  list.innerHTML = "";
  simulatedMail.slice(0, 12).forEach((item) => {
    const card = document.createElement("article");
    card.className = "message-card";
    card.innerHTML = `<b>${escapeHtml(item.from)}</b><p>${escapeHtml(item.text)}<br><small>${new Date(item.at).toLocaleTimeString([], { hour: "numeric", minute: "2-digit" })}</small></p>`;
    list.append(card);
  });
}

function renderMissedCalls() {
  const list = document.querySelector("#missedCallList");
  if (!list) return;
  list.innerHTML = "";
  simulatedMissedCalls.slice(0, 6).forEach((call) => {
    const row = document.createElement("article");
    row.className = "message-card";
    row.innerHTML = `<b>Missed Call: ${escapeHtml(call.name)}</b><p>${escapeHtml(call.number || "No callback number")}<br><small>${new Date(call.at).toLocaleTimeString([], { hour: "numeric", minute: "2-digit" })}</small></p>`;
    list.append(row);
  });
}

async function generateAmbientActivity(force = false) {
  if (!force && Math.random() > 0.45) return;
  const type = Math.floor(Math.random() * 3);
  if (type === 0) {
    const [id, text] = randomLine(ambientTextLines);
    conversationHistory[id] ||= [];
    conversationHistory[id].push({ from: "agent", text });
    conversationHistory[id] = conversationHistory[id].slice(-40);
    saveConversationHistory();
    renderTexts();
    await syncTextThreadsToWatch();
  } else if (type === 1) {
    const [from, text] = randomLine(ambientMailTemplates);
    simulatedMail.unshift({ from, text, at: Date.now() });
    simulatedMail = simulatedMail.slice(0, 24);
    localStorage.setItem("gtaNavSimMail", JSON.stringify(simulatedMail));
    renderMail();
  } else {
    const call = { ...randomLine(ambientCallTemplates), at: Date.now() };
    simulatedMissedCalls.unshift(call);
    simulatedMissedCalls = simulatedMissedCalls.slice(0, 16);
    linkedRecentCalls = [call, ...linkedRecentCalls].slice(0, 24);
    localStorage.setItem("gtaNavSimMissedCalls", JSON.stringify(simulatedMissedCalls));
    localStorage.setItem("gtaNavRecentCalls", JSON.stringify(linkedRecentCalls));
    renderMissedCalls();
    renderPhoneLinkStatus();
    await syncPhonebookToWatch();
  }
}

async function sendTextMessage() {
  const input = document.querySelector("#textInput");
  const text = input?.value.trim();
  if (!text) return;
  conversationHistory[selectedConversationId] ||= [];
  conversationHistory[selectedConversationId].push({ from: "user", text });
  if (input) input.value = "";
  renderTexts();
  const reply = await conversationReply(selectedConversationId, text);
  conversationHistory[selectedConversationId].push({ from: "agent", text: reply });
  saveConversationHistory();
  renderTexts();
  await syncTextThreadsToWatch();
}

function selectedDateTime() {
  const now = new Date();
  const dateText = appSettings.manualDate || now.toISOString().slice(0, 10);
  const timeText = appSettings.manualTime || now.toTimeString().slice(0, 5);
  const yearText = String(appSettings.manualYear || dateText.slice(0, 4) || now.getFullYear());
  const normalizedDate = `${yearText.padStart(4, "0")}${dateText.slice(4)}`;
  const local = new Date(`${normalizedDate}T${timeText}:00`);
  return Number.isNaN(local.getTime()) ? now : local;
}

function timezoneOffsetMinutes(date, timeZone) {
  try {
    const parts = new Intl.DateTimeFormat("en-US", {
      timeZone,
      year: "numeric",
      month: "2-digit",
      day: "2-digit",
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
      hour12: false,
    }).formatToParts(date).reduce((acc, part) => {
      acc[part.type] = part.value;
      return acc;
    }, {});
    const zoned = Date.UTC(
      Number(parts.year),
      Number(parts.month) - 1,
      Number(parts.day),
      Number(parts.hour),
      Number(parts.minute),
      Number(parts.second),
    );
    return Math.round((zoned - date.getTime()) / 60000);
  } catch {
    return -date.getTimezoneOffset();
  }
}

function gtaMockBridgeEnabled() {
  return Boolean(appSettings.mockBridge);
}

  function rememberCommand(command, mode = "mock") {
    const event = { command, mode, sentAt: Date.now() };
    commandHistory.unshift(event);
    commandHistory = commandHistory.slice(0, 80);
    localStorage.setItem("gtaNavCommandHistory", JSON.stringify(commandHistory));
    localStorage.setItem("gtaNavLastWatchCommand", JSON.stringify(event));
    if (window.BroadcastChannel) {
      const channel = new BroadcastChannel("gta-nav-watch");
      channel.postMessage(event);
      channel.close();
    }
    updateDebugSnapshot();
  }

async function mockSendCommand(command) {
  lastGpsCommand = command;
  document.querySelector("#commandBox").value = command;
  rememberCommand(command, "mock");
  setBleState("mock linked");
  setGpsStatus(command.startsWith("HEAD,") ? "Mock heading sent" : "Mock watch command");
  addDebug(`mock ${command}`);
  return true;
}

function commandDateText(date) {
  return date.toISOString().slice(0, 10);
}

function renderSettings() {
  const tzSelect = document.querySelector("#settingsTimezone");
  if (tzSelect && !tzSelect.children.length) {
    const current = appSettings.timezone || Intl.DateTimeFormat().resolvedOptions().timeZone || "America/New_York";
    const options = timezoneOptions.includes(current) ? timezoneOptions : [current, ...timezoneOptions];
    options.forEach((zone) => {
      const option = document.createElement("option");
      option.value = zone;
      option.textContent = zone.replace("_", " ");
      tzSelect.append(option);
    });
  }
  const clockSelect = document.querySelector("#settingsClockFace");
  if (clockSelect && !clockSelect.children.length) {
    clockFaceChoices.forEach(([id, label]) => {
      const option = document.createElement("option");
      option.value = id;
      option.textContent = label;
      clockSelect.append(option);
    });
  }
  const pairs = [
    ["#settingsHome", "homeAddress"],
    ["#settingsWork", "workAddress"],
    ["#settingsTimezone", "timezone"],
    ["#settingsDate", "manualDate"],
    ["#settingsTime", "manualTime"],
    ["#settingsYear", "manualYear"],
    ["#settingsAlarmTime", "alarmTime"],
    ["#settingsAlarmLabel", "alarmLabel"],
    ["#settingsClockFace", "clockFace"],
    ["#settingsVehicle", "vehicle"],
    ["#settingsFuelType", "fuelType"],
    ["#settingsTankGallons", "tankGallons"],
    ["#settingsMpg", "mpg"],
    ["#settingsGasPrice", "gasPrice"],
  ];
  pairs.forEach(([selector, key]) => {
    const input = document.querySelector(selector);
    if (input) input.value = appSettings[key] || "";
  });
  const alarmEnabled = document.querySelector("#settingsAlarmEnabled");
  if (alarmEnabled) alarmEnabled.checked = Boolean(appSettings.alarmEnabled);
  const mockBridge = document.querySelector("#settingsMockBridge");
  if (mockBridge) mockBridge.checked = Boolean(appSettings.mockBridge);
  renderBridgeMode();
  renderWeatherStatus();
  renderCalendarPanel();
}

async function saveAppSettings() {
  appSettings = {
    homeAddress: document.querySelector("#settingsHome")?.value.trim() || "",
    workAddress: document.querySelector("#settingsWork")?.value.trim() || "",
    timezone: document.querySelector("#settingsTimezone")?.value || "America/New_York",
    manualDate: document.querySelector("#settingsDate")?.value || "",
    manualTime: document.querySelector("#settingsTime")?.value || "",
    manualYear: document.querySelector("#settingsYear")?.value.trim() || "",
    alarmTime: document.querySelector("#settingsAlarmTime")?.value || "07:00",
    alarmLabel: document.querySelector("#settingsAlarmLabel")?.value.trim() || "Alarm",
    clockFace: document.querySelector("#settingsClockFace")?.value || "digital",
    alarmEnabled: Boolean(document.querySelector("#settingsAlarmEnabled")?.checked),
    mockBridge: Boolean(document.querySelector("#settingsMockBridge")?.checked),
    vehicle: document.querySelector("#settingsVehicle")?.value.trim() || "",
    fuelType: document.querySelector("#settingsFuelType")?.value.trim() || "Regular",
    tankGallons: Number.parseFloat(document.querySelector("#settingsTankGallons")?.value) || 14,
    mpg: Number.parseFloat(document.querySelector("#settingsMpg")?.value) || 24,
    gasPrice: Number.parseFloat(document.querySelector("#settingsGasPrice")?.value) || 3.49,
  };
  localStorage.setItem("gtaNavSettings", JSON.stringify(appSettings));
  renderBank();
  renderCalendarPanel();
  renderBridgeMode();
  await syncWatchSettings();
}

async function syncWatchSettings() {
  const date = selectedDateTime();
  const offset = timezoneOffsetMinutes(date, appSettings.timezone);
  const commands = [
    `TIME,${Math.floor(date.getTime() / 1000)},${offset},${csvSafe(appSettings.timezone)},${commandDateText(date)}`,
    `PROFILE,${csvSafe(appSettings.homeAddress || "Safe House")},${csvSafe(appSettings.workAddress || "Work")}`,
    `ALARM,${csvSafe(appSettings.alarmTime || "07:00")},${csvSafe(appSettings.alarmLabel || "Alarm")},${appSettings.alarmEnabled ? "1" : "0"}`,
    `VEHICLE,${csvSafe(appSettings.vehicle || "Vehicle")},${Number(appSettings.tankGallons).toFixed(1)},${Number(appSettings.mpg).toFixed(1)},${csvSafe(appSettings.fuelType)},${Number(appSettings.gasPrice).toFixed(2)}`,
    `THEME,${themeChoices[document.body.dataset.theme]?.watch || "theme1"}`,
    `CLOCK,${appSettings.clockFace || "digital"}`,
  ];
  for (const command of commands) {
    await sendCommand(command);
  }
  if (latestWeather && Date.now() - latestWeather.updatedAt < 30 * 60 * 1000) {
    await sendWeatherToWatch(latestWeather);
  }
  await syncPhonebookToWatch();
  await syncTextThreadsToWatch();
  await syncGalleryToWatch();
  await syncCalendarEventsToWatch();
  addDebug("settings synced to watch");
}

function renderBridgeMode() {
  const node = document.querySelector("#settingsMockStatus");
  if (node) node.textContent = appSettings.mockBridge ? "Mock linked" : "Real BLE";
  if (appSettings.mockBridge) {
    setBleState("mock linked");
  }
}

function renderWeatherStatus() {
  const node = document.querySelector("#settingsWeatherStatus");
  if (!node) return;
  if (!latestWeather) {
    node.textContent = "Test ready";
    return;
  }
  const age = Math.max(0, Math.round((Date.now() - latestWeather.updatedAt) / 60000));
  node.textContent = `${Math.round(latestWeather.temp)}F ${latestWeather.label} / ${age}m`;
}

async function sendWeatherToWatch(weather) {
  if (!weather) return false;
  return sendCommand(`WEATHER,${Math.round(weather.temp)},${Math.round(weather.high)},${Math.round(weather.low)},${csvSafe(weather.label)},${csvSafe(weather.city)}`);
}

async function syncLiveWeather() {
  latestWeather = {
    temp: 72,
    high: 78,
    low: 61,
    label: "Test Weather",
    city: lastTownName || "Phone Weather",
    updatedAt: Date.now(),
  };
  localStorage.setItem("gtaNavWeather", JSON.stringify(latestWeather));
  renderWeatherStatus();
  await sendWeatherToWatch(latestWeather);
  addDebug("weather test synced to watch");
}

function renderCalendarPanel() {
  const date = selectedDateTime();
  const dateFmt = new Intl.DateTimeFormat("en-US", {
    weekday: "long",
    month: "short",
    day: "numeric",
    year: "numeric",
    timeZone: appSettings.timezone || "America/New_York",
  });
  const timeFmt = new Intl.DateTimeFormat("en-US", {
    hour: "numeric",
    minute: "2-digit",
    timeZone: appSettings.timezone || "America/New_York",
  });
  const dateNode = document.querySelector("#calendarDate");
  const dowNode = document.querySelector("#calendarDow");
  const tzNode = document.querySelector("#calendarTimezone");
  const homeNode = document.querySelector("#calendarHome");
  const workNode = document.querySelector("#calendarWork");
  const alarmNode = document.querySelector("#calendarAlarm");
  if (dateNode) dateNode.textContent = `${dateFmt.format(date)} ${timeFmt.format(date)}`;
  if (dowNode) dowNode.textContent = new Intl.DateTimeFormat("en-US", { weekday: "short", timeZone: appSettings.timezone || "America/New_York" }).format(date);
  if (tzNode) tzNode.textContent = appSettings.timezone || "Phone timezone";
  if (homeNode) homeNode.textContent = appSettings.homeAddress || "Not set";
  if (workNode) workNode.textContent = appSettings.workAddress || "Not set";
  if (alarmNode) alarmNode.textContent = appSettings.alarmEnabled ? `${appSettings.alarmTime} ${appSettings.alarmLabel}` : "Off";
  const calendarList = document.querySelector("#calendarEvents");
  if (calendarList) {
    calendarList.innerHTML = "";
    linkedCalendarEvents.slice(0, 6).forEach((event) => {
      const card = document.createElement("article");
      card.className = "message-card";
      const when = event.begin
        ? new Date(event.begin).toLocaleString([], { weekday: "short", month: "short", day: "numeric", hour: "numeric", minute: "2-digit" })
        : "Upcoming";
      card.innerHTML = `<b>${escapeHtml(event.title || "Calendar Event")}</b><p>${escapeHtml(when)}<br><small>${escapeHtml(event.location || "No location")}</small></p>`;
      calendarList.append(card);
    });
  }
}

function renderBank() {
  const now = Date.now();
  const weekAgo = now - 7 * 24 * 60 * 60 * 1000;
  const weekly = tripHistory
    .filter((trip) => trip.savedAt >= weekAgo)
    .reduce((sum, trip) => sum + (Number(trip.cost) || 0), 0);
  const last = tripHistory[0];
  const current = last ? Number(last.cost) || 0 : 0;
  const currentEl = document.querySelector("#bankCurrent");
  const weeklyEl = document.querySelector("#bankWeekly");
  const savedEl = document.querySelector("#bankSaved");
  const lastEl = document.querySelector("#bankLast");
  const modeEl = document.querySelector("#bankMode");
  const distanceEl = document.querySelector("#bankDistance");
  const fuelEl = document.querySelector("#bankFuel");
  const stopGoEl = document.querySelector("#bankStopGo");
  const tollsEl = document.querySelector("#bankTolls");
  if (currentEl) currentEl.textContent = dollars(current);
  if (weeklyEl) weeklyEl.textContent = dollars(weekly);
  if (savedEl) savedEl.textContent = String(tripHistory.length);
  if (lastEl) lastEl.textContent = last ? `${last.name} ${Number(last.miles || 0).toFixed(1)} mi` : "None";
  if (modeEl) modeEl.textContent = last ? (last.mode === "walk" ? "Walk" : "Drive") : (appSettings.travelMode === "walk" ? "Walk" : "Drive");
  if (distanceEl) distanceEl.textContent = last ? `${Number(last.miles || 0).toFixed(1)} mi` : "0.0 mi";
  if (fuelEl) fuelEl.textContent = dollars(last?.mode === "walk" ? 0 : last?.fuel || 0);
  if (stopGoEl) stopGoEl.textContent = dollars(last?.mode === "walk" ? 0 : last?.stopGo || 0);
  if (tollsEl) tollsEl.textContent = dollars(last?.mode === "walk" ? 0 : last?.tolls || 0);
}

function csvSafe(value) {
  return String(value || "").replace(/,/g, " ").trim();
}

function renderLandmarkTypes() {
  const select = document.querySelector("#landmarkType");
  const grid = document.querySelector("#landmarkIconGrid");
  select.innerHTML = "";
  if (grid) grid.innerHTML = "";
  landmarkTypes.forEach((type) => {
    const option = document.createElement("option");
    option.value = type.id;
    option.textContent = `${type.glyph} ${type.label}`;
    select.append(option);
    if (grid) {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "landmark-icon-choice";
      button.dataset.type = type.id;
      button.style.setProperty("--icon-color", type.color);
      button.textContent = type.glyph;
      button.title = type.label;
      button.addEventListener("click", () => setLandmarkType(type.id));
      grid.append(button);
    }
  });
  select.addEventListener("change", () => setLandmarkType(select.value));
  setLandmarkType(selectedLandmarkTypeId);
}

function setLandmarkType(typeId) {
  selectedLandmarkTypeId = landmarkTypeFor(typeId).id;
  const select = document.querySelector("#landmarkType");
  if (select) select.value = selectedLandmarkTypeId;
  document.querySelectorAll(".landmark-icon-choice").forEach((button) => {
    button.classList.toggle("selected", button.dataset.type === selectedLandmarkTypeId);
  });
}

function renderLandmarks() {
  const list = document.querySelector("#landmarkList");
  if (!list) return;
  list.innerHTML = "";
  if (!savedLandmarks.length) {
    const empty = document.createElement("p");
    empty.className = "hint";
    empty.textContent = "No custom landmarks yet. Save your current spot as a GTA location.";
    list.append(empty);
    return;
  }
  savedLandmarks.forEach((landmark, index) => {
    const branded = normalizeLandmarkBrand(landmark);
    const type = landmarkTypeFor(branded.type);
    const row = document.createElement("div");
    row.className = "landmark-row";
    row.innerHTML = `<span class="landmark-icon" style="background:${type.color}">${type.glyph}</span><span><b>${branded.name}</b><small>${type.label} / ${landmark.latitude.toFixed(5)}, ${landmark.longitude.toFixed(5)}</small></span><button type="button" class="mini-action route">Route</button><button type="button" class="mini-action send">Send</button><button type="button" class="mini-action remove">X</button>`;
    row.querySelector(".route").addEventListener("click", () => routeToLandmark(branded, index));
    row.querySelector(".send").addEventListener("click", () => sendLandmarkToWatch(branded, index));
    row.querySelector(".remove").addEventListener("click", () => {
      savedLandmarks.splice(index, 1);
      saveLandmarks();
    });
    list.append(row);
  });
}

async function sendLandmarkToWatch(landmark, index) {
  const branded = normalizeLandmarkBrand(landmark);
  const type = landmarkTypeFor(branded.type);
  await sendCommand(`POI,${index % 12},${csvSafe(type.id)},${csvSafe(branded.name || type.label)},${branded.latitude.toFixed(6)},${branded.longitude.toFixed(6)}`);
}

async function syncLandmarksToWatch() {
  const landmarks = visibleLandmarksForMap().slice(0, 12);
  for (let i = 0; i < landmarks.length; i++) {
    await sendLandmarkToWatch(landmarks[i], i);
  }
}

function saveLandmarks() {
  localStorage.setItem("gtaNavLandmarks", JSON.stringify(savedLandmarks.slice(0, 48)));
  renderLandmarks();
  drawPauseMap();
}

function saveTripHistory() {
  tripHistory = tripHistory.slice(0, 80);
  localStorage.setItem("gtaNavTripHistory", JSON.stringify(tripHistory));
  renderBank();
}

function tripCostForMiles(miles) {
  const mpg = Number.parseFloat(appSettings.mpg) || 0;
  const gasPrice = Number.parseFloat(appSettings.gasPrice) || 0;
  if (mpg <= 0 || miles <= 0) return 0;
  return (miles / mpg) * gasPrice;
}

function tripBreakdownForMiles(miles, mode = appSettings.travelMode || "drive") {
  const driving = mode !== "walk";
  const fuel = driving ? tripCostForMiles(miles) : 0;
  const stopGo = driving ? Math.max(0, miles * 0.18) : 0;
  const tolls = driving && miles > 35 ? Math.ceil(miles / 40) * 1.25 : 0;
  return {
    mode: driving ? "drive" : "walk",
    miles,
    fuel,
    stopGo,
    tolls,
    total: driving ? fuel + stopGo + tolls : 0,
  };
}

function routeMilesTo(target) {
  const start = liveFix || gpsOrigin || mapBiasFix();
  return distanceMeters(start, target) / 1609.344 * 1.18;
}

async function routeToLandmark(landmark, index = 0) {
  const branded = normalizeLandmarkBrand(landmark);
  activeDestination = branded;
  const miles = routeMilesTo(branded);
  const breakdown = tripBreakdownForMiles(miles);
  tripHistory.unshift({
    name: branded.name || landmarkTypeFor(branded.type).label,
    miles,
    cost: breakdown.total,
    mode: breakdown.mode,
    fuel: breakdown.fuel,
    stopGo: breakdown.stopGo,
    tolls: breakdown.tolls,
    savedAt: Date.now(),
  });
  saveTripHistory();
  drawPauseMap();
  await sendLandmarkToWatch(branded, index);
  await sendCommand(`DEST,${branded.latitude.toFixed(6)},${branded.longitude.toFixed(6)},${csvSafe(branded.name || "Waypoint")}`);
  await sendCommand(`MODE,${breakdown.mode}`);
  await sendCommand(`TRIP,${csvSafe(branded.name || "Waypoint")},${breakdown.mode},${miles.toFixed(1)},${breakdown.tolls.toFixed(2)},${Number(appSettings.gasPrice || 0).toFixed(2)}`);
  document.querySelector("#gpsReadout").textContent =
    `Route set: ${branded.name} / ${miles.toFixed(1)} mi / ${breakdown.mode === "walk" ? "walk free" : `drive ${dollars(breakdown.total)}`}`;
  showPanel("gps");
}

function dollars(value) {
  return `$${(Number(value) || 0).toFixed(2)}`;
}

function openNativeBrowser(url) {
  if (window.GtaNavNative?.openBrowser) {
    window.GtaNavNative.openBrowser(url);
  } else {
    window.open(url, "_blank");
  }
}

async function setTravelMode(mode) {
  appSettings.travelMode = mode === "walk" ? "walk" : "drive";
  localStorage.setItem("gtaNavSettings", JSON.stringify(appSettings));
  renderBank();
  await sendCommand(`MODE,${appSettings.travelMode}`);
}

function fillLandmarkFromCurrent() {
  const fix = liveFix || parkedFix || gpsOrigin;
  if (!fix) {
    document.querySelector("#gpsReadout").textContent = "Start GPS first, then tap Use Current.";
    return;
  }
  document.querySelector("#landmarkLat").value = fix.latitude.toFixed(6);
  document.querySelector("#landmarkLon").value = fix.longitude.toFixed(6);
}

async function addLandmark() {
  const typeId = selectedLandmarkTypeId || document.querySelector("#landmarkType").value;
  const type = landmarkTypeFor(typeId);
  const lat = Number.parseFloat(document.querySelector("#landmarkLat").value);
  const lon = Number.parseFloat(document.querySelector("#landmarkLon").value);
  if (!Number.isFinite(lat) || !Number.isFinite(lon)) {
    fillLandmarkFromCurrent();
    return;
  }
  const rawName = document.querySelector("#landmarkName").value.trim() || type.label;
  const branded = parodyPlaceFor(rawName, typeId);
  const landmark = { name: branded.name || rawName, realName: rawName, type: branded.type || typeId, latitude: lat, longitude: lon, savedAt: Date.now() };
  savedLandmarks.unshift(landmark);
  savedLandmarks = savedLandmarks.slice(0, 48);
  document.querySelector("#landmarkName").value = "";
  saveLandmarks();
  await syncLandmarksToWatch();
}

function normalizeStreetName(value) {
  return String(value || "")
    .toLowerCase()
    .replace(/\b(avenue|ave\.?)\b/g, "ave")
    .replace(/\b(street|st\.?)\b/g, "st")
    .replace(/\b(road|rd\.?)\b/g, "rd")
    .replace(/\b(drive|dr\.?)\b/g, "dr")
    .replace(/\b(lane|ln\.?)\b/g, "ln")
    .replace(/\b(boulevard|blvd\.?)\b/g, "blvd")
    .replace(/^\d+\s+/, "")
    .replace(/[^a-z0-9]+/g, " ")
    .trim();
}

function offlineGeocodeFromMap(address, bias) {
  if (!mapData?.features) return null;
  const wanted = normalizeStreetName(address);
  if (!wanted) return null;
  let best = null;
  for (const feature of mapData.features) {
    if (feature.kind !== "road" || !feature.name || !feature.points?.length) continue;
    const roadName = normalizeStreetName(feature.name);
    if (!roadName || (!roadName.includes(wanted) && !wanted.includes(roadName))) continue;
    for (const [lat, lon] of feature.points) {
      const candidate = { latitude: lat, longitude: lon };
      const distance = distanceMeters(bias, candidate);
      if (!best || distance < best.distance) {
        best = {
          lat: String(lat),
          lon: String(lon),
          display_name: `${feature.name} / offline OSM match`,
          distance,
          queryIndex: 99,
        };
      }
    }
  }
  return best;
}

function localAddressSuggestions(query) {
  if (!mapData?.features) return [];
  const wanted = normalizeStreetName(query);
  if (!wanted) return [];
  const names = new Set();
  for (const feature of mapData.features) {
    if (feature.kind !== "road" || !feature.name) continue;
    const roadName = normalizeStreetName(feature.name);
    if (roadName.includes(wanted)) {
      names.add(feature.name);
    }
    if (names.size >= 8) break;
  }
  return Array.from(names).map((name) => `${name}, Lewiston, Maine`);
}

async function updateAddressSuggestions() {
  const input = document.querySelector("#landmarkAddress");
  const list = document.querySelector("#addressSuggestions");
  if (!input || !list) return;
  const value = input.value.trim();
  list.innerHTML = "";
  if (value.length < 3) return;
  const suggestions = new Set(localAddressSuggestions(value));
  try {
    const bias = mapBiasFix();
    const params = new URLSearchParams({
      q: value,
      lat: bias.latitude.toFixed(6),
      lon: bias.longitude.toFixed(6),
    });
    const res = await fetch(`/api/geocode?${params.toString()}`);
    if (res.ok) {
      const payload = await res.json();
      (payload.results || []).slice(0, 8).forEach((hit) => suggestions.add(hit.display_name));
    }
  } catch (error) {
    // Local street-name suggestions still work offline.
  }
  Array.from(suggestions).slice(0, 10).forEach((suggestion) => {
    const option = document.createElement("option");
    option.value = suggestion;
    list.append(option);
  });
}

function bindAddressAutocomplete() {
  const input = document.querySelector("#landmarkAddress");
  if (!input) return;
  input.addEventListener("input", () => {
    clearTimeout(addressSuggestTimer);
    addressSuggestTimer = setTimeout(updateAddressSuggestions, 250);
  });
}

async function geocodeAddressToFix(address) {
  const bias = mapBiasFix();
  const params = new URLSearchParams({
    q: address,
    lat: bias.latitude.toFixed(6),
    lon: bias.longitude.toFixed(6),
  });
  try {
    const res = await fetch(`/api/geocode?${params.toString()}`);
    if (res.ok) {
      const payload = await res.json();
      const hit = (payload.results || [])[0];
      if (hit) {
        const label = hit.display_name?.split(",")[0] || address;
        const branded = parodyPlaceFor(label, "waypoint");
        return {
          latitude: Number.parseFloat(hit.lat),
          longitude: Number.parseFloat(hit.lon),
          label: branded.name || label,
          type: branded.type,
          displayName: hit.display_name || address,
        };
      }
    }
  } catch (error) {
    // Fall through to offline map-street matching.
  }
  const offlineHit = offlineGeocodeFromMap(address, bias);
  if (!offlineHit) return null;
  return {
    latitude: Number.parseFloat(offlineHit.lat),
    longitude: Number.parseFloat(offlineHit.lon),
    label: parodyPlaceFor(offlineHit.display_name.split(" / ")[0], "waypoint").name,
    displayName: offlineHit.display_name,
  };
}

async function lookupAddressForLandmark() {
  const address = document.querySelector("#landmarkAddress").value.trim();
  if (!address) {
    document.querySelector("#gpsReadout").textContent = "Type an address or place first.";
    return;
  }
  document.querySelector("#gpsReadout").textContent = "Finding address...";
  try {
    const bias = mapBiasFix();
    let hits = [];
    try {
      const localParams = new URLSearchParams({
        q: address,
        lat: bias.latitude.toFixed(6),
        lon: bias.longitude.toFixed(6),
      });
      const localRes = await fetch(`/api/geocode?${localParams.toString()}`);
      if (localRes.ok) {
        const localPayload = await localRes.json();
        hits = Array.isArray(localPayload.results) ? localPayload.results : [];
      }
    } catch (localError) {
      hits = [];
    }
    const hasRegion = /,\s*|\b(me|maine|nh|new hampshire|ma|massachusetts|vt|vermont)\b/i.test(address);
    const queries = hasRegion ? [address] : [
      `${address}, Lewiston, Maine`,
      `${address}, Maine`,
      address,
    ];
    for (const [queryIndex, query] of hits.length ? [] : queries.entries()) {
      try {
        const params = new URLSearchParams({
          q: query,
          format: "jsonv2",
          limit: "8",
          addressdetails: "1",
          countrycodes: "us",
          viewbox: `${bias.longitude - 0.45},${bias.latitude + 0.35},${bias.longitude + 0.45},${bias.latitude - 0.35}`,
          bounded: "0",
        });
        const res = await fetch(`https://nominatim.openstreetmap.org/search?${params.toString()}`, {
          headers: { "Accept": "application/json" },
        });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const results = await res.json();
        results.forEach((item) => {
          const hitFix = {
            latitude: Number.parseFloat(item.lat),
            longitude: Number.parseFloat(item.lon),
          };
          if (!Number.isFinite(hitFix.latitude) || !Number.isFinite(hitFix.longitude)) return;
          hits.push({
            ...item,
            distance: distanceMeters(bias, hitFix),
            queryIndex,
          });
        });
        if (hits.some((hit) => hit.distance < 30000)) break;
      } catch (onlineError) {
        break;
      }
    }
    if (!hits.length) {
      const offlineHit = offlineGeocodeFromMap(address, bias);
      if (offlineHit) hits.push(offlineHit);
    }
    if (!hits.length) throw new Error("No nearby match");
    hits.sort((a, b) => (a.distance + a.queryIndex * 50000) - (b.distance + b.queryIndex * 50000));
    const hit = hits[0];
    if (hit.distance > 160934) {
      throw new Error("No Maine-area match. Add city/state or tap Use Current.");
    }
    document.querySelector("#landmarkLat").value = Number.parseFloat(hit.lat).toFixed(6);
    document.querySelector("#landmarkLon").value = Number.parseFloat(hit.lon).toFixed(6);
    if (!document.querySelector("#landmarkName").value.trim()) {
      document.querySelector("#landmarkName").value = parodyPlaceFor(hit.display_name.split(",")[0], "waypoint").name;
    }
    document.querySelector("#gpsReadout").textContent = `Found ${hit.display_name}`;
  } catch (error) {
    document.querySelector("#gpsReadout").textContent = `Address lookup failed: ${error.message || error}`;
  }
}

async function routeWorkAddress() {
  const address = appSettings.workAddress;
  if (!address) {
    showPanel("settings");
    document.querySelector("#gpsReadout").textContent = "Set your work address in Settings first.";
    return;
  }
  document.querySelector("#gpsReadout").textContent = "Finding work address...";
  const hit = await geocodeAddressToFix(address);
  if (!hit) {
    document.querySelector("#gpsReadout").textContent = "Work address lookup failed.";
    return;
  }
  const landmark = {
    name: "Work",
    type: "job",
    latitude: hit.latitude,
    longitude: hit.longitude,
    savedAt: Date.now(),
  };
  await routeToLandmark(landmark, 10);
}

async function routeParkedLocation() {
  if (!parkedFix) {
    await savePark();
    if (!parkedFix) return;
  }
  await routeToLandmark({
    name: "Parked Car",
    type: "parking",
    latitude: parkedFix.latitude,
    longitude: parkedFix.longitude,
    heading: parkedFix.heading,
  }, 11);
}

function townNameForFix(fix) {
  if (!fix) return "Lewiston / Auburn";
  return fix.longitude < -70.218 ? "Auburn" : "Lewiston";
}

function playDialTone() {
  try {
    const AudioContext = window.AudioContext || window.webkitAudioContext;
    if (!AudioContext) return;
    const ctx = new AudioContext();
    const gain = ctx.createGain();
    gain.gain.setValueAtTime(0.0001, ctx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.12, ctx.currentTime + 0.02);
    gain.gain.exponentialRampToValueAtTime(0.0001, ctx.currentTime + 1.1);
    gain.connect(ctx.destination);
    [350, 440].forEach((freq) => {
      const osc = ctx.createOscillator();
      osc.type = "sine";
      osc.frequency.value = freq;
      osc.connect(gain);
      osc.start();
      osc.stop(ctx.currentTime + 1.12);
    });
    setTimeout(() => ctx.close(), 1300);
  } catch (error) {
    addDebug(`dial tone skipped ${error.message || error}`);
  }
}

function callDialogFor(contact) {
  const name = contact?.name || "Unknown";
  if (/mechanic/i.test(name)) return "Dialing garage dispatch. He says your car is where you last saved it.";
  if (/mors/i.test(name)) return "Claims line ringing. Expect empathy, then paperwork, then disappointment.";
  if (/cab/i.test(name)) return "Cab office ringing. They can route you, not teleport you.";
  if (/maze/i.test(name)) return "Bank line ringing. Route costs are waiting in the Bank app.";
  return `Dialing ${name}.`;
}

function startCall(contact) {
  const number = contact?.number || document.querySelector("#dialInput")?.value || "";
  if (!normalizeNumber(number)) {
    document.querySelector("#dialResponse").textContent = `${contact?.name || "This contact"} has no real-world number. No call was placed.`;
    return;
  }
  playDialTone();
  document.querySelector("#dialResponse").textContent = callDialogFor(contact || { name: number });
  if (window.GtaNavNative?.openDialer) {
    window.GtaNavNative.openDialer(number);
  } else {
    window.location.href = `tel:${number}`;
  }
}

function handleDial() {
  const input = document.querySelector("#dialInput");
  const raw = input.value.trim();
  const normalized = normalizeNumber(raw);
  const known = combinedContacts().find((contact) => normalizeNumber(contact.number) === normalized);
  const secret = secretResponses[normalized];
  if (known) {
    startCall(known);
    return;
  }
  if (secret) {
    document.querySelector("#dialResponse").textContent = secret;
    return;
  }
  document.querySelector("#dialResponse").textContent = "No contact found. Add it to the GTA-Nav contact pack.";
}

async function sendCommand(command) {
  lastGpsCommand = command;
  document.querySelector("#commandBox").value = command;
  addDebug(`send ${command.split(",", 1)[0]} ${command.length}b`);
  updateDebugSnapshot();
  if (gtaMockBridgeEnabled()) {
    return mockSendCommand(command);
  }
  if (window.GtaNavNative?.sendCommand) {
    rememberCommand(command, "native");
    window.GtaNavNative.sendCommand(command);
    return true;
  }
  if (!rxCharacteristic) {
    setBleState(window.GtaNavNative ? "Native local" : "GPS local only");
    return false;
  }
  const payload = new TextEncoder().encode(command.endsWith("\n") ? command : `${command}\n`);
  try {
    await rxCharacteristic.writeValue(payload);
    rememberCommand(command, "webble");
    setGpsStatus(command.startsWith("HEAD,") ? "Sent heading" : "Sent to watch");
    setBleState("linked");
    return true;
  } catch (error) {
    rxCharacteristic = null;
    setBleState("send failed");
    addDebug(`web ble send failed ${error.message || error}`);
    return false;
  }
}

async function connectBle() {
  if (window.GtaNavNative) {
    clearBleScanList();
    setBleState("scanning");
    addDebug("native connect requested");
    window.GtaNavNative.connectWatch();
    return;
  }
  if (!window.isSecureContext) {
    setBleState("not secure");
    document.querySelector("#bridgeStatus").textContent = "Use HTTPS URL";
    return;
  }
  if (!navigator.bluetooth) {
    setBleState("no web ble");
    return;
  }
  try {
    setBleState("pairing");
    const device = await navigator.bluetooth.requestDevice({
      filters: [{ namePrefix: "GTA-Nav" }],
      optionalServices: [SERVICE_UUID],
    });
    device.addEventListener("gattserverdisconnected", () => {
      rxCharacteristic = null;
      setBleState("offline");
    });
    const server = await device.gatt.connect();
    const service = await server.getPrimaryService(SERVICE_UUID);
    rxCharacteristic = await service.getCharacteristic(RX_UUID);
    setBleState("linked");
    await syncLandmarksToWatch();
  } catch (error) {
    setBleState("BLE failed");
    document.querySelector("#bridgeStatus").textContent = error.message || "BLE failed";
    addDebug(`web ble failed ${error.message || error}`);
  }
}

function locationToCommand(position) {
  const { latitude, longitude, speed } = position.coords;
  const mph = Number.isFinite(speed) && speed !== null ? speed * 2.236936 : 0;
  return `GPS,${latitude.toFixed(6)},${longitude.toFixed(6)},${mph.toFixed(1)},${currentHeading.toFixed(0)}`;
}

function updateGpsReadout(position) {
  const { latitude, longitude, accuracy, heading, speed } = position.coords;
  const gpsHeading = Number.isFinite(heading) && heading !== null && Number.isFinite(speed) && speed > 0.7
    ? normalizeHeading(heading)
    : currentHeading;
  if (gpsHeading !== currentHeading && Number.isFinite(speed) && speed > 0.7) {
    currentHeading = gpsHeading;
  }
  liveFix = {
    latitude,
    longitude,
    heading: gpsHeading,
  };
  if (!gpsOrigin) {
    gpsOrigin = { ...liveFix };
  }
  centerLiveMap();
  const offset = gpsOffsetFromOrigin(liveFix);
  document.querySelector("#gpsReadout").textContent =
    `${latitude.toFixed(6)}, ${longitude.toFixed(6)}  +/- ${Math.round(accuracy)} m  ${offset.east.toFixed(1)}E ${offset.north.toFixed(1)}N`;
  setGpsStatus(`GPS live ${Math.round(accuracy)}m`);
  const town = townNameForFix(liveFix);
  if (town !== lastTownName) {
    lastTownName = town;
  }
  const mapMode = mapData && !fixInsideMapBounds(liveFix) ? "LIVE OUTSIDE PACK" : "LIVE CENTER";
  document.querySelector("#mapMode").textContent = `${town} / ${mapMode}`;
  drawPauseMap();
}

window.nativeGps = (latitude, longitude, accuracy = 0, heading = currentHeading) => {
  const numericHeading = Number.isFinite(Number(heading)) ? normalizeHeading(Number(heading)) : currentHeading;
  currentHeading = numericHeading;
  updateGpsReadout({
    coords: {
      latitude: Number(latitude),
      longitude: Number(longitude),
      accuracy: Number(accuracy) || 0,
      heading: numericHeading,
      speed: 0,
    },
  });
};

window.nativeSetStatus = (gpsText, bridgeText) => {
  if (gpsText) setGpsStatus(String(gpsText));
  if (bridgeText) setBleState(String(bridgeText));
  updateDebugSnapshot();
};

async function pushHeadingToWatch() {
  if (!liveFix) return;
  const now = performance.now();
  if (now - lastHeadingPushMs < 900) return;
  lastHeadingPushMs = now;
  liveFix.heading = currentHeading;
  drawPauseMap();
  await sendCommand(`HEAD,${currentHeading.toFixed(0)}`);
}

function setCompassHeading(heading, source = "sensor") {
  if (!Number.isFinite(heading)) return;
  if (source === "sensor" && Date.now() < headingManualUntil) return;
  const now = performance.now();
  const incoming = normalizeHeading(heading);
  if (source === "sensor") {
    const delta = headingDelta(currentHeading, incoming);
    if (Math.abs(delta) < 3 && now - lastHeadingRenderMs < 1500) return;
    currentHeading = normalizeHeading(currentHeading + delta * 0.22);
  } else {
    currentHeading = incoming;
  }
  lastHeadingRenderMs = now;
  const headingStatus = document.querySelector("#headingStatus");
  if (headingStatus) {
    headingStatus.textContent = `Heading ${currentHeading.toFixed(0)} deg`;
  }
  if (liveFix) {
    liveFix.heading = currentHeading;
    drawPauseMap();
    pushHeadingToWatch();
  }
}

async function bumpHeading(delta) {
  headingManualUntil = Date.now() + 10000;
  setCompassHeading(currentHeading + delta, "manual");
  if (!liveFix) {
    liveFix = gpsOrigin || { latitude: 44.09785, longitude: -70.1942, heading: currentHeading };
    gpsOrigin = gpsOrigin || { ...liveFix };
  }
  liveFix.heading = currentHeading;
  drawPauseMap();
  await sendCommand(`HEAD,${currentHeading.toFixed(0)}`);
}

function bindCompassHeading() {
  const handler = (event) => {
    let heading = null;
    if (Number.isFinite(event.webkitCompassHeading)) {
      heading = event.webkitCompassHeading;
    } else if (Number.isFinite(event.alpha)) {
      heading = 360 - event.alpha;
    }
    if (heading !== null) {
      setCompassHeading(heading);
    }
  };

  const start = () => {
    if (!window.DeviceOrientationEvent) return;
    window.addEventListener("deviceorientationabsolute", handler, true);
    window.addEventListener("deviceorientation", handler, true);
  };

  if (window.DeviceOrientationEvent?.requestPermission) {
    window.DeviceOrientationEvent.requestPermission().then((state) => {
      if (state === "granted") start();
    }).catch(start);
  } else {
    start();
  }
}

async function startGps() {
  if (window.GtaNavNative?.startGps) {
    const bridgeText = String(document.querySelector("#bridgeStatus")?.textContent || document.querySelector("#bleState")?.textContent || "").toLowerCase();
    const alreadyLinked = bridgeText.includes("linked") && !bridgeText.includes("not linked") && !bridgeText.includes("offline") && !bridgeText.includes("local only");
    if (!alreadyLinked && !selectedWatchAddress) {
      setBleState("Pick watch");
      setGpsStatus("Pick GTA-Nav watch first");
      document.querySelector("#gpsReadout").textContent = "Tap Connect Watch, choose the GTA-Nav watch row, then start GPS.";
      showPanel("gps");
      return;
    }
    testFix = null;
    gpsAnchorSent = false;
    bindCompassHeading();
    setGpsStatus("Native GPS starting");
    setBleState("Connecting");
    document.querySelector("#gpsReadout").textContent = "Starting Android GPS service...";
    document.querySelector("#mapMode").textContent = "SNAZZY GTA / LIVE CENTER";
    showPanel("gps");
    await syncWatchSettings();
    window.GtaNavNative.startGps();
    return;
  }
  if (!window.isSecureContext) {
    document.querySelector("#gpsReadout").textContent = "Use the HTTPS Tailscale URL. Phone GPS is blocked on insecure pages.";
    setGpsStatus("Use HTTPS");
    return;
  }
  if (!navigator.geolocation) {
    document.querySelector("#gpsReadout").textContent = "Geolocation is not available here.";
    setGpsStatus("GPS unavailable");
    return;
  }
  if (watchId !== null) {
    navigator.geolocation.clearWatch(watchId);
  }
  gpsOrigin = null;
  testFix = null;
  gpsAnchorSent = false;
  bindCompassHeading();
  setGpsStatus("Requesting GPS");
  document.querySelector("#gpsReadout").textContent = "Requesting phone location permission...";
  showPanel("gps");
  watchId = navigator.geolocation.watchPosition(
    async (position) => {
      updateGpsReadout(position);
      if (!gpsAnchorSent) {
        await syncWatchSettings();
        await sendCommand("ANCHOR");
        gpsAnchorSent = true;
        await syncLandmarksToWatch();
      }
      await sendCommand(locationToCommand(position));
    },
    (error) => {
      document.querySelector("#gpsReadout").textContent = error.message;
      setGpsStatus(`GPS ${error.code}: ${error.message}`);
    },
    { enableHighAccuracy: true, maximumAge: 1000, timeout: 12000 },
  );
}

async function testMove() {
  if (!gpsOrigin) {
    gpsOrigin = { latitude: 44.09785, longitude: -70.1942, heading: 0 };
    gpsAnchorSent = false;
  }
  if (!testFix) {
    testFix = { latitude: gpsOrigin.latitude, longitude: gpsOrigin.longitude, heading: currentHeading };
  }
  const rad = (currentHeading * Math.PI) / 180;
  const stepMeters = 8;
  testMoveMeters += stepMeters;
  testFix.latitude += Math.cos(rad) * stepMeters / 111320;
  testFix.longitude += Math.sin(rad) * stepMeters / metersPerLonAt(testFix.latitude);
  liveFix = {
    latitude: testFix.latitude,
    longitude: testFix.longitude,
    heading: currentHeading,
  };
  const offset = gpsOffsetFromOrigin(liveFix);
  centerLiveMap();
  setGpsStatus("GPS test move");
  document.querySelector("#gpsReadout").textContent =
    `SIM ${offset.east.toFixed(1)}E ${offset.north.toFixed(1)}N`;
  document.querySelector("#mapMode").textContent = `SIM LIVE CENTER`;
  drawPauseMap();
  if (!gpsAnchorSent) {
    await syncWatchSettings();
    await sendCommand("ANCHOR");
    gpsAnchorSent = true;
    await syncLandmarksToWatch();
  }
  await sendCommand(`GPS,${liveFix.latitude.toFixed(6)},${liveFix.longitude.toFixed(6)},0.0,${currentHeading.toFixed(0)}`);
}

async function savePark() {
  if (liveFix) {
    const bearing = Number.isFinite(liveFix.heading) ? liveFix.heading : currentHeading;
    parkedFix = { latitude: liveFix.latitude, longitude: liveFix.longitude, heading: bearing, savedAt: Date.now() };
    localStorage.setItem("gtaNavParked", JSON.stringify(parkedFix));
    document.querySelector("#saveStatus").textContent =
      `Quick saved at ${parkedFix.latitude.toFixed(6)}, ${parkedFix.longitude.toFixed(6)}`;
    await sendCommand(`PARK,${parkedFix.latitude.toFixed(6)},${parkedFix.longitude.toFixed(6)},${bearing.toFixed(0)}`);
    drawPauseMap();
    return parkedFix;
  }
  return new Promise((resolve) => {
    navigator.geolocation.getCurrentPosition(async (position) => {
      const { latitude, longitude, heading } = position.coords;
      const bearing = Number.isFinite(heading) && heading !== null ? heading : currentHeading;
      parkedFix = { latitude, longitude, heading: bearing, savedAt: Date.now() };
      localStorage.setItem("gtaNavParked", JSON.stringify(parkedFix));
      document.querySelector("#saveStatus").textContent =
        `Parked at ${latitude.toFixed(6)}, ${longitude.toFixed(6)}`;
      await sendCommand(`PARK,${latitude.toFixed(6)},${longitude.toFixed(6)},${bearing.toFixed(0)}`);
      drawPauseMap();
      resolve(parkedFix);
    }, (error) => {
      document.querySelector("#gpsReadout").textContent = error.message;
      resolve(null);
    }, { enableHighAccuracy: true, timeout: 12000 });
  });
}

function speakMechanicLine() {
  const line = mechanicLines[Math.floor(Math.random() * mechanicLines.length)];
  document.querySelector("#mechanicLine").textContent = line;
  if (!window.speechSynthesis) {
    return;
  }
  window.speechSynthesis.cancel();
  const utterance = new SpeechSynthesisUtterance(line);
  utterance.rate = 0.92;
  utterance.pitch = 0.72;
  utterance.volume = 1;
  window.speechSynthesis.speak(utterance);
}

async function openCamera() {
  try {
    cameraStream = await navigator.mediaDevices.getUserMedia({ video: { facingMode: "environment" } });
    document.querySelector("#cameraView").srcObject = cameraStream;
  } catch (error) {
    addDebug(`web camera failed ${error.message || error}`);
    if (window.GtaNavNative?.openCameraApp) {
      window.GtaNavNative.openCameraApp();
    }
  }
}

function snapPhoto() {
  const video = document.querySelector("#cameraView");
  if (!video.videoWidth) {
    return;
  }
  const canvas = document.createElement("canvas");
  canvas.width = video.videoWidth;
  canvas.height = video.videoHeight;
  canvas.getContext("2d").drawImage(video, 0, 0);
  const img = new Image();
  img.src = canvas.toDataURL("image/jpeg", 0.82);
  document.querySelector("#gallery").prepend(img);
  showPanel("gallery");
}

document.querySelectorAll(".app").forEach((button) => {
  button.addEventListener("click", () => showPanel(button.dataset.panel));
});

document.querySelectorAll(".back").forEach((button) => {
  button.addEventListener("click", () => showPanel(null));
});

document.querySelector("#connectBle").addEventListener("click", connectBle);
document.querySelector("#startGps").addEventListener("click", startGps);
document.querySelector("#testMove").addEventListener("click", testMove);
document.querySelector("#savePark").addEventListener("click", savePark);
document.querySelector("#routeDriveMode").addEventListener("click", () => setTravelMode("drive"));
document.querySelector("#routeWalkMode").addEventListener("click", () => setTravelMode("walk"));
document.querySelector("#headingLeft").addEventListener("click", () => bumpHeading(-30));
document.querySelector("#headingRight").addEventListener("click", () => bumpHeading(30));
document.querySelector("#mapZoomIn").addEventListener("click", () => zoomPauseMap(1.25));
document.querySelector("#mapZoomOut").addEventListener("click", () => zoomPauseMap(0.8));
document.querySelector("#mapZoomReset").addEventListener("click", resetPauseMapZoom);
document.querySelector("#useCurrentLocation").addEventListener("click", fillLandmarkFromCurrent);
document.querySelector("#lookupAddress").addEventListener("click", lookupAddressForLandmark);
document.querySelector("#saveLandmark").addEventListener("click", addLandmark);
document.querySelector("#quickSavePark").addEventListener("click", savePark);
document.querySelector("#routeWork").addEventListener("click", routeWorkAddress);
document.querySelector("#showParked").addEventListener("click", async () => {
  if (parkedFix) {
    await sendCommand(`PARK,${parkedFix.latitude.toFixed(6)},${parkedFix.longitude.toFixed(6)},${parkedFix.heading.toFixed(0)}`);
    drawPauseMap();
    showPanel("gps");
  } else {
    await savePark();
  }
});
document.querySelector("#routeParked").addEventListener("click", routeParkedLocation);
document.querySelector("#speakMechanic").addEventListener("click", speakMechanicLine);
document.querySelector("#openCamera").addEventListener("click", openCamera);
document.querySelector("#snapPhoto").addEventListener("click", snapPhoto);
    document.querySelector("#copyCommand").addEventListener("click", async () => {
      await navigator.clipboard.writeText(lastGpsCommand || document.querySelector("#commandBox").value);
    });
    document.querySelector("#openWatchSim").addEventListener("click", () => {
      window.open("./watch-sim/", "_blank", "noopener");
    });
    document.querySelector("#saveSettings").addEventListener("click", saveAppSettings);
document.querySelector("#syncWatchSettings").addEventListener("click", syncWatchSettings);
document.querySelector("#syncCalendarWatch").addEventListener("click", syncWatchSettings);
document.querySelector("#linkPhoneCalendar").addEventListener("click", linkPhoneCalendar);
document.querySelector("#syncWeatherWatch").addEventListener("click", syncLiveWeather);
document.querySelector("#loadPhoneContacts").addEventListener("click", linkPhoneContacts);
document.querySelector("#loadRecentCalls").addEventListener("click", linkRecentCalls);
document.querySelector("#syncPhoneBookWatch").addEventListener("click", syncPhonebookToWatch);
document.querySelector("#loadPhoneGallery").addEventListener("click", linkPhoneGallery);
document.querySelector("#syncGalleryWatch").addEventListener("click", syncGalleryToWatch);
document.querySelector("#sendText").addEventListener("click", sendTextMessage);
document.querySelector("#textRouteParked").addEventListener("click", routeParkedLocation);
document.querySelector("#textInput").addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    sendTextMessage();
  }
});
document.querySelector("#textContact").addEventListener("change", (event) => {
  selectedConversationId = event.target.value;
  localStorage.setItem("gtaNavTextContact", selectedConversationId);
  renderTexts();
});
document.querySelector("#homeButton").addEventListener("click", () => showPanel(null));
document.querySelector("#toggleLegend").addEventListener("click", () => {
  document.querySelector("#mapLegend").hidden = false;
});
document.querySelector("#closeLegend").addEventListener("click", () => {
  document.querySelector("#mapLegend").hidden = true;
});

document.querySelectorAll(".theme-dot").forEach((button) => {
  button.addEventListener("click", async () => {
    document.body.dataset.theme = button.dataset.theme;
    localStorage.setItem("gtaNavTheme", button.dataset.theme);
    await sendCommand(`THEME,${themeChoices[button.dataset.theme]?.watch || "theme1"}`);
  });
});

document.querySelector("#dialCall").addEventListener("click", handleDial);
document.querySelector("#dialInput").addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    handleDial();
  }
});

document.querySelectorAll(".job-card").forEach((button) => {
  button.addEventListener("click", () => {
    if (button.dataset.command) sendCommand(button.dataset.command);
  });
});

document.querySelector("#openGtaWeb").addEventListener("click", () => openNativeBrowser("https://www.playdosgames.com/play/grand-theft-auto"));
document.querySelector("#openDosGta").addEventListener("click", () => openNativeBrowser("https://www.playdosgames.com/play/grand-theft-auto"));
document.querySelector("#openOsmWeb").addEventListener("click", () => openNativeBrowser("https://www.openstreetmap.org/"));

if (parkedFix) {
  document.querySelector("#saveStatus").textContent =
    `Parked at ${parkedFix.latitude.toFixed(6)}, ${parkedFix.longitude.toFixed(6)}`;
}
renderContacts();
renderTexts();
renderMail();
renderMissedCalls();
renderGalleryItems();
renderPhoneLinkStatus();
renderSettings();
renderBank();
renderLandmarkTypes();
renderLandmarks();
bindMapGestures();
bindAddressAutocomplete();
loadPauseMap();
updateCapabilityStatus();
setClock();
setInterval(setClock, 1000);
setInterval(renderCalendarPanel, 30000);
setInterval(() => generateAmbientActivity(false), 90000);
setInterval(() => {
  if (window.GtaNavNative?.getDebugLog) {
    addDebug(window.GtaNavNative.getDebugLog());
  }
  updateDebugSnapshot();
}, 1000);

if ("serviceWorker" in navigator) {
  navigator.serviceWorker.register("sw.js").catch(() => {});
}
