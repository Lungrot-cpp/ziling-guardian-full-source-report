static const char board_page[] = R"V6PAGE(<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
  <meta name="theme-color" content="#f7f3ec">
  <title>VF Ti60 · 现场监测</title>
  <link rel="stylesheet" href="/style.css">
</head>
<body class="monitor-app">
  <!--
  THESIS: A quiet monitoring desk with evidence and current state immediately legible, not a decorative Zen landing page.
  OWN-WORLD: The user's sample-09-zen paper, ink, cinnabar and fine-rule language; Songti Chinese and Times New Roman English, soft-cornered surfaces, no shadows. Original hand-drawn people add quiet warmth; line diagrams clarify the data path.
  STORY: Read the latest status, inspect the video with an explicit source label, then scan targets, measurements and photographic event evidence.
  FIRST VIEWPORT: A spacious title and calm state line sit above a video-left/result-right work area; mobile keeps status first and stacks video and photographic events. The offline six-frame reference is collapsed.
  FORM: User-pinned Zen/Wabi-sabi reference adapted to an Operate surface. Seed 807fd807; the pinned reference overrides the roll.
  FINISH: unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance
  -->
  <a class="skip-link" href="#main">跳到监控内容</a>
  <nav class="sidebar" aria-label="功能导航">
    <span class="sidebar-mark" aria-hidden="true">VF</span>
    <a class="nav-item" href="#monitor" data-nav="monitor" aria-current="page" title="现场监测">
      <svg viewBox="0 0 24 24" aria-hidden="true"><path d="m3 10 9-7 9 7M5 9v11h5v-6h4v6h5V9"/></svg><span>现场监测</span>
    </a>
    <a class="nav-item" href="#tracking" data-nav="tracking" title="人员跟踪">
      <svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="9" cy="7" r="3"/><path d="M3 20v-2a6 6 0 0 1 12 0v2M17 4a3 3 0 0 1 0 6m1 3a5 5 0 0 1 3 5v2"/></svg><span>人员跟踪</span>
    </a>
    <a class="nav-item" href="#care" data-nav="care" title="照护控制">
      <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 21V3h12v18M8 21h13M12 12h1M7 6h6M19 7v7m-3-4h6"/></svg><span>照护控制</span>
    </a>
    <a class="nav-item" href="#events" data-nav="events" title="事件记录">
      <svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="9"/><path d="M12 6v6l-3 3"/></svg><span>事件记录</span>
    </a>
    <a class="nav-item" href="#device" data-nav="device" title="设备状态">
      <svg viewBox="0 0 24 24" aria-hidden="true"><rect x="5" y="5" width="14" height="14" rx="2"/><rect x="9" y="9" width="6" height="6" rx="1"/><path d="M9 2v3m6-3v3M9 19v3m6-3v3M2 9h3m-3 6h3m14-6h3m-3 6h3"/></svg><span>设备状态</span>
    </a>
    <span class="sidebar-note" aria-hidden="true">Ti60</span>
  </nav>
  <div class="app-shell">
  <header class="topbar">
    <div class="topbar-inner">
      <a class="brand" href="#monitor" aria-label="VF Ti60 现场监测首页"><strong>VF</strong><span>Ti60 视觉监测</span></a>
      <div class="top-status"><span class="source-pill" id="source-pill"><i></i><span>连接中</span></span><time id="clock">--:--:--</time></div>
    </div>
  </header>

  <main id="main" tabindex="-1">
    <div class="page-heading">
      <div class="heading-intro"><div><h1 id="page-title" tabindex="-1">现场监测</h1></div><img id="page-art" class="editorial-art observer-art" src="/media/observer.png" width="144" height="144" alt="" aria-hidden="true"></div>
        <div class="page-meta"><span>当前设备</span><strong>VF-Ti60F225</strong></div>
    </div>

    <section class="overall normal" id="overall" aria-label="当前判定">
      <div><span id="overall-kicker">板端判定</span><strong id="overall-label">状态正常</strong></div>
      <time id="updated">--:--:--</time>
      <span class="sr-only" id="state-announcement" role="status" aria-live="polite"></span>
    </section>

    <div class="tracking-controls">
      <button type="button" id="reset-monitor" aria-describedby="reset-monitor-status" disabled>重新测试</button>
      <span id="reset-monitor-status" class="control-note" role="status">清除本轮报警，准备下一次测试</span>
    </div>
    <div class="page-view monitor-grid" data-view="monitor">
      <section class="video-panel surface" aria-labelledby="video-title">
        <div class="surface-heading"><h2 id="video-title">视频画面</h2><span class="record-badge" id="video-state">等待视频源</span></div>
        <div class="live-stage">
          <video id="stream-video" controls playsinline preload="metadata"  aria-label="当前视频画面"></video>
          <!-- impeccable-disable-next-line broken-image: hidden until renderVideo assigns the configured MJPEG source -->
          <img id="stream-image" alt="当前摄像头视频流" hidden>
          <div class="video-empty" id="stream-empty" hidden><strong>视频源不可用</strong><span>请检查摄像头或网关视频地址。</span></div>
        </div>
        <p class="telemetry-detail"><strong id="monitor-power-mode" role="status">工作模式：等待板卡</strong><br><span id="monitor-power-detail">等待低功耗状态</span></p>
        <div class="video-caption"><button id="retry-video" type="button">重新连接</button></div>
        <details class="test-reference">
        <summary>离线测试参考 · 六帧</summary>
        <div class="video-stage">
          <img id="demo-frame" src="/media/demo-frame.jpg" alt="同屏两人跌倒检测六帧测试序列">
          <div class="mobile-frames" tabindex="0" aria-label="六帧离线跌倒测试序列，左右滑动查看">
            <div role="img" aria-label="第 1 帧：两人站立，检测框为绿色" style="--x:0%;--y:0%"></div>
            <div role="img" aria-label="第 2 帧：前方人员开始弯腰" style="--x:50%;--y:0%"></div>
            <div role="img" aria-label="第 3 帧：前方人员下落，出现黄色疑似跌倒框" style="--x:100%;--y:0%"></div>
            <div role="img" aria-label="第 4 帧：目标倒地，检测框变红" style="--x:0%;--y:100%"></div>
            <div role="img" aria-label="第 5 帧：倒地状态持续，红框仍在" style="--x:50%;--y:100%"></div>
            <div role="img" aria-label="第 6 帧：倒地目标仍以红框标注" style="--x:100%;--y:100%"></div>
          </div>
          <div class="video-empty" id="video-empty" hidden><strong>测试画面不可用</strong><span>遥测和事件记录仍可继续查看。</span></div>
        </div>
        <ol class="frame-steps" aria-label="离线测试序列阶段">
          <li><span>1</span><small>站立</small></li><li><span>2</span><small>弯腰</small></li><li class="warning"><span>3</span><small>下落</small></li><li class="danger"><span>4</span><small>倒地</small></li><li class="danger"><span>5</span><small>持续</small></li><li class="danger"><span>6</span><small>持续</small></li>
        </ol>
        <p class="mobile-hint"><span id="frame-count">1 / 6</span></p>
        <div class="video-caption"><a href="/media/demo-frame.jpg" target="_blank" rel="noopener">查看原图</a></div>
        </details>
      </section>

      <aside class="decision-panel surface" aria-labelledby="decision-title">
        <div class="surface-heading"><h2 id="decision-title">判定结果</h2><span class="people-count" id="people">0 人</span></div>
        <div class="tracking-controls" role="group" aria-label="人员检测模式">
          <button type="button" id="mode-single" aria-pressed="false" disabled>单人模式</button>
          <button type="button" id="mode-multi" aria-pressed="false" disabled>多人模式</button>
          <a href="#tracking">查看轨迹</a>
        </div>
        <p id="tracking-status" class="control-note" role="status">正在读取人员模式…</p>
        <div class="tracking-controls">
          <button type="button" id="infrared-toggle" role="switch" aria-checked="true" aria-describedby="infrared-status" disabled>红外辅助识别</button>
        </div>
        <p id="infrared-status" class="control-note" role="status">正在读取红外开关状态…</p>
        <div class="track-head">跟踪目标</div>
        <div class="tracks" id="tracks"><div class="loading-row">正在接收数据…</div></div>
        <section class="thermal-panel" aria-labelledby="thermal-title">
          <h3 id="thermal-title">红外与环境</h3>
          <canvas id="thermal-map" width="32" height="24" role="img" aria-label="32×24 红外表面温度热图，等待真实数据"></canvas>
          <p id="thermal-note">等待真实红外数据</p>
          <dl class="sensor-readings"><div><dt>环境温度</dt><dd id="ambient">--</dd></div><div><dt>相对湿度</dt><dd id="humidity">--</dd></div></dl>
        </section>
        <img class="editorial-art companions-art" src="/media/companions.png" width="116" height="116" alt="" aria-hidden="true">
      </aside>
    </div>

    <div class="page-view" data-view="tracking" hidden>
      
      <iframe class="control-frame" id="tracking-frame" data-src="/tracking.html?embedded=1" title="人员跟踪与模式控制"></iframe>
    </div>

    <div class="page-view" data-view="care" hidden>
      
      <iframe class="control-frame" id="care-frame" data-src="/care.html?embedded=1" title="位置灯板、模拟门与救援控制"></iframe>
    </div>

    <div class="page-view" data-view="events" hidden>
      <section class="events-panel surface" aria-labelledby="archive-title">
        <div class="section-heading"><div><h2 id="archive-title">已保存事件与抓拍</h2></div><a class="archive-download" href="http://192.168.0.3:8092/events.csv" download>下载完整CSV</a></div>
        <p id="archive-status" class="control-note" role="status">正在连接事件接收程序…</p>
        <div class="events" id="archive-events"><div class="loading-row">正在读取存档…</div></div>
      </section>
      <section class="events-panel surface" aria-labelledby="events-title">
        <div class="section-heading"><div><h2 id="events-title">板端最近事件</h2></div><button type="button" id="refresh-events">刷新记录</button></div>
        <div class="events" id="events"><div class="loading-row">正在读取事件…</div></div>
      </section>
    </div>

    <div class="page-view" data-view="device" hidden>
    <section class="data-strip surface" aria-labelledby="telemetry-title">
      <div class="strip-intro"><h2 id="telemetry-title">运行读数</h2></div>
      <dl>
        <div><dt><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M8 3H3V8M16 3H21V8M3 16V21H8M16 21H21V16M9 8l7 4-7 4Z"/></svg>检测更新率</dt><dd><span id="fps">--</span><small>fps</small></dd></div>
        <div><dt><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M3 8H20l-4-4M21 16H4l4 4M8 12H16"/></svg>采集至处理</dt><dd><span id="latency">--</span><small>ms</small></dd></div>
        <div><dt><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M9 14V5a3 3 0 0 1 6 0v9a5 5 0 1 1-6 0ZM12 8V18M17 6H20M17 10H19"/></svg>环境温度</dt><dd><span id="temperature">--</span><small>°C</small></dd></div>
        <div><dt><svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="9"/><path d="M12 6V12l4 2"/></svg>服务运行</dt><dd id="uptime">--</dd></div>
      </dl>
    </section>

    <div class="device-grid">
      <section class="connection-panel surface" aria-labelledby="connection-title">
        <div class="section-heading"><div><h2 id="connection-title">连接链路</h2></div></div>
        <figure class="device-map simulation" id="device-map" aria-label="遥测数据链路示意">
          <svg viewBox="0 0 300 72" aria-hidden="true" focusable="false">
            <g class="map-source"><rect x="12" y="14" width="48" height="40" rx="2"/><rect x="25" y="24" width="22" height="20"/><path d="M29 19V24M36 19V24M43 19V24M29 44V49M36 44V49M43 44V49M20 28H25M20 35H25M47 28H52M47 35H52"/><circle cx="17" cy="19" r="1"/><circle cx="55" cy="49" r="1"/></g>
            <path class="source-wire" d="M68 34H118"/><path class="map-arrow" d="m113 30 5 4-5 4"/>
            <g><rect x="126" y="15" width="48" height="35" rx="2"/><path d="M135 57H165M150 50V57M135 27l5 4-5 4M145 36H158"/></g>
            <path d="M182 34H232"/><path class="map-arrow" d="m227 30 5 4-5 4"/><circle class="map-packet" cx="207" cy="34" r="3"/>
            <g><rect x="240" y="14" width="48" height="40" rx="2"/><path d="M240 23H288M246 19H249M254 19H257M247 32H261M247 39H280M247 46H272"/></g>
          </svg>
          <div class="map-labels"><span id="path-source">VF 板卡 · 待接入</span><span>RISC-V HTTP</span><span>浏览器</span></div>
        </figure>
        <p id="diagnostics-note" class="telemetry-detail">等待诊断数据</p>
        <p id="power-state" class="telemetry-detail">轻量监视：等待板卡数据</p>
        <p id="power-metrics" class="telemetry-detail">等待采样</p>
        <div class="section-heading"><button type="button" id="power-toggle" disabled>等待工作模式</button></div>
        <div class="source-detail"><span>遥测来源</span><strong id="source-name">正在连接</strong><span class="connection" id="connection">--</span></div>
      </section>
      <section class="signal-panel surface" aria-labelledby="signal-title">
        <div class="section-heading"><div><h2 id="signal-title">温度变化</h2></div></div>
        <svg class="chart" id="chart" viewBox="0 0 600 160" preserveAspectRatio="none" role="img" aria-label="环境温度趋势图"><path class="chart-grid" d="M0 20H600M0 70H600M0 120H600"/><path class="chart-area" id="chart-area" d=""/><path class="chart-line" id="chart-line" d=""/><circle class="chart-tip" id="chart-tip" cx="596" cy="80" r="4" hidden/></svg>
        <div class="chart-axis"><span>较早读数</span><span>最近读数</span></div>
      </section>
    </div>
    </div>
  </main>
  </div>
  <script src="/privacy.js" defer></script>
  <script src="/app.js" defer></script>
</body>
</html>
)V6PAGE";
