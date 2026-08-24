const canvas = document.querySelector("#watchCanvas");
const ctx = canvas.getContext("2d");
const W = canvas.width;
const H = canvas.height;

const screens = [
  "Launcher",
  "Map",
  "Clock",
  "Pedometer",
  "Weather",
  "Calendar",
  "Contacts",
  "Messages",
  "Buddy",
  "Settings",
];

const state = {
  screen: "Launcher",
  overview: false,
  bridge: "mock linked",
  theme: "theme1",
  heading: 24,
  battery: 78,
  gps: null,
  parked: null,
  dest: null,
  trip: { destination: "Waypoint", mode: "drive", miles: 1.04, tolls: 0, gas: 3.45 },
  weather: { temp: 72, high: 78, low: 61, label: "Test Weather", city: "Lewiston" },
  fitness: { steps: 4230, miles: 1.8, calories: 186, active: 28, goal: 8000 },
  contacts: [
    { name: "Mechanic", role: "Find parked car" },
    { name: "Mors-ish Mutual", role: "Insurance comedy" },
    { name: "Roadside", role: "Route assist" },
    { name: "GTA-Nav Support", role: "Bridge help" },
  ],
  messages: [
    { from: "Mechanic", text: "Your ride marker is live if the phone saved it." },
    { from: "Support", text: "Mock bridge is feeding this simulator." },
  ],
  log: [],
};

const themeColors = {
  theme1: { accent: "#39d86f", route: "#a34eff", armor: "#38a8ff", health: "#3fd46c" },
  theme2: { accent: "#46cfff", route: "#f0a824", armor: "#1ba8ff", health: "#35d36d" },
  theme3: { accent: "#ff7b29", route: "#d254ff", armor: "#30b6ff", health: "#58d65e" },
};

let mapImage = new Image();
mapImage.src = "../snazzy-watch-close.bmp";
let overviewImage = new Image();
overviewImage.src = "../snazzy-watch-overview.bmp";
mapImage.onload = overviewImage.onload = draw;

function log(line) {
  state.log.unshift(`${new Date().toLocaleTimeString()} ${line}`);
  state.log = state.log.slice(0, 32);
  document.querySelector("#log").textContent = state.log.join("\n");
}

function csvParts(command) {
  const parts = [];
  let current = "";
  let quoted = false;
  for (let i = 0; i < command.length; i += 1) {
    const ch = command[i];
    if (ch === '"') {
      quoted = !quoted;
    } else if (ch === "," && !quoted) {
      parts.push(current);
      current = "";
    } else {
      current += ch;
    }
  }
  parts.push(current);
  return parts.map((part) => part.trim());
}

function handleCommand(command, mode = "sim") {
  if (!command) return;
  const parts = csvParts(command);
  const op = String(parts[0] || "").toUpperCase();
  state.bridge = mode === "native" ? "native linked" : "mock linked";
  if (op === "GPS") {
    state.gps = {
      lat: Number(parts[1]),
      lon: Number(parts[2]),
      speed: Number(parts[3] || 0),
      heading: Number(parts[4] || state.heading),
    };
    state.heading = Number.isFinite(state.gps.heading) ? state.gps.heading : state.heading;
  } else if (op === "HEAD") {
    state.heading = Number(parts[1] || state.heading);
  } else if (op === "PARK") {
    state.parked = { lat: Number(parts[1]), lon: Number(parts[2]), heading: Number(parts[3] || 0) };
  } else if (op === "DEST") {
    state.dest = { lat: Number(parts[1]), lon: Number(parts[2]), label: parts[3] || "Waypoint" };
    state.trip.destination = state.dest.label;
  } else if (op === "TRIP") {
    state.trip = {
      destination: parts[1] || "Waypoint",
      mode: parts[2] || "drive",
      miles: Number(parts[3] || 0),
      tolls: Number(parts[4] || 0),
      gas: Number(parts[5] || 0),
    };
  } else if (op === "WEATHER") {
    state.weather = {
      temp: Number(parts[1] || 0),
      high: Number(parts[2] || 0),
      low: Number(parts[3] || 0),
      label: parts[4] || "Weather",
      city: parts[5] || "Phone",
    };
  } else if (op === "FITNESS") {
    state.fitness = {
      steps: Number(parts[1] || 0),
      miles: Number(parts[2] || 0),
      calories: Number(parts[3] || 0),
      active: Number(parts[4] || 0),
      goal: Number(parts[5] || 8000),
    };
  } else if (op === "THEME") {
    state.theme = parts[1] || "theme1";
  } else if (op === "CONTACT") {
    const index = Number(parts[1] || 0);
    state.contacts[index] = { name: parts[2] || "Contact", role: parts[4] || parts[3] || "Phone contact" };
  } else if (op === "MSG") {
    const index = Number(parts[1] || 0);
    state.messages[index] = { from: parts[2] || "Message", text: parts[3] || "" };
  }
  document.querySelector("#commandStatus").textContent = command.slice(0, 58);
  log(`${mode}: ${command}`);
  draw();
}

function clear() {
  ctx.fillStyle = "#000";
  ctx.fillRect(0, 0, W, H);
}

function text(txt, x, y, size = 18, color = "#f3f5ea", align = "left", weight = "600") {
  ctx.fillStyle = color;
  ctx.font = `${weight} ${size}px "Segoe UI", Arial, sans-serif`;
  ctx.textAlign = align;
  ctx.textBaseline = "top";
  ctx.fillText(txt, x, y);
}

function topBar(title = "") {
  ctx.fillStyle = "rgba(0,0,0,0.82)";
  ctx.fillRect(0, 0, W, 34);
  text(new Date().toLocaleTimeString([], { hour: "numeric", minute: "2-digit" }), 16, 8, 14, "#fff");
  text(title, W / 2, 8, 13, colors().accent, "center", "700");
  drawSignal(W - 62, 10);
  drawBattery(W - 35, 10, 24, 12, state.battery);
}

function drawBattery(x, y, w, h, value) {
  ctx.strokeStyle = "#f4f4ef";
  ctx.lineWidth = 2;
  ctx.strokeRect(x, y, w, h);
  ctx.fillRect(x + w + 2, y + 4, 2, h - 8);
  ctx.fillStyle = value > 24 ? colors().health : "#ff3d3d";
  ctx.fillRect(x + 3, y + 3, Math.max(0, (w - 6) * value / 100), h - 6);
}

function drawSignal(x, y) {
  ctx.fillStyle = state.bridge.includes("linked") ? colors().accent : "#5b625d";
  for (let i = 0; i < 3; i += 1) ctx.fillRect(x + i * 6, y + 8 - i * 4, 4, 4 + i * 4);
}

function colors() {
  return themeColors[state.theme] || themeColors.theme1;
}

function drawLauncher() {
  clear();
  topBar("GTA-NAV");
  const apps = [
    ["Trackify", "M", colors().accent],
    ["Clock", "12", "#38a8ff"],
    ["Steps", "S", "#f0a824"],
    ["Contacts", "C", "#8957ff"],
    ["Messages", "...", "#e7a328"],
    ["Buddy", "AI", "#29c4ff"],
    ["Weather", "72", "#46d3a6"],
    ["Settings", "*", "#8d99a3"],
  ];
  apps.forEach((app, i) => {
    const col = i % 2;
    const row = Math.floor(i / 2);
    const x = 52 + col * 170;
    const y = 58 + row * 100;
    ctx.fillStyle = "#121615";
    ctx.fillRect(x, y, 112, 70);
    ctx.strokeStyle = app[2];
    ctx.lineWidth = 5;
    ctx.strokeRect(x + 2, y + 2, 108, 66);
    text(app[1], x + 56, y + 14, 24, app[2], "center", "800");
    text(app[0], x + 56, y + 74, 14, "#f2f2eb", "center");
  });
}

function drawMap() {
  clear();
  const image = state.overview ? overviewImage : mapImage;
  if (image.complete && image.naturalWidth) {
    ctx.drawImage(image, 0, 0, W, H);
  } else {
    ctx.fillStyle = "#141816";
    ctx.fillRect(0, 0, W, H);
  }

  if (state.overview) {
    drawWideOverlay();
  } else {
    drawMiniOverlay();
  }
}

function drawMiniOverlay() {
  const cx = W / 2;
  const cy = H / 2 + 6;
  const r = 174;
  ctx.save();
  ctx.globalCompositeOperation = "destination-in";
  ctx.beginPath();
  ctx.arc(cx, cy, r - 9, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
  ctx.strokeStyle = "#000";
  ctx.lineWidth = 10;
  ctx.beginPath();
  ctx.arc(cx, cy, r, 0, Math.PI * 2);
  ctx.stroke();
  ctx.strokeStyle = colors().armor;
  ctx.lineWidth = 7;
  ctx.beginPath();
  ctx.arc(cx, cy, r - 9, -1.08, 1.14);
  ctx.stroke();
  ctx.strokeStyle = colors().health;
  ctx.beginPath();
  ctx.arc(cx, cy, r - 9, 1.28, 3.92);
  ctx.stroke();
  drawHouse(cx - 138, cy + 8);
  drawHouse(cx + 1, cy + 148);
  drawHouse(cx - 111, cy - 96);
  drawPlayer(cx, cy, state.heading);
}

function drawWideOverlay() {
  const c = colors();
  ctx.fillStyle = "rgba(0,0,0,0.28)";
  ctx.fillRect(0, 0, W, H);
  ctx.strokeStyle = c.route;
  ctx.lineWidth = 9;
  ctx.lineCap = "round";
  ctx.beginPath();
  ctx.moveTo(205, 300);
  ctx.lineTo(204, 225);
  ctx.lineTo(276, 178);
  ctx.lineTo(327, 96);
  ctx.stroke();
  drawPlayer(W / 2, 326, state.heading);
  ctx.fillStyle = "rgba(0,0,0,0.68)";
  ctx.fillRect(0, H - 48, W, 48);
  drawBar(54, H - 32, 138, 10, state.battery / 100, c.health);
  drawBar(194, H - 32, 138, 10, Math.min(1, Number(state.trip.miles || 0) / 3), c.armor);
  text("N", 18, H - 42, 18, "#fff", "center", "800");
  text(`${Number(state.trip.miles || 0).toFixed(2)} mi`, 52, H - 72, 24, "#fff");
  text("M", 21, H - 34, 20, "#61b6ff", "center", "900");
  text("?", 338, H - 41, 22, "#61b6ff", "center", "900");
}

function drawBar(x, y, w, h, value, color) {
  ctx.fillStyle = "#1d332b";
  ctx.fillRect(x, y, w, h);
  ctx.fillStyle = color;
  ctx.fillRect(x, y, w * Math.max(0, Math.min(1, value)), h);
}

function drawHouse(x, y) {
  ctx.fillStyle = "#fff";
  ctx.beginPath();
  ctx.arc(x, y, 17, 0, Math.PI * 2);
  ctx.fill();
  ctx.fillStyle = "#000";
  ctx.beginPath();
  ctx.moveTo(x - 9, y);
  ctx.lineTo(x, y - 8);
  ctx.lineTo(x + 9, y);
  ctx.lineTo(x + 9, y + 9);
  ctx.lineTo(x - 9, y + 9);
  ctx.closePath();
  ctx.fill();
}

function drawPlayer(x, y, heading) {
  ctx.save();
  ctx.translate(x, y);
  ctx.rotate((heading || 0) * Math.PI / 180);
  ctx.fillStyle = "#f7f7f7";
  ctx.strokeStyle = "#101010";
  ctx.lineWidth = 3;
  ctx.beginPath();
  ctx.moveTo(0, -18);
  ctx.lineTo(13, 15);
  ctx.lineTo(0, 8);
  ctx.lineTo(-13, 15);
  ctx.closePath();
  ctx.fill();
  ctx.stroke();
  ctx.restore();
}

function drawListScreen(title, rows) {
  clear();
  topBar(title);
  rows.slice(0, 6).forEach((row, i) => {
    const y = 56 + i * 66;
    ctx.fillStyle = i === 0 ? "#1c231f" : "#0c0f0e";
    ctx.fillRect(22, y, W - 44, 52);
    ctx.strokeStyle = i === 0 ? colors().accent : "#252b27";
    ctx.strokeRect(22, y, W - 44, 52);
    text(row.title || row.name || row.from, 38, y + 8, 16, "#fff");
    text(row.sub || row.role || row.text || "", 38, y + 30, 12, "#aeb8ae");
  });
}

function drawClock() {
  clear();
  topBar("CLOCK");
  const cx = W / 2;
  const cy = 238;
  const r = 130;
  ctx.strokeStyle = colors().accent;
  ctx.lineWidth = 6;
  ctx.beginPath();
  ctx.arc(cx, cy, r, 0, Math.PI * 2);
  ctx.stroke();
  for (let i = 0; i < 12; i += 1) {
    const a = i * Math.PI / 6 - Math.PI / 2;
    ctx.fillStyle = i % 3 === 0 ? "#fff" : "#788079";
    ctx.fillRect(cx + Math.cos(a) * 112 - 3, cy + Math.sin(a) * 112 - 3, 6, 6);
  }
  const now = new Date();
  const min = now.getMinutes();
  const hour = now.getHours() % 12 + min / 60;
  hand(cx, cy, hour * 30, 70, "#fff", 8);
  hand(cx, cy, min * 6, 104, colors().accent, 5);
  text(now.toLocaleDateString(), cx, 392, 18, "#fff", "center");
}

function hand(cx, cy, deg, len, color, width) {
  const a = deg * Math.PI / 180 - Math.PI / 2;
  ctx.strokeStyle = color;
  ctx.lineWidth = width;
  ctx.lineCap = "round";
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.lineTo(cx + Math.cos(a) * len, cy + Math.sin(a) * len);
  ctx.stroke();
}

function drawStats() {
  clear();
  topBar("PEDOMETER");
  const pct = Math.min(1, state.fitness.steps / (state.fitness.goal || 8000));
  ctx.strokeStyle = "#202722";
  ctx.lineWidth = 28;
  ctx.beginPath();
  ctx.arc(W / 2, 196, 116, 0, Math.PI * 2);
  ctx.stroke();
  ctx.strokeStyle = colors().accent;
  ctx.beginPath();
  ctx.arc(W / 2, 196, 116, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * pct);
  ctx.stroke();
  text(String(state.fitness.steps), W / 2, 160, 44, "#fff", "center", "900");
  text("steps", W / 2, 212, 18, "#aeb8ae", "center");
  drawMetric("DIST", `${state.fitness.miles.toFixed(1)} mi`, 54, 356);
  drawMetric("CAL", `${state.fitness.calories}`, 222, 356);
}

function drawMetric(label, value, x, y) {
  ctx.fillStyle = "#111514";
  ctx.fillRect(x, y, 134, 76);
  ctx.strokeStyle = "#29302c";
  ctx.strokeRect(x, y, 134, 76);
  text(label, x + 14, y + 12, 13, "#aeb8ae");
  text(value, x + 14, y + 36, 22, "#fff", "left", "800");
}

function drawWeather() {
  clear();
  topBar("WEATHER");
  text(state.weather.city, W / 2, 82, 22, "#fff", "center");
  text(`${state.weather.temp}°`, W / 2, 136, 86, colors().accent, "center", "900");
  text(state.weather.label, W / 2, 250, 24, "#fff", "center");
  drawMetric("HIGH", `${state.weather.high}°`, 54, 348);
  drawMetric("LOW", `${state.weather.low}°`, 222, 348);
}

function drawSettings() {
  drawListScreen("SETTINGS", [
    { title: "Bridge", sub: state.bridge },
    { title: "Theme", sub: state.theme },
    { title: "Battery", sub: `${state.battery}%` },
    { title: "Map", sub: state.overview ? "wide mini map" : "mini map" },
  ]);
}

function drawBuddy() {
  drawListScreen("AI BUDDY", [
    { title: "Mic", sub: "Phone mic bridge required" },
    { title: "Status", sub: "Text command simulator ready" },
    { title: "Prompt", sub: "Ask mechanic, route, weather, BLE" },
  ]);
}

function drawCalendar() {
  drawListScreen("CALENDAR", [
    { title: new Date().toLocaleDateString(undefined, { weekday: "long" }), sub: new Date().toLocaleDateString() },
    { title: "Safe House", sub: "Synced phone events will show here" },
    { title: "Alarm", sub: "07:00 Wake" },
  ]);
}

function draw() {
  document.querySelector("#screenStatus").textContent = `${state.screen}${state.overview ? " Wide" : ""}`;
  document.querySelector("#bridgeStatus").textContent = state.bridge;
  document.querySelector("#gpsStatus").textContent = state.gps ? `${state.gps.lat.toFixed(5)}, ${state.gps.lon.toFixed(5)}` : "No fix";
  if (state.screen === "Launcher") drawLauncher();
  else if (state.screen === "Map") drawMap();
  else if (state.screen === "Clock") drawClock();
  else if (state.screen === "Pedometer") drawStats();
  else if (state.screen === "Weather") drawWeather();
  else if (state.screen === "Calendar") drawCalendar();
  else if (state.screen === "Contacts") drawListScreen("CONTACTS", state.contacts);
  else if (state.screen === "Messages") drawListScreen("MESSAGES", state.messages);
  else if (state.screen === "Buddy") drawBuddy();
  else drawSettings();
}

function setScreen(screen) {
  state.screen = screen;
  document.querySelector("#screenSelect").value = screen;
  draw();
}

function inject(command) {
  handleCommand(command, "sim");
}

document.querySelector("#screenSelect").innerHTML = screens.map((screen) => `<option>${screen}</option>`).join("");
document.querySelector("#screenSelect").addEventListener("change", (event) => setScreen(event.target.value));
document.querySelector("#headingInput").addEventListener("input", (event) => {
  state.heading = Number(event.target.value);
  draw();
});
document.querySelector("#batteryInput").addEventListener("input", (event) => {
  state.battery = Number(event.target.value);
  draw();
});
document.querySelector("#tapButton").addEventListener("click", () => {
  if (state.screen === "Launcher") setScreen("Map");
  else setScreen("Launcher");
});
document.querySelector("#doubleTapButton").addEventListener("click", () => {
  if (state.screen !== "Map") setScreen("Map");
  state.overview = !state.overview;
  draw();
});
document.querySelector("#swipeUpButton").addEventListener("click", () => {
  const i = screens.indexOf(state.screen);
  setScreen(screens[(i + 1) % screens.length]);
});
document.querySelector("#swipeDownButton").addEventListener("click", () => {
  const i = screens.indexOf(state.screen);
  setScreen(screens[(i - 1 + screens.length) % screens.length]);
});
document.querySelector("#demoGpsButton").addEventListener("click", () => inject("GPS,44.097327,-70.162827,0.0,24"));
document.querySelector("#routeButton").addEventListener("click", () => {
  inject("DEST,44.103177,-70.175840,Burger Shot");
  inject("TRIP,Burger Shot,drive,1.04,0.00,3.45");
  setScreen("Map");
});
document.querySelector("#weatherButton").addEventListener("click", () => inject("WEATHER,72,78,61,Clear,Lewiston"));
document.querySelector("#fitnessButton").addEventListener("click", () => inject("FITNESS,6238,2.7,261,42,8000"));
document.querySelector("#sendCommandButton").addEventListener("click", () => inject(document.querySelector("#commandInput").value));

if (window.BroadcastChannel) {
  const channel = new BroadcastChannel("gta-nav-watch");
  channel.addEventListener("message", (event) => {
    if (event.data?.command) handleCommand(event.data.command, event.data.mode || "mock");
  });
}

window.addEventListener("storage", (event) => {
  if (event.key === "gtaNavLastWatchCommand" && event.newValue) {
    try {
      const payload = JSON.parse(event.newValue);
      handleCommand(payload.command, payload.mode || "mock");
    } catch {
      handleCommand(event.newValue, "storage");
    }
  }
});

try {
  const last = JSON.parse(localStorage.getItem("gtaNavLastWatchCommand") || "null");
  if (last?.command) handleCommand(last.command, last.mode || "startup");
} catch {
  // Ignore corrupt local simulator state.
}

const params = new URLSearchParams(window.location.search);
if (params.has("gps")) {
  handleCommand("GPS,44.097327,-70.162827,0.0,24", "url");
}
if (params.has("route")) {
  handleCommand("DEST,44.103177,-70.175840,Burger Shot", "url");
  handleCommand("TRIP,Burger Shot,drive,1.04,0.00,3.45", "url");
}
if (params.get("theme")) {
  state.theme = params.get("theme");
}
state.overview = params.has("wide");
setScreen(params.get("screen") && screens.includes(params.get("screen")) ? params.get("screen") : "Launcher");
log("sim ready");
