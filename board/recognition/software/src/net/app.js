const $ = (selector) => document.querySelector(selector);
const stateLabels = {
  NORMAL: "状态正常",
  FALLING: "疑似跌倒",
  FALLEN: "确认跌倒",
};
const trackLabels = { NORMAL: "正常", FALLING: "疑似跌倒", FALLEN: "已跌倒" };
const sourceLabels = { simulation: "内置仿真", bridge: "外部遥测桥接", waiting: "等待板卡" };
const qualityLabels = ["", "未标定", "传感器无效", "红外过期", "环境数据过期", "环境条件不适用", "超出标定区域", "标定无效", "超出红外视场", "有效像元不足", "热证据冲突"];
const temperatures = [];
let videoSignature = "";
let previewConfig = null;
let thermalSource = "";
let lastThermalLabel = "";

const pages = {
  monitor: ["现场监测", "observer"],
  tracking: ["人员跟踪", "observer"],
  care: ["照护控制", "companions"],
  events: ["事件记录", "recorder"],
  device: ["设备状态", "companions"],
};
function showPage(focus = false) {
  // Keep the skip-to-content link within the current view.
  if (location.hash === "#main") return;
  const key = location.hash.slice(1);
  const name = Object.hasOwn(pages, key) ? key : "monitor";
  document.querySelectorAll("[data-view]").forEach(panel => { panel.hidden = panel.dataset.view !== name; });
  document.querySelectorAll("[data-nav]").forEach(link => {
    if (link.dataset.nav === name) link.setAttribute("aria-current", "page");
    else link.removeAttribute("aria-current");
  });
  const [title, art] = pages[name];
  $("#page-title").textContent = title;
  $("#page-art").src = `/media/${art}.png`;
  document.title = `VF Ti60 · ${title}`;
  if (name !== "monitor") $("#stream-video").pause();
  syncControlFrames();
  if (name !== "monitor") { $("#stream-image").removeAttribute("src"); videoSignature = ""; }
  if (focus) { $("#page-title").focus({ preventScroll: true }); globalThis.scrollTo(0, 0); }
}
globalThis.addEventListener("hashchange", () => showPage(true));
showPage();

function safeVideoUrl(value) {
  try {
    const url = new URL(value, location.origin);
    return ["http:", "https:"].includes(url.protocol) && !url.username && !url.password ? url.href : "";
  } catch { return ""; }
}

function safeSnapshotUrl(value) {
  return typeof value === "string" && /^(\/snapshots\/[0-9a-f]{32}\.jpg|\/media\/demo-(fallen|falling)\.jpg|http:\/\/192\.168\.0\.3:8092\/image\/[0-9a-f]{32}\/[1-9][0-9]{0,9}\.bmp)$/.test(value) ? value : "";
}

function renderVideo(config = { mode: "demo", kind: "video", url: "/media/demo.mp4" }) {
  if (config.source === "riscv-lwip") {
    previewConfig = config;
    $("#stream-video").pause(); $("#stream-video").hidden = true;
    return;
  }
  if (config.source === "v6-ethernet") {
    previewConfig = config;
    const image = $("#stream-image"), video = $("#stream-video");
    video.pause(); video.hidden = true; video.removeAttribute("src");
    if(document.hidden || $("[data-view='monitor']").hidden) {
      image.removeAttribute("src");videoSignature="";return;
    }
    image.hidden = !config.connected;
    $("#stream-empty").hidden = Boolean(config.connected);
    $("#stream-empty strong").textContent = "网口画面中断";
    $("#stream-empty span").textContent = "请检查板卡、网线和视频接收服务。";
    $("#video-state").textContent = config.connected ? `${config.low_power ? "低功耗画面" : "网口连续画面"} · ${config.fps.toFixed(1)} 帧/秒` : "等待网口画面";
    image.alt = "FPGA摄像头网口连续画面，320×180";
    const url = safeVideoUrl(config.url);
    image.onerror=()=>{videoSignature="";image.hidden=true;$("#stream-empty").hidden=false;};
    if (config.connected && videoSignature !== url) { image.setAttribute("src", url); videoSignature = url; }
    if (!config.connected) { image.removeAttribute("src"); videoSignature = ""; }
    return;
  }
  if (config.mode === "preview") {
    previewConfig = config;
    const image = $("#stream-image"), video = $("#stream-video");
    video.pause(); video.hidden = true; video.removeAttribute("src");
    image.hidden = !config.connected;
    $("#stream-empty").hidden = Boolean(config.connected);
    $("#stream-empty strong").textContent = "画面更新中断";
    $("#stream-empty span").textContent = "请检查板卡与串口连接。";
    const size = `${config.width || 96}×${config.height || 54}`;
    $("#video-state").textContent = config.connected ? `板端彩色预览 · ${size}` : "画面已过期";
    image.alt = `板端实际摄像头画面，${size}，采集帧 ${config.frame_id}`;
    const url = safeVideoUrl(config.url);
    if (config.connected && videoSignature !== url) { image.setAttribute("src", url); videoSignature = url; }
    return;
  }
  previewConfig = null;
  const signature = JSON.stringify(config);
  if (signature === videoSignature) return;
  videoSignature = signature;
  const url = safeVideoUrl(config.url);
  const video = $("#stream-video");
  const image = $("#stream-image");
  video.pause();
  video.hidden = config.kind === "mjpeg";
  image.hidden = config.kind !== "mjpeg";
  video.removeAttribute("src");
  image.removeAttribute("src");
  $("#stream-empty").hidden = Boolean(url);
  $("#video-state").textContent = config.mode === "live" ? "实时视频源" : "演示视频 · 非实时";
  if (config.mode === "unavailable") {
    video.hidden = image.hidden = true;
    $("#stream-empty").hidden = false;
    $("#stream-empty strong").textContent = "真实图像尚未接入";
    $("#stream-empty span").textContent = "板端事件抓拍可在事件记录中查看。";
    $("#video-state").textContent = "等待真实画面";
    return;
  }
  $("#stream-empty strong").textContent = "视频源不可用";
  $("#stream-empty span").textContent = "请检查摄像头或网关视频地址。";
  if (!url) { $("#video-state").textContent = "视频地址无效"; return; }
  if (config.kind === "mjpeg") image.setAttribute("src", url);
  else {
    video.setAttribute("src", url);
    video.muted = config.mode === "live";
    video.load();
    if (config.mode === "live" && !$("[data-view='monitor']").hidden) video.play().catch(() => {});
  }
}

function formatDuration(seconds) {
  const hours = Math.floor(seconds / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  const remainder = Math.floor(seconds % 60);
  return `${String(hours).padStart(2, "0")}:${String(minutes).padStart(2, "0")}:${String(remainder).padStart(2, "0")}`;
}

function timeLabel(value) {
  return new Intl.DateTimeFormat("zh-CN", { hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false }).format(new Date(value));
}

function eventTimeLabel(value) {
  const date = new Date(value);
  return date.toDateString() === new Date().toDateString()
    ? timeLabel(value)
    : `${date.toLocaleDateString("sv-SE")}\n${date.toLocaleTimeString("zh-CN", { hour: "2-digit", minute: "2-digit", hour12: false })}`;
}

function renderChart(value) {
  temperatures.push(value);
  if (temperatures.length > 60) temperatures.shift();
  if (temperatures.length === 1) temperatures.unshift(value);
  const min = Math.min(...temperatures) - 1;
  const max = Math.max(...temperatures) + 1;
  const points = temperatures.map((temperature, index) => {
    const x = index * 600 / (temperatures.length - 1);
    const y = 140 - (temperature - min) / (max - min) * 110;
    return [x, y];
  });
  const line = points.map(([x, y], index) => `${index ? "L" : "M"}${x.toFixed(1)} ${y.toFixed(1)}`).join(" ");
  $("#chart-line").setAttribute("d", line);
  $("#chart-area").setAttribute("d", `${line} L600 150 L0 150 Z`);
  $("#chart-tip").setAttribute("cy", points[points.length - 1][1].toFixed(1));
  $("#chart-tip").removeAttribute("hidden");
}

function renderTracks(tracks, emptyMessage = "当前没有跟踪目标") {
  const container = $("#tracks");
  container.replaceChildren();
  if (!tracks.length) {
    const empty = document.createElement("div");
    empty.className = "empty-row";
    empty.textContent = emptyMessage;
    container.append(empty);
    return;
  }
  tracks.forEach((track) => {
    const row = document.createElement("div");
    const unconfirmed = track.person_confirmed === 0 && track.state === "NORMAL";
    row.className = `track ${unconfirmed ? "unconfirmed" : track.state.toLowerCase()}`;
    const dot = document.createElement("i");
    dot.className = "track-dot";
    const description = document.createElement("div");
    const title = document.createElement("strong");
    title.textContent = `目标 ${track.id}`;
    const confidence = document.createElement("span");
    confidence.textContent = track.fusion_mode && track.fusion_mode !== "legacy"
      ? `人体 ${Math.round(track.detector_score * 100)}% · 规则 ${Math.round(track.rule_score * 100)}% · ${track.fusion_mode === "WEIGHTED" ? "融合" : "视觉降级"}${track.quality ? ` · ${qualityLabels[track.quality]}` : ""}`
      : `置信度 ${Math.round(track.confidence * 100)}%`;
    description.append(title, confidence);
    const label = document.createElement("em");
    label.textContent = unconfirmed ? "红外未确认" : trackLabels[track.state];
    row.append(dot, description, label);
    container.append(row);
  });
}

function renderSensors(data) {
  const s = data.sensors || {};
  const live = data.connected;
  $("#ambient").textContent = live && s.environment_valid ? `${s.ambient_c.toFixed(1)} °C` : "--";
  $("#humidity").textContent = live && s.environment_valid ? `${s.humidity_percent.toFixed(1)} %` : "--";
  const canvas = $("#thermal-map"), ctx = canvas.getContext("2d");
  const source = data.session_id ? `${data.device_id}/${data.session_id}` : data.source;
  if (source && source !== thermalSource) {
    thermalSource = source;
    lastThermalLabel = "";
    ctx.clearRect(0, 0, 32, 24);
  }
  const valid = live && s.thermal_centi ? s.thermal_centi.filter(v => v !== -32768) : [];
  if (valid.length) {
    const low = Math.min(...valid) / 100, high = Math.max(...valid) / 100;
    const range = Math.max(6, high-low);
    s.thermal_centi.forEach((v, i) => {
      const t = Math.max(0, Math.min(1, (v/100-low)/range));
      ctx.fillStyle = v === -32768 ? "#ccc" : `hsl(${240-t*240} 85% 48%)`;
      ctx.fillRect(i%32, Math.floor(i/32), 1, 1);
    });
    lastThermalLabel = `表面温度 ${low.toFixed(1)}～${high.toFixed(1)} °C`;
  }
  const current = live && s.thermal_valid && valid.length;
  const unavailable = !live ? data.mode === "waiting" ? "等待设备数据" : "数据中断" : "红外数据无效或过期";
  const label = lastThermalLabel
    ? current ? lastThermalLabel : `${lastThermalLabel} · ${unavailable}`
    : unavailable;
  $("#thermal-note").textContent = label;
  canvas.setAttribute("aria-label", label);
  const d = data.diagnostics;
  $("#diagnostics-note").textContent = d?.board ? `热图累计 ${d.thermal_frames} 帧 · 网口错包 ${d.rx_bad} · 缓冲丢包 ${d.rx_dropped}` : d?.thermal_frames !== undefined ? `热图累计 ${d.thermal_frames} 帧 · 未消费 ${d.overwritten} · 配对丢弃 ${d.pair_drops} · 传输跳帧 ${d.tx_dropped}` : "等待板端诊断数据";
}

let infraredEnabled=true,infraredPending=null,infraredPosting=false;
function renderInfrared(p) {
  const button=$("#infrared-toggle"),status=$("#infrared-status");
  if(!p){button.disabled=true;status.textContent="红外开关状态不可用，请检查板卡连接";return;}
  infraredEnabled=!!p.enabled;
  if(!infraredPosting && infraredPending!==null && !!p.active===infraredPending)infraredPending=null;
  const pending=infraredPosting || infraredPending!==null || p.enabled!==p.active;
  button.disabled=pending;
  button.setAttribute("aria-checked",String(!!p.active));
  status.textContent=pending?"切换中，等待板端下一次识别…":!p.active?"已关闭 · 仅 RGB 人体检测":
    p.calibrated?"已开启 · 按目标区域热证据确认":"已开启 · 待 RGB／红外标定，暂不确认绿框";
}
$("#infrared-toggle").onclick=async()=>{
  const button=$("#infrared-toggle");button.disabled=true;
  infraredPending=!infraredEnabled;infraredPosting=true;
  $("#infrared-status").textContent="正在发送到板卡…";
  try{
    const response=await fetch("/api/infrared",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:`enabled=${infraredPending?1:0}`,signal:AbortSignal.timeout(5000)});
    if(!response.ok || !(await response.json()).ok)throw Error("Infrared mode rejected");
    $("#infrared-status").textContent="切换中，等待板端下一次识别…";
  }catch(error){
    infraredPending=null;
    $("#infrared-status").textContent="红外开关切换失败，请检查板卡连接后重试";button.disabled=false;
  }finally{infraredPosting=false;}
};

let lastPowerSample=null,automaticPower=true;
function renderPower(p) {
  const button=$("#power-toggle");button.disabled=!p;
  if(!p){
    lastPowerSample=null;
    if($("#monitor-power-mode").textContent!=="工作模式：数据不可用")$("#monitor-power-mode").textContent="工作模式：数据不可用";
    $("#monitor-power-detail").textContent="等待板卡连接";
    $("#power-state").textContent="轻量监视：数据不可用";
    $("#power-metrics").textContent="等待采样";
    button.textContent="等待工作模式";return;
  }
  automaticPower=!!p.automatic;
  const displayMode={active:"正常检测",watch:"低功耗监视",waking:"正在恢复正常检测"}[p.mode]||"工作模式未知";
  if($("#monitor-power-mode").textContent!==displayMode)$("#monitor-power-mode").textContent=displayMode;
  $("#monitor-power-detail").textContent=p.mode==="watch"?"HDMI 画面调暗 · 网口视频目标 2 帧/秒 · 完整推理每 2 秒一次":
    p.mode==="waking"?"已唤醒 · 网页视频恢复目标 10 帧/秒":
    !automaticPower?"连续检测对照 · 自动低功耗已关闭":
    p.event_blocked?"告警／救援处理中，保持正常检测":
    !p.monitor_ok?"传感器状态异常，保持正常检测":
    p.empty_ms?`连续无人 · 距低功耗至少 ${Math.max(0,30-p.empty_ms/1000).toFixed(1)} 秒`:"有人或等待无人确认 · 网页视频目标 10 帧/秒";
  const mode={active:"正常检测",watch:"轻量监视",waking:"正在恢复检测"}[p.mode]||p.mode;
  const reason={boot:"开机",motion:"检测到运动",person:"兜底检测发现人体",fault:"告警或监测故障",manual:"手动切换"}[p.wake_reason]||p.wake_reason;
  const guard=p.event_blocked?"告警／救援处理中":!p.monitor_ok?"等待传感器恢复":`无人倒计时 ${Math.max(0,30-p.empty_ms/1000).toFixed(1)} 秒`;
  $("#power-state").textContent=`${automaticPower?"自动管理":"连续检测对照"} · ${mode} · ${p.mode==="active"?guard:`最近唤醒：${reason}`} · 轻量累计 ${(p.residence_ms[1]/1000).toFixed(1)} 秒`;
  if(lastPowerSample && p.sample_ms>lastPowerSample.sample_ms && p.inferences>=lastPowerSample.inferences && p.inference_us>=lastPowerSample.inference_us){
    const ms=p.sample_ms-lastPowerSample.sample_ms;
    $("#power-metrics").textContent=`采样窗 ${(ms/1000).toFixed(1)} 秒 · 推理 ${((p.inferences-lastPowerSample.inferences)*1000/ms).toFixed(2)} 次/秒 · 推理耗时占比 ${((p.inference_us-lastPowerSample.inference_us)/ms/10).toFixed(1)}% · 累计 ${p.inferences} 次`;
  }
  if(!lastPowerSample || p.sample_ms-lastPowerSample.sample_ms>=5000 || p.sample_ms<lastPowerSample.sample_ms)lastPowerSample=p;
  button.textContent=automaticPower?"切换为连续检测对照":"启用自动轻量监视";
}
$("#power-toggle").onclick=async()=>{
  const button=$("#power-toggle");button.disabled=true;
  try{
    const response=await fetch("/api/power",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:`mode=${automaticPower?"continuous":"auto"}`,signal:AbortSignal.timeout(5000)});
    if(!response.ok || !(await response.json()).ok)throw Error("Mode rejected");
    lastPowerSample=null;await loadStatus();
  }catch(error){$("#power-state").textContent="模式切换失败，请检查板卡连接后重试";button.disabled=false;}
};
let monitorReset=null,resetPending=0,resetSending=false,resetDeadline=0;
function renderMonitorReset(value) {
  monitorReset=value;
  const button=$("#reset-monitor"),status=$("#reset-monitor-status");
  if(!value || !Number.isInteger(value.completed)) {
    button.disabled=true;status.textContent=resetPending?"连接中断，重置结果尚未确认":"等待板卡连接";return;
  }
  if(resetPending && value.completed>=resetPending) {
    resetPending=0;status.textContent="已重置，可以开始下一次测试";
  } else if(resetPending && performance.now()>resetDeadline) {
    resetPending=0;status.textContent="尚未确认重置完成，请重试";
  }
  button.disabled=resetSending || Boolean(resetPending);
  button.textContent=button.disabled?"重置中…":"重新测试";
}
$("#reset-monitor").onclick=async()=>{
  const button=$("#reset-monitor"),status=$("#reset-monitor-status");
  resetSending=true;button.disabled=true;button.textContent="重置中…";
  status.textContent="正在清除本轮状态…";
  try {
    const response=await fetch("/api/monitor/reset",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"",signal:AbortSignal.timeout(5000)});
    const result=await response.json();
    if(!response.ok || !result.ok || !Number.isInteger(result.reset_id) || result.reset_id<1)throw Error("Reset rejected");
    resetPending=result.reset_id;resetDeadline=performance.now()+10000;
    status.textContent="等待板端完成重置…";
    await loadStatus();loadEvents();
  } catch(error) {status.textContent="重置请求未确认，请检查连接后重试";}
  finally {resetSending=false;renderMonitorReset(monitorReset);}
};
function renderStatus(data) {
  renderMonitorReset(data.connected?data.monitor_reset:null);
  renderInfrared(data.connected?data.infrared_assist:null);
  renderPower(data.connected?data.power:null);
  renderSensors(data);

  renderVideo(data.video);
  $("#fps").textContent = data.connected ? data.fps.toFixed(1) : "--";
  $("#latency").textContent = data.connected ? Math.round(data.latency_ms) : "--";
  const ambient = data.protocol_version ? data.sensors?.ambient_c : data.temperature;
  $("#temperature").textContent = data.connected && Number.isFinite(ambient) ? ambient.toFixed(1) : "--";
  $("#uptime").textContent = formatDuration(data.uptime_seconds);
  $("#people").textContent = data.connected ? `${data.people} 人${data.infrared_assist?.active ? "已确认" : ""}` : "--";
  $("#updated").textContent = data.connected ? timeLabel(data.updated_at) : "无新数据";
  $("#source-name").textContent = data.mode === "bridge" ? data.source : sourceLabels[data.mode] || data.source;
  $("#device-map").className = `device-map ${data.mode}${data.connected ? "" : " offline"}`;
  $("#path-source").textContent = data.mode === "simulation" ? "VF 板卡 · 待接入" : "FPGA 板卡";
  const connection = $("#connection");
  connection.textContent = data.mode === "simulation" ? "仿真运行中" : data.connected ? "遥测在线" : `已中断 ${data.stale_seconds}s`;
  connection.classList.toggle("offline", !data.connected);
  const pill = $("#source-pill");
  pill.classList.toggle("offline", !data.connected);
  pill.querySelector("span").textContent = data.mode === "simulation" ? "演示模式" : data.connected ? "板卡在线" : "板卡离线";
  const overall = $("#overall");
  overall.className = `overall ${data.connected ? data.overall.toLowerCase() : "offline"}`;
  $("#overall-kicker").textContent = data.mode === "simulation" ? "仿真状态" : "板端判定";
  $("#overall-label").textContent = data.connected ? stateLabels[data.overall] : "数据中断";
  if(data.mode === "waiting") {
    $("#overall-kicker").textContent = "连接状态";
    $("#overall-label").textContent = "等待板卡连接";
    pill.querySelector("span").textContent = "等待数据";
    connection.textContent = "等待板卡";
  }
  const announcement = $("#overall-label").textContent;
  if ($("#state-announcement").textContent !== announcement) $("#state-announcement").textContent = announcement;
  renderTracks(data.connected ? data.tracks : [], data.connected ? "当前没有跟踪目标" : "遥测中断，无法确认目标状态");
  if (data.connected && Number.isFinite(ambient)) renderChart(ambient);
}

async function loadStatus() {
  try {
    const response = await fetch("/api/status", { cache: "no-store", signal: AbortSignal.timeout(5000) });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const raw = await response.json();
    renderStatus(raw.server === "riscv-lwip" ? boardStatus(raw) : raw);
  } catch (error) {
    renderMonitorReset(null);
    renderPower(null);
    renderInfrared(null);

    if (previewConfig) renderVideo({ ...previewConfig, connected: false });
    renderSensors({ connected: false });
    $("#source-pill").querySelector("span").textContent = "服务离线";
    $("#source-pill").classList.add("offline");
    $("#connection").textContent = "无法连接";
    $("#connection").classList.add("offline");
    for (const id of ["fps", "latency", "temperature", "people", "updated", "uptime"]) $(`#${id}`).textContent = "--";
    $("#source-name").textContent = "不可用";
    $("#device-map").classList.add("offline");
    $("#overall").className = "overall offline";
    $("#overall-kicker").textContent = "连接状态";
    $("#overall-label").textContent = "服务离线";
    const announcement = "服务离线";
    if ($("#state-announcement").textContent !== announcement) $("#state-announcement").textContent = announcement;
    renderTracks([], "服务离线，无法确认目标状态");
  }
}

function renderEvents(events, container = $("#events")) {
    container.replaceChildren();
    if (!events.length) {
      const empty = document.createElement("div");
      empty.className = "empty-row";
      empty.textContent = "暂无事件记录";
      container.append(empty);
      return;
    }
    events.forEach((event) => {
      const row = document.createElement("div");
      row.className = `event ${event.severity}`;
      const dot = document.createElement("i");
      const copy = document.createElement("div");
      const title = document.createElement("strong");
      title.textContent = event.track_id ? `${event.title} · 目标 ${event.track_id}` : event.title;
      const detail = document.createElement("span");
      detail.textContent = `${event.detail} · ${event.source === "simulation" ? "仿真" : event.source === "gateway" ? "系统" : event.source}`;
      copy.append(title, detail);
      const time = document.createElement("time");
      time.dateTime = event.created_at;
      time.textContent = event.boot_ms === undefined ? eventTimeLabel(event.created_at) : `开机后 ${formatDuration(Math.floor(event.boot_ms/1000))}`;
      const needsPhoto = ["warning", "critical"].includes(event.severity);
      if (needsPhoto) {
        row.classList.add("has-evidence");
        const location = document.createElement("span");
        location.className = "event-location";
        location.textContent = `地点：${event.location || "未提供"}`;
        if(event.location)copy.append(location);
        copy.append(time);
        const evidence = document.createElement("figure");
        evidence.className = "event-evidence";
        const url = safeSnapshotUrl(event.snapshot_url);
        const photo = document.createElement(url ? "a" : "div");
        photo.className = "event-photo";
        if (url) {
          photo.href = url;
          photo.target = "_blank";
          photo.rel = "noopener";
          photo.setAttribute("aria-label", `查看目标 ${event.track_id || ""} 的事件图片`);
          const image = document.createElement("img");
          image.src = url;
          image.loading = "lazy";
          image.alt = event.snapshot_kind === "demo" ? "离线测试示例素材，并非该事件抓拍" : `目标 ${event.track_id || ""} 的事件抓拍`;
          image.addEventListener("error", () => { photo.replaceChildren(); photo.textContent = "图片不可用"; });
          photo.append(image);
        } else photo.textContent = "暂无抓拍";
        evidence.append(photo);
        row.append(dot, copy, evidence);
      } else row.append(dot, copy, time);
      container.append(row);
    });
}

async function loadEvents() {
  const container = $("#events");
  try {
    const response = await fetch("/api/events?limit=20", { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const raw = await response.json();
    let saved = [];
    if (raw.volatile) {
      try {
        const [reportResponse, savedResponse] = await Promise.all([
          fetch("/api/report", {cache:"no-store", signal:AbortSignal.timeout(1500)}),
          fetch("http://192.168.0.3:8092/api/events", {cache:"no-store", signal:AbortSignal.timeout(1500)})
        ]);
        if (!reportResponse.ok || !savedResponse.ok) throw Error("事件接收不可用");
        const report = await reportResponse.json(), archive = await savedResponse.json();
        saved = archive.events.filter(e => e.session === report.session);
      } catch { /* Event records remain available without the PC receiver. */ }
    }
    renderEvents(raw.volatile ? raw.events.slice().reverse().map(e => ({track_id:e.track,
      title:trackLabels[["NORMAL","FALLING","FALLEN"][e.state]], severity:["info","warning","critical"][e.state],
      detail:`规则分数 ${Math.round(e.rule/10)}%`,source:"板内记录",boot_ms:e.uptime_ms,created_at:new Date().toISOString(),
      ...eventPhoto(e, saved)})) : raw.events);
  } catch (error) {
    container.replaceChildren();
    const message = document.createElement("div");
    message.className = "error-row";
    message.textContent = "事件读取失败，请检查监控服务后重试。";
    container.append(message);
  }
}

function syncControlFrames() {
  document.querySelectorAll(".control-frame").forEach(frame => {
    const active = !document.hidden && !frame.closest("[data-view]").hidden;
    if (active && !frame.hasAttribute("src")) {
      frame.addEventListener("load", () => {
        try {
          const main = frame.contentDocument.querySelector("main");
          if (!main) return;
          const resize = () => { frame.style.height = `${Math.ceil(main.getBoundingClientRect().height) + 4}px`; };
          new ResizeObserver(resize).observe(main);
          resize();
          syncControlFrames();
        } catch { /* Board pages must have the same origin. */ }
      });
      frame.src = frame.dataset.src;
    }
    if (frame.hasAttribute("src")) frame.contentWindow.postMessage({type:"vf-page-visibility", active}, location.origin);
  });
}
document.addEventListener("visibilitychange", syncControlFrames);

let changingTracking = false;
async function loadTrackingMode() {
  try {
    const response = await fetch("/api/tracking", {cache:"no-store", signal:AbortSignal.timeout(4000)});
    if (!response.ok) throw Error("跟踪接口不可用");
    const data = await response.json();
    for (const mode of ["single", "multi"]) {
      const button = $(`#mode-${mode}`);
      button.disabled = changingTracking;
      button.setAttribute("aria-pressed", String(data.mode === mode));
    }
    $("#tracking-status").textContent = `${data.active_mode === "multi" ? "多人模式" : "单人模式"} · ${data.tracks.length}人${data.mode !== data.active_mode ? " · 正在切换" : ""}`;
  } catch {
    for (const mode of ["single", "multi"]) $(`#mode-${mode}`).disabled = true;
    $("#tracking-status").textContent = "模式读取中断，请检查板卡连接";
  }
}
async function changeTrackingMode(mode) {
  changingTracking = true;
  for (const key of ["single", "multi"]) $(`#mode-${key}`).disabled = true;
  try {
    const response = await fetch("/api/tracking", {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body:`mode=${mode}`, signal:AbortSignal.timeout(4000)});
    if (!response.ok) throw Error("模式未切换，请重试");
    $("#tracking-status").textContent = "切换请求已接收，等待下一轮检测生效…";
  } catch (error) {
    $("#tracking-status").textContent = `${error.message}；请刷新模式后重试`;
    return;
  }
  finally { changingTracking = false; }
  await loadTrackingMode();
}
for (const mode of ["single", "multi"]) $(`#mode-${mode}`).addEventListener("click", () => changeTrackingMode(mode));

async function loadArchive() {
  if ($("[data-view='events']").hidden || document.hidden) return;
  try {
    const response = await fetch("http://192.168.0.3:8092/api/events", {cache:"no-store", signal:AbortSignal.timeout(2500)});
    if (!response.ok) throw Error("存档不可用");
    const data = await response.json();
    $("#archive-status").textContent = `${data.connected ? "板卡已连接接收程序" : "等待板卡连接，已存历史仍可查看"} · 共${data.count}条 · 重传去重${data.duplicates}次${data.export_error ? " · 导出失败，请关闭占用CSV的程序后重启05" : ""}`;
    renderEvents(data.events.map(e => ({track_id:e.track, title:trackLabels[["NORMAL","FALLING","FALLEN"][e.state]],
      severity:["info","warning","critical"][e.state], source:"电脑存档", created_at:e.received_utc,
      detail:`事件${e.id} · 开机${e.uptime_ms}ms · 帧${e.frame} · 人体${(e.detector/10).toFixed(1)}% · 规则${(e.rule/1000).toFixed(3)}`,
      location:`(${e.x0}, ${e.y0}) → (${e.x1}, ${e.y1})`,
      snapshot_url:`http://192.168.0.3:8092/image/${e.session}/${e.id}.bmp`, snapshot_kind:e.color ? "rgb" : "gray"})), $("#archive-events"));
  } catch {
    $("#archive-status").textContent = "存档连接中断，请在电脑启动05事件接收程序；板端最近事件仍可查看。";
    $("#archive-events").replaceChildren();
  }
}
globalThis.addEventListener("hashchange", () => { loadTrackingMode(); loadArchive(); });

function eventPhoto(event, saved) {
  const photo = saved.find(e => e.id === event.id && e.track === event.track && e.frame === event.frame && e.uptime_ms === event.uptime_ms && e.state === event.state);
  return photo ? {snapshot_url:`http://192.168.0.3:8092/image/${photo.session}/${photo.id}.bmp`, snapshot_kind:photo.color ? "rgb" : "gray"} : {};
}

function tickClock() {
  $("#clock").textContent = new Intl.DateTimeFormat("zh-CN", { hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false }).format(new Date());
}

$("#demo-frame").addEventListener("error", () => {
  $("#demo-frame").hidden = true;
  $(".video-stage").classList.add("failed");
  $("#video-empty").hidden = false;
});
$("#refresh-events").addEventListener("click", () => { loadEvents(); loadArchive(); });
$("#retry-video").addEventListener("click", () => { videoSignature = ""; loadStatus(); });
for (const selector of ["#stream-video", "#stream-image"]) {
  $(selector).addEventListener(selector === "#stream-video" ? "loadeddata" : "load", () => {
    $("#stream-empty").hidden = true;
  });
  $(selector).addEventListener("error", () => {
    $("#stream-empty").hidden = false;
    $("#video-state").textContent = "视频源不可用";
  });
}
const frames = $(".mobile-frames");
$(".frame-steps li").setAttribute("data-current", "true");
frames.addEventListener("scroll", () => {
  const index = Math.min(frames.children.length - 1, Math.round(frames.scrollLeft / frames.clientWidth));
  $("#frame-count").textContent = `${index + 1} / ${frames.children.length}`;
  document.querySelectorAll(".frame-steps li").forEach((step, position) => {
    if (position === index) step.setAttribute("data-current", "true");
    else step.removeAttribute("data-current");
  });
}, { passive: true });
tickClock();
loadStatus();
loadEvents();
loadTrackingMode();
loadArchive();
if (!new URLSearchParams(location.search).has("snapshot")) {
  setInterval(tickClock, 1000);
  setInterval(loadStatus, 1000);
  setInterval(loadEvents, 5000);
  setInterval(() => { if (!document.hidden && !$("[data-view='monitor']").hidden) loadTrackingMode(); }, 1500);
  setInterval(loadArchive, 5000);
}

// Same interface as the PC monitor; acquisition and HTTP run on the board.
let boardUptime=0, boardReceived=0;
let ethernetVideo={source:"v6-ethernet",connected:false,fps:0,url:""};
async function loadEthernetVideo() {
  try {
    const response=await fetch("http://192.168.0.3:8093/api/video",{cache:"no-store",signal:AbortSignal.timeout(1500)});
    if(!response.ok)throw Error("Video receiver unavailable");
    const info=await response.json();
    if(info.source!=="v6-ethernet" || !Number.isFinite(info.fps))throw Error("Invalid video status");
    ethernetVideo=info;
  } catch {ethernetVideo={source:"v6-ethernet",connected:false,fps:0,url:""};}
  if(previewConfig?.source==="v6-ethernet")renderVideo({...ethernetVideo,detection_fps:previewConfig.detection_fps,detection_age_ms:previewConfig.detection_age_ms});
}
function boardStatus(s) {
  boardUptime=s.uptime_ms;boardReceived=performance.now();
  const names=["NORMAL","FALLING","FALLEN"], sensor=s.sensors;
  const state=s.alarm_latched?2:s.tracks.reduce((v,t)=>Math.max(v,t.state),0);
  return {connected:true,mode:"bridge",source:"FPGA RISC-V / lwIP",protocol_version:1,
    power:s.power,infrared_assist:s.infrared_assist,monitor_reset:s.monitor_reset,overall:names[state],people:s.tracks.filter(t=>t.person_confirmed!==0).length,updated_at:new Date().toISOString(),fps:s.period_ms?1000/s.period_ms:0,
    latency_ms:((s.uptime_ms*1000-s.capture_us)>>>0)/1000,uptime_seconds:s.uptime_ms/1000,
    tracks:s.tracks.map(t=>({id:t.id,state:names[t.state],detector_score:t.detector/1000,rule_score:t.rule/1000,
      fusion_mode:t.weighted?"WEIGHTED":"VISUAL_ONLY",quality:t.quality,person_confirmed:t.person_confirmed})),
    sensors:{ambient_c:sensor.env_valid?sensor.ambient/100:null,humidity_percent:sensor.humidity/100,environment_valid:sensor.env_valid,
      thermal_centi:sensor.heat,thermal_valid:sensor.thermal_valid,thermal_age_ms:sensor.age_us/1000},
    diagnostics:{board:true,thermal_frames:sensor.thermal_frames,rx_bad:s.rx_bad,rx_dropped:s.rx_dropped},
    video:{...ethernetVideo,detection_fps:s.period_ms?1000/s.period_ms:0,detection_age_ms:((s.uptime_ms*1000-s.capture_us)>>>0)/1000}};
}
let frameUrl="",frameTimes=[],lastImageFrame=0,lastImageAt=0;
async function boardPicture() {
  if(previewConfig?.source!=="riscv-lwip" || document.hidden || $("[data-view='monitor']").hidden) {
    setTimeout(boardPicture,200);return;
  }
  try {
    const response=await fetch("/frame.bmp?t="+Date.now(),{cache:"no-store",signal:AbortSignal.timeout(6000)});
    if(!response.ok)throw Error("Frame unavailable");
    const frame=Number(response.headers.get("X-Frame-Id")),blob=await response.blob();
    const image=$("#stream-image"),url=URL.createObjectURL(blob);await new Promise((resolve,reject)=>{image.onload=resolve;image.onerror=reject;image.src=url;});
    if(frameUrl)URL.revokeObjectURL(frameUrl);frameUrl=url;
    image.hidden=false;$("#stream-empty").hidden=true;
    if(frame!==lastImageFrame){lastImageAt=performance.now();frameTimes.push(lastImageAt);frameTimes=frameTimes.slice(-16);lastImageFrame=frame;}
    const fps=frameTimes.length>1?1000*(frameTimes.length-1)/(frameTimes.at(-1)-frameTimes[0]):0;
    $("#video-state").textContent=`板内连续画面 · ${fps.toFixed(1)} 帧/秒`;
  } catch {
    if(performance.now()-lastImageAt>3000){$("#stream-empty").hidden=false;$("#stream-empty strong").textContent="板卡画面中断";$("#stream-empty span").textContent="正在重试，请检查板卡与网线。";}
  } finally {setTimeout(boardPicture,20);}
}
setInterval(()=>{if(boardReceived && performance.now()-boardReceived<3000)$("#uptime").textContent=formatDuration(Math.floor((boardUptime+performance.now()-boardReceived)/1000));},1000);
loadEthernetVideo();
if(!new URLSearchParams(location.search).has("snapshot"))setInterval(loadEthernetVideo,1000);
