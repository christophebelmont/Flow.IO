/**
 * @file WebInterfaceModule.cpp
 * @brief Web interface bridge for Supervisor profile.
 */

#include "WebInterfaceModule.h"

#define LOG_TAG "WebServr"
#include "Core/ModuleLog.h"

#include <string.h>
#include <stdlib.h>
#include "Core/DataKeys.h"
#include "Core/EventBus/EventPayloads.h"
#include "Modules/Network/WifiModule/WifiRuntime.h"

static void sanitizeJsonString_(char* s)
{
    if (!s) return;
    for (size_t i = 0; s[i] != '\0'; ++i) {
        if (s[i] == '"' || s[i] == '\\' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t') {
            s[i] = ' ';
        }
    }
}

static const char kFlowIoLogoSvg[] PROGMEM = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1200 360" role="img" aria-labelledby="title desc">
  <title id="title">Flow.IO Logo</title>
  <desc id="desc">Logo Flow.IO with water drop and waves.</desc>
  <defs>
    <linearGradient id="gradDrop" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#21C7FF"/>
      <stop offset="100%" stop-color="#0066FF"/>
    </linearGradient>
    <linearGradient id="gradWave" x1="0%" y1="0%" x2="100%" y2="0%">
      <stop offset="0%" stop-color="#00AEEF"/>
      <stop offset="100%" stop-color="#1ED6A6"/>
    </linearGradient>
    <filter id="softShadow" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="4" stdDeviation="6" flood-color="#001A33" flood-opacity="0.2"/>
    </filter>
  </defs>

  <rect x="0" y="0" width="1200" height="360" fill="#FFFFFF"/>

  <g transform="translate(70,40)" filter="url(#softShadow)">
    <circle cx="140" cy="140" r="116" fill="#FFFFFF"/>
    <circle cx="140" cy="140" r="112" fill="none" stroke="#D7E9F8" stroke-width="2"/>

    <path d="M140 40
             C140 40, 72 116, 72 170
             C72 222, 106 252, 140 252
             C174 252, 208 222, 208 170
             C208 116, 140 40, 140 40 Z"
          fill="url(#gradDrop)"/>

    <path d="M82 168
             C98 154, 116 152, 134 160
             C152 168, 170 170, 196 158"
          fill="none" stroke="url(#gradWave)" stroke-width="10" stroke-linecap="round"/>

    <path d="M86 196
             C106 184, 126 184, 146 192
             C166 200, 182 200, 196 194"
          fill="none" stroke="url(#gradWave)" stroke-width="8" stroke-linecap="round" opacity="0.95"/>

  </g>

  <g transform="translate(340,94)">
    <text x="0" y="110" fill="#073B66" font-family="Avenir Next, Montserrat, Segoe UI, Arial, sans-serif" font-size="132" font-weight="800" letter-spacing="1">
      FLOW
    </text>
    <text x="420" y="110" fill="#00AEEF" font-family="Avenir Next, Montserrat, Segoe UI, Arial, sans-serif" font-size="132" font-weight="800">
      .
    </text>
    <text x="470" y="110" fill="#073B66" font-family="Avenir Next, Montserrat, Segoe UI, Arial, sans-serif" font-size="132" font-weight="800">
      IO
    </text>
    <text x="4" y="156" fill="#3C6E91" font-family="Avenir Next, Montserrat, Segoe UI, Arial, sans-serif" font-size="36" font-weight="500" letter-spacing="4">
      SMART POOL WATER MANAGEMENT
    </text>
  </g>
</svg>
)SVG";

static const char kFlowIoIconSvg[] PROGMEM = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" role="img" aria-labelledby="title desc">
  <title id="title">Flow.IO App Icon</title>
  <desc id="desc">Flow.IO icon with drop and waves.</desc>
  <defs>
    <linearGradient id="dropGrad" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#21C7FF"/>
      <stop offset="100%" stop-color="#0066FF"/>
    </linearGradient>
    <linearGradient id="waveGrad" x1="0%" y1="0%" x2="100%" y2="0%">
      <stop offset="0%" stop-color="#00AEEF"/>
      <stop offset="100%" stop-color="#1ED6A6"/>
    </linearGradient>
  </defs>
  <rect x="24" y="24" width="464" height="464" rx="110" fill="#F6FBFF"/>
  <rect x="24" y="24" width="464" height="464" rx="110" fill="none" stroke="#D6E9F8" stroke-width="4"/>
  <path d="M256 102
           C256 102, 142 224, 142 306
           C142 382, 196 424, 256 424
           C316 424, 370 382, 370 306
           C370 224, 256 102, 256 102 Z"
        fill="url(#dropGrad)"/>
  <path d="M164 298
           C188 280, 216 276, 246 288
           C276 300, 306 304, 350 286"
        fill="none" stroke="url(#waveGrad)" stroke-width="20" stroke-linecap="round"/>
  <path d="M170 346
           C198 330, 228 330, 260 342
           C292 354, 320 356, 348 346"
        fill="none" stroke="url(#waveGrad)" stroke-width="16" stroke-linecap="round" opacity="0.95"/>
</svg>
)SVG";

static const char kWebInterfacePage[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Flow.IO Supervisor Interface</title>
  <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@24,400,0,0" />
  <style>
    :root {
      --md-bg: #fffbfe;
      --md-surface: #fffbfe;
      --md-on-surface: #1d1b20;
      --md-outline: #79747e;
      --md-primary: #6750a4;
      --md-on-primary: #ffffff;
      --md-secondary-container: #e8def8;
      --md-on-secondary-container: #1d192b;
      --md-surface-variant: #e7e0ec;
      --term-bg: #020617;
      --term-fg: #e2e8f0;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      font-family: "Inter", "Segoe UI", Roboto, Arial, sans-serif;
      background: var(--md-bg);
      color: var(--md-on-surface);
    }
    .app { display: flex; min-height: 100vh; }
    .drawer {
      width: 280px;
      padding: 12px;
      border-right: 1px solid rgba(121,116,126,0.35);
      background: #ffffff;
      overflow: hidden;
      transition: width 0.2s ease;
      z-index: 20;
      display: flex;
      flex-direction: column;
    }
    .drawer.collapsed { width: 78px; }
    .drawer-header { display: flex; align-items: center; gap: 10px; margin-bottom: 8px; }
    .menu-btn {
      width: 42px; height: 42px; border: 0; border-radius: 12px;
      background: var(--md-secondary-container); color: var(--md-on-secondary-container);
      cursor: pointer; font-size: 18px; line-height: 1;
    }
    .drawer-user {
      font-size: 18px;
      font-weight: 700;
      color: #073b66;
      line-height: 1;
      white-space: nowrap;
    }
    .drawer.collapsed .drawer-user { display: none; }
    .menu-group { display: grid; gap: 6px; margin-top: 10px; }
    .drawer-footer {
      margin-top: auto;
      display: flex;
      justify-content: center;
      padding-top: 14px;
    }
    .app-icon-shell {
      width: 100%;
      max-width: 210px;
      border-radius: 10px;
      background: #ffffff;
      border: 1px solid #d6e9f8;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 6px 8px;
    }
    .app-icon {
      width: 100%;
      max-width: 170px;
      height: auto;
      display: block;
    }
    .menu-item {
      border: 0; border-radius: 14px; padding: 12px 14px;
      display: flex; align-items: center; gap: 12px; cursor: pointer;
      background: transparent; color: var(--md-on-surface); text-align: left;
      font-size: 14px; font-weight: 500; white-space: nowrap;
    }
    .menu-item.active { background: var(--md-secondary-container); color: var(--md-on-secondary-container); font-weight: 600; }
    .menu-item .ico {
      width: 20px;
      text-align: center;
      display: inline-flex;
      justify-content: center;
      align-items: center;
    }
    .material-symbols-outlined {
      font-size: 20px;
      line-height: 1;
      font-variation-settings: 'FILL' 0, 'wght' 400, 'GRAD' 0, 'opsz' 24;
    }
    .drawer.collapsed .menu-item .label { display: none; }
    @media (min-width: 901px) {
      .drawer.collapsed .drawer-header { justify-content: center; }
      .drawer.collapsed .menu-group { justify-items: center; }
      .drawer.collapsed .menu-btn,
      .drawer.collapsed .menu-item {
        width: 48px;
        min-width: 48px;
        height: 42px;
        padding-left: 0;
        padding-right: 0;
        justify-content: center;
      }
      .drawer.collapsed .menu-item .ico { width: auto; }
      .drawer.collapsed .app-icon-shell {
        width: 48px;
        max-width: 48px;
        min-height: 48px;
        padding: 8px 6px;
        border-radius: 14px;
      }
      .drawer.collapsed .app-icon {
        max-width: 100%;
      }
    }
    .content { flex: 1; padding: 18px; min-width: 0; }
    .mobile-topbar { display: none; }
    .mobile-topbar .mobile-title {
      font-size: 16px;
      font-weight: 700;
      color: #073b66;
      line-height: 1;
    }
    .page { display: none; }
    .page.active { display: block; }
    .topbar { display: flex; align-items: center; justify-content: space-between; margin-bottom: 12px; gap: 12px; }
    .topbar h1 { margin: 0; font-size: 20px; font-weight: 700; }
    .status-chip {
      font-size: 12px; padding: 7px 10px; border-radius: 999px;
      background: var(--md-surface-variant);
    }
    .card {
      background: rgba(255,251,254,0.95);
      border: 1px solid rgba(121,116,126,0.35);
      border-radius: 20px;
      padding: 14px;
    }
    .terminal {
      border: 1px solid #334155;
      background: var(--term-bg);
      color: var(--term-fg);
      border-radius: 10px;
      height: 66vh;
      overflow: auto;
      padding: 10px;
      white-space: pre-wrap;
      font-family: "Cascadia Mono", "JetBrains Mono", "Fira Code", "SFMono-Regular", Menlo, Monaco, Consolas, monospace;
      font-size: 11px;
      line-height: 1.25;
    }
    .log-line { white-space: pre-wrap; }
    .term-toolbar { display: flex; gap: 8px; margin-top: 10px; }
    .term-toolbar input { flex: 1; }
    .btn-toggle-off {
      background: #eef2f7;
      color: #5b6673;
      border-color: rgba(121,116,126,0.35);
    }
    .form-grid { display: grid; gap: 12px; grid-template-columns: repeat(2, minmax(220px, 1fr)); }
    .field { display: grid; gap: 6px; min-width: 0; }
    .field.full { grid-column: 1 / -1; }
    .field label { font-size: 12px; font-weight: 600; opacity: 0.78; }
    input, button {
      border-radius: 12px; border: 1px solid rgba(121,116,126,0.45);
      background: white; color: var(--md-on-surface); font: inherit; padding: 10px 12px;
    }
    input:focus { outline: 2px solid rgba(103,80,164,0.4); border-color: var(--md-primary); }
    .btn-row { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 12px; }
    button { cursor: pointer; }
    .btn-primary { background: var(--md-primary); color: var(--md-on-primary); border-color: var(--md-primary); font-weight: 600; }
    .btn-tonal { background: var(--md-secondary-container); color: var(--md-on-secondary-container); border-color: transparent; font-weight: 600; }
    .upgrade-status {
      margin-top: 10px; padding: 10px; border-radius: 12px;
      background: rgba(232,222,248,0.7); font-size: 13px;
    }
    .upgrade-progress {
      margin-top: 12px;
      height: 8px;
      border-radius: 999px;
      overflow: hidden;
      background: var(--md-surface-variant);
      border: 1px solid rgba(121,116,126,0.22);
    }
    .upgrade-progress-bar {
      height: 100%;
      width: 0%;
      border-radius: inherit;
      background: linear-gradient(90deg, #6750a4 0%, #7f67be 100%);
      transition: width 0.2s ease;
    }
    .config-status {
      margin-top: 10px;
      padding: 10px;
      border-radius: 12px;
      background: rgba(214, 233, 248, 0.8);
      font-size: 13px;
      color: #073b66;
    }
    .control-list {
      display: grid;
      gap: 6px;
      max-width: 640px;
      margin: 0 auto;
    }
    .control-item {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
      padding: 10px 4px;
      border: none;
      background: transparent;
    }
    .control-name {
      font-size: 14px;
      font-weight: 600;
      color: #073b66;
    }
    .control-card {
      border: none;
      background: transparent;
      padding: 6px 0;
    }
    .md3-switch {
      position: relative;
      width: 52px;
      height: 32px;
      flex: 0 0 auto;
    }
    .md3-switch input {
      position: absolute;
      inset: 0;
      margin: 0;
      padding: 0;
      border: 0;
      opacity: 0;
      cursor: pointer;
      appearance: none;
      -webkit-appearance: none;
    }
    .md3-track {
      position: absolute;
      inset: 0;
      border-radius: 999px;
      border: 2px solid #7c8ea4;
      background: #e5edf5;
      transition: all 0.18s ease;
    }
    .md3-thumb {
      position: absolute;
      top: 50%;
      left: 8px;
      width: 14px;
      height: 14px;
      border-radius: 50%;
      background: #3e4f62;
      transform: translateY(-50%);
      transition: all 0.18s ease;
    }
    .md3-switch input:checked + .md3-track {
      border-color: #0066ff;
      background: #9dc7ff;
    }
    .md3-switch input:checked + .md3-track + .md3-thumb {
      left: 30px;
      background: #ffffff;
    }
    .md3-switch input:focus-visible + .md3-track {
      box-shadow: 0 0 0 3px rgba(0, 102, 255, 0.25);
    }
    .overlay { position: fixed; inset: 0; background: rgba(24,20,38,0.25); display: none; z-index: 10; }
    @media (max-width: 900px) {
      .drawer {
        position: fixed; left: 0; top: 0; bottom: 0;
        width: min(280px, calc(100vw - 32px));
        transform: translateX(-104%);
        transition: transform 0.2s ease;
        box-shadow: 0 10px 28px rgba(7, 59, 102, 0.22);
      }
      .drawer.mobile-open { transform: translateX(0); }
      .drawer.collapsed { width: 280px; }
      .drawer.collapsed .drawer-user, .drawer.collapsed .menu-item .label { display: inline; }
      .overlay.visible { display: block; }
      .content { width: 100%; padding: 12px; }
      .mobile-topbar {
        position: sticky;
        top: 0;
        z-index: 5;
        display: flex;
        align-items: center;
        gap: 10px;
        margin: -12px -12px 12px -12px;
        padding: 10px 12px;
        border-bottom: 1px solid rgba(121,116,126,0.35);
        background: #ffffff;
      }
      .form-grid { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <div id="overlay" class="overlay"></div>
  <div class="app">
    <aside id="drawer" class="drawer">
      <div class="drawer-header">
        <button id="menuToggle" class="menu-btn" data-menu-toggle aria-label="Toggle menu">=</button>
        <div class="drawer-user">Admin</div>
      </div>
      <nav class="menu-group">
        <button class="menu-item active" data-page="page-terminal"><span class="ico material-symbols-outlined">terminal</span><span class="label">Journaux</span></button>
        <button class="menu-item" data-page="page-upgrade"><span class="ico material-symbols-outlined">update</span><span class="label">Mise à jour Firmware</span></button>
        <button class="menu-item" data-page="page-config"><span class="ico material-symbols-outlined">settings</span><span class="label">Configuration</span></button>
        <button class="menu-item" data-page="page-control"><span class="ico material-symbols-outlined">pool</span><span class="label">Control</span></button>
      </nav>
      <div class="drawer-footer">
        <span class="app-icon-shell"><img class="app-icon" src="/assets/flowio-logo.svg" alt="Flow.IO" /></span>
      </div>
    </aside>

    <main class="content">
      <div class="mobile-topbar">
        <button class="menu-btn" data-menu-toggle aria-label="Open menu">=</button>
        <div class="mobile-title">Admin</div>
      </div>
      <section id="page-terminal" class="page active">
        <div class="topbar">
          <h1>Journaux</h1>
          <span id="wsStatus" class="status-chip">connecting...</span>
        </div>
        <div class="card">
          <div id="term" class="terminal"></div>
          <div class="term-toolbar">
            <button id="toggleAutoscroll" class="btn-tonal" aria-pressed="true">Auto-scroll: ON</button>
            <input id="line" placeholder="Send line to UART" />
            <button id="send" class="btn-tonal">Send</button>
            <button id="clear">Clear</button>
          </div>
        </div>
      </section>

      <section id="page-upgrade" class="page">
        <div class="topbar">
          <h1>Mise à jour Firmware</h1>
          <span id="upStatusChip" class="status-chip">idle</span>
        </div>
        <div class="card">
          <div class="form-grid">
            <div class="field full">
              <label for="updateHost">HTTP Server (hostname or IP, optional protocol)</label>
              <input id="updateHost" placeholder="e.g. 192.168.1.20 or http://192.168.1.20" />
            </div>
            <div class="field">
              <label for="flowPath">Flow.IO image path</label>
              <input id="flowPath" placeholder="/build/FlowIO.bin" />
            </div>
            <div class="field">
              <label for="nextionPath">Nextion image path</label>
              <input id="nextionPath" placeholder="/build/Nextion.tft" />
            </div>
          </div>
          <div class="btn-row">
            <button id="saveCfg" class="btn-tonal">Save Config</button>
            <button id="upFlow" class="btn-primary">Upgrade Flow.IO</button>
            <button id="upNextion" class="btn-primary">Upgrade Nextion</button>
            <button id="refreshState">Refresh Status</button>
          </div>
          <div class="upgrade-progress" aria-label="Progression mise à jour firmware">
            <div id="upgradeProgressBar" class="upgrade-progress-bar"></div>
          </div>
          <div id="upgradeStatusText" class="upgrade-status">No operation running.</div>
        </div>
      </section>

      <section id="page-config" class="page">
        <div class="topbar">
          <h1>Configuration</h1>
          <span class="status-chip">MQTT</span>
        </div>
        <div class="card">
          <div class="form-grid">
            <div class="field full">
              <label for="mqttServer">Serveur MQTT</label>
              <input id="mqttServer" placeholder="e.g. 192.168.1.20" />
            </div>
            <div class="field">
              <label for="mqttPort">Port</label>
              <input id="mqttPort" type="number" min="1" max="65535" placeholder="1883" />
            </div>
            <div class="field">
              <label for="mqttUser">Username</label>
              <input id="mqttUser" placeholder="username" />
            </div>
            <div class="field full">
              <label for="mqttPass">Password</label>
              <input id="mqttPass" type="password" placeholder="password" />
            </div>
          </div>
          <div class="btn-row">
            <button id="applyMqttCfg" class="btn-primary">Appliquer</button>
          </div>
          <div id="mqttConfigStatus" class="config-status">Configuration MQTT prête.</div>
        </div>
      </section>

      <section id="page-control" class="page">
        <div class="topbar">
          <h1>Control</h1>
          <span class="status-chip">manual</span>
        </div>
        <div class="card control-card">
          <div class="control-list">
            <label class="control-item"><span class="control-name">Remplissage</span><span class="md3-switch"><input type="checkbox" /><span class="md3-track"></span><span class="md3-thumb"></span></span></label>
            <label class="control-item"><span class="control-name">Electrolyseur</span><span class="md3-switch"><input type="checkbox" /><span class="md3-track"></span><span class="md3-thumb"></span></span></label>
            <label class="control-item"><span class="control-name">Filtration</span><span class="md3-switch"><input type="checkbox" /><span class="md3-track"></span><span class="md3-thumb"></span></span></label>
            <label class="control-item"><span class="control-name">Robot</span><span class="md3-switch"><input type="checkbox" /><span class="md3-track"></span><span class="md3-thumb"></span></span></label>
            <label class="control-item"><span class="control-name">Pompe pH</span><span class="md3-switch"><input type="checkbox" /><span class="md3-track"></span><span class="md3-thumb"></span></span></label>
            <label class="control-item"><span class="control-name">Pompe Chlore</span><span class="md3-switch"><input type="checkbox" /><span class="md3-track"></span><span class="md3-thumb"></span></span></label>
            <label class="control-item"><span class="control-name">Eclairage</span><span class="md3-switch"><input type="checkbox" /><span class="md3-track"></span><span class="md3-thumb"></span></span></label>
            <label class="control-item"><span class="control-name">Régulation pH</span><span class="md3-switch"><input type="checkbox" /><span class="md3-track"></span><span class="md3-thumb"></span></span></label>
            <label class="control-item"><span class="control-name">Régulation Orp</span><span class="md3-switch"><input type="checkbox" /><span class="md3-track"></span><span class="md3-thumb"></span></span></label>
            <label class="control-item"><span class="control-name">Mode automatique</span><span class="md3-switch"><input type="checkbox" /><span class="md3-track"></span><span class="md3-thumb"></span></span></label>
            <label class="control-item"><span class="control-name">Hivernage</span><span class="md3-switch"><input type="checkbox" /><span class="md3-track"></span><span class="md3-thumb"></span></span></label>
          </div>
        </div>
      </section>
    </main>
  </div>

  <script>
    const drawer = document.getElementById('drawer');
    const overlay = document.getElementById('overlay');
    const menuToggles = Array.from(document.querySelectorAll('[data-menu-toggle]'));
    const menuItems = Array.from(document.querySelectorAll('.menu-item'));
    const pages = Array.from(document.querySelectorAll('.page'));

    function isMobileLayout() {
      return window.innerWidth <= 900;
    }

    function setMobileDrawerOpen(open) {
      drawer.classList.toggle('mobile-open', open);
      overlay.classList.toggle('visible', open);
    }

    function closeMobileDrawer() {
      if (isMobileLayout()) {
        setMobileDrawerOpen(false);
      }
    }

    function showPage(pageId) {
      pages.forEach((el) => el.classList.toggle('active', el.id === pageId));
      menuItems.forEach((el) => el.classList.toggle('active', el.dataset.page === pageId));
      closeMobileDrawer();
    }

    menuItems.forEach((item) => item.addEventListener('click', () => showPage(item.dataset.page)));

    menuToggles.forEach((btn) => btn.addEventListener('click', () => {
      if (isMobileLayout()) {
        setMobileDrawerOpen(!drawer.classList.contains('mobile-open'));
      } else {
        drawer.classList.toggle('collapsed');
      }
    }));

    overlay.addEventListener('click', closeMobileDrawer);
    window.addEventListener('resize', () => {
      if (!isMobileLayout()) {
        setMobileDrawerOpen(false);
      }
    });

    const term = document.getElementById('term');
    const wsStatus = document.getElementById('wsStatus');
    const line = document.getElementById('line');
    const sendBtn = document.getElementById('send');
    const clearBtn = document.getElementById('clear');
    const toggleAutoscrollBtn = document.getElementById('toggleAutoscroll');
    let autoScrollEnabled = true;

    const updateHost = document.getElementById('updateHost');
    const flowPath = document.getElementById('flowPath');
    const nextionPath = document.getElementById('nextionPath');
    const saveCfgBtn = document.getElementById('saveCfg');
    const upFlowBtn = document.getElementById('upFlow');
    const upNextionBtn = document.getElementById('upNextion');
    const refreshStateBtn = document.getElementById('refreshState');
    const upgradeStatusText = document.getElementById('upgradeStatusText');
    const upgradeProgressBar = document.getElementById('upgradeProgressBar');
    const upStatusChip = document.getElementById('upStatusChip');

    const mqttServer = document.getElementById('mqttServer');
    const mqttPort = document.getElementById('mqttPort');
    const mqttUser = document.getElementById('mqttUser');
    const mqttPass = document.getElementById('mqttPass');
    const applyMqttCfgBtn = document.getElementById('applyMqttCfg');
    const mqttConfigStatus = document.getElementById('mqttConfigStatus');

    const wsProto = location.protocol === 'https:' ? 'wss' : 'ws';
    const ws = new WebSocket(wsProto + '://' + location.host + '/wsserial');

    ws.onopen = () => wsStatus.textContent = 'connected';
    ws.onclose = () => wsStatus.textContent = 'disconnected';
    ws.onerror = () => wsStatus.textContent = 'error';

    const ansiState = { fg: null };
    const ansiFgMap = {
      30: '#94a3b8', 31: '#ef4444', 32: '#22c55e', 33: '#f59e0b',
      34: '#60a5fa', 35: '#f472b6', 36: '#22d3ee', 37: '#e2e8f0',
      90: '#64748b', 91: '#f87171', 92: '#4ade80', 93: '#fbbf24',
      94: '#93c5fd', 95: '#f9a8d4', 96: '#67e8f9', 97: '#f8fafc'
    };
    const ansiRe = /\u001b\[([0-9;]*)m/g;

    function applySgrCodes(rawCodes) {
      const codes = rawCodes === '' ? [0] : rawCodes.split(';').map((v) => parseInt(v, 10)).filter(Number.isFinite);
      for (const code of codes) {
        if (code === 0 || code === 39) {
          ansiState.fg = null;
          continue;
        }
        if (Object.prototype.hasOwnProperty.call(ansiFgMap, code)) {
          ansiState.fg = ansiFgMap[code];
        }
      }
    }

    function decodeAnsiLine(rawLine) {
      let out = '';
      let cursor = 0;
      let lineColor = ansiState.fg;
      rawLine.replace(ansiRe, (full, codes, idx) => {
        out += rawLine.slice(cursor, idx);
        applySgrCodes(codes);
        if (ansiState.fg) lineColor = ansiState.fg;
        cursor = idx + full.length;
        return '';
      });
      out += rawLine.slice(cursor);
      return { text: out, color: lineColor };
    }

    ws.onmessage = (ev) => {
      const raw = String(ev.data || '');
      const parsed = decodeAnsiLine(raw);
      const row = document.createElement('div');
      row.className = 'log-line';
      if (parsed.color) row.style.color = parsed.color;
      row.textContent = parsed.text;
      term.appendChild(row);
      while (term.childNodes.length > 2000) term.removeChild(term.firstChild);
      if (autoScrollEnabled) term.scrollTop = term.scrollHeight;
    };

    function refreshAutoscrollUi() {
      toggleAutoscrollBtn.textContent = autoScrollEnabled ? 'Auto-scroll: ON' : 'Auto-scroll: OFF';
      toggleAutoscrollBtn.setAttribute('aria-pressed', autoScrollEnabled ? 'true' : 'false');
      toggleAutoscrollBtn.classList.toggle('btn-tonal', autoScrollEnabled);
      toggleAutoscrollBtn.classList.toggle('btn-toggle-off', !autoScrollEnabled);
    }

    function sendLine() {
      const txt = line.value;
      if (!txt) return;
      if (ws.readyState === WebSocket.OPEN) ws.send(txt);
      line.value = '';
      line.focus();
    }
    sendBtn.addEventListener('click', sendLine);
    line.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') sendLine();
    });
    clearBtn.addEventListener('click', () => { term.textContent = ''; });
    toggleAutoscrollBtn.addEventListener('click', () => {
      autoScrollEnabled = !autoScrollEnabled;
      refreshAutoscrollUi();
      if (autoScrollEnabled) term.scrollTop = term.scrollHeight;
    });
    refreshAutoscrollUi();

    function setUpgradeProgress(value) {
      const p = Math.max(0, Math.min(100, Number(value) || 0));
      upgradeProgressBar.style.width = p + '%';
    }

    function setUpgradeMessage(text) {
      upgradeStatusText.textContent = text;
    }

    function updateUpgradeView(data) {
      if (!data || data.ok !== true) return;
      const state = data.state || 'unknown';
      const target = data.target || '-';
      const progress = Number.isFinite(data.progress) ? data.progress : 0;
      const msg = data.msg || '';
      upStatusChip.textContent = state;
      let p = progress;
      if (state === 'done') p = 100;
      if (state === 'queued' && p <= 0) p = 2;
      setUpgradeProgress(p);
      setUpgradeMessage(state + ' | target=' + target + (msg ? ' | ' + msg : ''));
    }

    async function loadUpgradeConfig() {
      try {
        const res = await fetch('/api/fwupdate/config', { cache: 'no-store' });
        const data = await res.json();
        if (data && data.ok) {
          updateHost.value = data.update_host || '';
          flowPath.value = data.flowio_path || '';
          nextionPath.value = data.nextion_path || '';
        }
      } catch (err) {
        setUpgradeMessage('Config load failed: ' + err);
      }
    }

    async function saveUpgradeConfig() {
      const body = new URLSearchParams();
      body.set('update_host', updateHost.value.trim());
      body.set('flowio_path', flowPath.value.trim());
      body.set('nextion_path', nextionPath.value.trim());
      const res = await fetch('/api/fwupdate/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
        body: body.toString()
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.ok) throw new Error('save failed');
      setUpgradeMessage('Configuration saved.');
    }

    async function refreshUpgradeStatus() {
      try {
        const res = await fetch('/api/fwupdate/status', { cache: 'no-store' });
        const data = await res.json();
        if (data && data.ok) updateUpgradeView(data);
      } catch (err) {
        setUpgradeMessage('Status read failed: ' + err);
      }
    }

    async function startUpgrade(target) {
      try {
        await saveUpgradeConfig();
        const endpoint = target === 'flowio' ? '/fwupdate/flowio' : '/fwupdate/nextion';
        const res = await fetch(endpoint, { method: 'POST' });
        const data = await res.json().catch(() => ({}));
        if (!res.ok || !data.ok) throw new Error('start failed');
        setUpgradeProgress(1);
        setUpgradeMessage('Upgrade request accepted for ' + target + '.');
        await refreshUpgradeStatus();
      } catch (err) {
        setUpgradeMessage('Upgrade failed: ' + err);
      }
    }

    async function loadMqttConfig() {
      try {
        const res = await fetch('/api/mqtt/config', { cache: 'no-store' });
        const data = await res.json();
        if (data && data.ok) {
          mqttServer.value = data.server || '';
          mqttPort.value = Number.isFinite(data.port) ? String(data.port) : '1883';
          mqttUser.value = data.username || '';
          mqttPass.value = data.password || '';
          mqttConfigStatus.textContent = 'Configuration MQTT chargée.';
        }
      } catch (err) {
        mqttConfigStatus.textContent = 'Chargement MQTT échoué: ' + err;
      }
    }

    async function saveMqttConfig() {
      const body = new URLSearchParams();
      body.set('server', mqttServer.value.trim());
      body.set('port', (mqttPort.value || '1883').trim());
      body.set('username', mqttUser.value.trim());
      body.set('password', mqttPass.value);

      const res = await fetch('/api/mqtt/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
        body: body.toString()
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.ok) throw new Error('apply failed');
      mqttConfigStatus.textContent = 'Configuration MQTT appliquée.';
    }

    saveCfgBtn.addEventListener('click', async () => {
      try {
        await saveUpgradeConfig();
      } catch (err) {
        setUpgradeMessage('Save failed: ' + err);
      }
    });
    upFlowBtn.addEventListener('click', () => startUpgrade('flowio'));
    upNextionBtn.addEventListener('click', () => startUpgrade('nextion'));
    refreshStateBtn.addEventListener('click', refreshUpgradeStatus);
    applyMqttCfgBtn.addEventListener('click', async () => {
      try {
        await saveMqttConfig();
      } catch (err) {
        mqttConfigStatus.textContent = 'Application MQTT échouée: ' + err;
      }
    });

    loadUpgradeConfig();
    setUpgradeProgress(0);
    loadMqttConfig();
    refreshUpgradeStatus();
    setInterval(refreshUpgradeStatus, 2000);
  </script>
</body>
</html>
)HTML";

void WebInterfaceModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Mqtt;
    constexpr uint16_t kCfgBranchId = (uint16_t)ConfigBranchId::Mqtt;
    cfgStore_ = &cfg;
    cfg.registerVar(mqttHostVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(mqttPortVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(mqttUserVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(mqttPassVar_, kCfgModuleId, kCfgBranchId);

    services_ = &services;
    logHub_ = services.get<LogHubService>("loghub");
    wifiSvc_ = services.get<WifiService>("wifi");
    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    dataStore_ = dsSvc ? dsSvc->store : nullptr;
    auto* ebSvc = services.get<EventBusService>("eventbus");
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;
    fwUpdateSvc_ = services.get<FirmwareUpdateService>("fwupdate");
    if (eventBus_) {
        eventBus_->subscribe(EventId::DataChanged, &WebInterfaceModule::onEventStatic_, this);
    }

    static WebInterfaceService webInterfaceSvc{
        &WebInterfaceModule::svcSetPaused_,
        &WebInterfaceModule::svcIsPaused_,
        nullptr
    };
    webInterfaceSvc.ctx = this;
    services.add("webinterface", &webInterfaceSvc);

    uart_.setRxBufferSize(kUartRxBufferSize);
    uart_.begin(kUartBaud, SERIAL_8N1, kUartRxPin, kUartTxPin);
    netReady_ = dataStore_ ? wifiReady(*dataStore_) : false;
    LOGI("WebInterface init uart=Serial2 baud=%lu rx=%d tx=%d line_buf=%u rx_buf=%u (server deferred)",
         (unsigned long)kUartBaud,
         kUartRxPin,
         kUartTxPin,
         (unsigned)kLineBufferSize,
         (unsigned)kUartRxBufferSize);
}

void WebInterfaceModule::startServer_()
{
    if (started_) return;

    server_.on("/assets/flowio-icon.svg", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "image/svg+xml", kFlowIoIconSvg);
    });
    server_.on("/assets/flowio-logo.svg", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "image/svg+xml", kFlowIoLogoSvg);
    });

    server_.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/webinterface");
    });

    server_.on("/webinterface", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", kWebInterfacePage);
    });
    server_.on("/webserial", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/webinterface");
    });

    server_.on("/webinterface/health", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "ok");
    });
    server_.on("/webserial/health", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/webinterface/health");
    });

    auto fwStatusHandler = [this](AsyncWebServerRequest* request) {
        if (!fwUpdateSvc_ && services_) {
            fwUpdateSvc_ = services_->get<FirmwareUpdateService>("fwupdate");
        }
        if (!fwUpdateSvc_ || !fwUpdateSvc_->statusJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.status\"}}");
            return;
        }

        char out[320] = {0};
        if (!fwUpdateSvc_->statusJson(fwUpdateSvc_->ctx, out, sizeof(out))) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.status\"}}");
            return;
        }
        request->send(200, "application/json", out);
    };
    server_.on("/fwupdate/status", HTTP_GET, fwStatusHandler);
    server_.on("/api/fwupdate/status", HTTP_GET, fwStatusHandler);

    server_.on("/api/fwupdate/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!fwUpdateSvc_ && services_) {
            fwUpdateSvc_ = services_->get<FirmwareUpdateService>("fwupdate");
        }
        if (!fwUpdateSvc_ || !fwUpdateSvc_->configJson) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.config\"}}");
            return;
        }

        char out[320] = {0};
        if (!fwUpdateSvc_->configJson(fwUpdateSvc_->ctx, out, sizeof(out))) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.config\"}}");
            return;
        }
        request->send(200, "application/json", out);
    });

    server_.on("/api/fwupdate/config", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!fwUpdateSvc_ && services_) {
            fwUpdateSvc_ = services_->get<FirmwareUpdateService>("fwupdate");
        }
        if (!fwUpdateSvc_ || !fwUpdateSvc_->setConfig) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.set_config\"}}");
            return;
        }

        String hostStr;
        String flowStr;
        String nxStr;
        if (request->hasParam("update_host", true)) {
            hostStr = request->getParam("update_host", true)->value();
        }
        if (request->hasParam("flowio_path", true)) {
            flowStr = request->getParam("flowio_path", true)->value();
        }
        if (request->hasParam("nextion_path", true)) {
            nxStr = request->getParam("nextion_path", true)->value();
        }

        char err[96] = {0};
        if (!fwUpdateSvc_->setConfig(fwUpdateSvc_->ctx,
                                     hostStr.c_str(),
                                     flowStr.c_str(),
                                     nxStr.c_str(),
                                     err,
                                     sizeof(err))) {
            request->send(409,
                          "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.set_config\"}}");
            return;
        }

        request->send(200, "application/json", "{\"ok\":true}");
    });

    server_.on("/api/mqtt/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        char host[sizeof(mqttCfg_.host)] = {0};
        char user[sizeof(mqttCfg_.user)] = {0};
        char pass[sizeof(mqttCfg_.pass)] = {0};
        snprintf(host, sizeof(host), "%s", mqttCfg_.host);
        snprintf(user, sizeof(user), "%s", mqttCfg_.user);
        snprintf(pass, sizeof(pass), "%s", mqttCfg_.pass);
        sanitizeJsonString_(host);
        sanitizeJsonString_(user);
        sanitizeJsonString_(pass);

        char out[512] = {0};
        const int n = snprintf(out,
                               sizeof(out),
                               "{\"ok\":true,\"server\":\"%s\",\"port\":%ld,\"username\":\"%s\",\"password\":\"%s\"}",
                               host,
                               (long)mqttCfg_.port,
                               user,
                               pass);
        if (n <= 0 || (size_t)n >= sizeof(out)) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"mqtt.config.get\"}}");
            return;
        }
        request->send(200, "application/json", out);
    });

    server_.on("/api/mqtt/config", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!cfgStore_) {
            request->send(503, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"mqtt.config.set\"}}");
            return;
        }

        String serverStr = request->hasParam("server", true)
                               ? request->getParam("server", true)->value()
                               : String(mqttCfg_.host);
        String userStr = request->hasParam("username", true)
                             ? request->getParam("username", true)->value()
                             : String(mqttCfg_.user);
        String passStr = request->hasParam("password", true)
                             ? request->getParam("password", true)->value()
                             : String(mqttCfg_.pass);

        int32_t portVal = mqttCfg_.port;
        if (request->hasParam("port", true)) {
            String portStr = request->getParam("port", true)->value();
            if (portStr.length() == 0) {
                portVal = Limits::Mqtt::Defaults::Port;
            } else {
                char* end = nullptr;
                const long parsed = strtol(portStr.c_str(), &end, 10);
                if (!end || *end != '\0' || parsed < 1 || parsed > 65535) {
                    request->send(400, "application/json",
                                  "{\"ok\":false,\"err\":{\"code\":\"InvalidArg\",\"where\":\"mqtt.port\"}}");
                    return;
                }
                portVal = (int32_t)parsed;
            }
        }

        bool ok = true;
        ok = ok && cfgStore_->set(mqttHostVar_, serverStr.c_str());
        ok = ok && cfgStore_->set(mqttPortVar_, portVal);
        ok = ok && cfgStore_->set(mqttUserVar_, userStr.c_str());
        ok = ok && cfgStore_->set(mqttPassVar_, passStr.c_str());
        if (!ok) {
            request->send(500, "application/json",
                          "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"mqtt.config.set\"}}");
            return;
        }

        request->send(200, "application/json", "{\"ok\":true}");
    });

    server_.on("/fwupdate/flowio", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleUpdateRequest_(request, FirmwareUpdateTarget::FlowIO);
    });

    server_.on("/fwupdate/nextion", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleUpdateRequest_(request, FirmwareUpdateTarget::Nextion);
    });

    ws_.onEvent([this](AsyncWebSocket* server,
                       AsyncWebSocketClient* client,
                       AwsEventType type,
                       void* arg,
                       uint8_t* data,
                       size_t len) {
        this->onWsEvent_(server, client, type, arg, data, len);
    });

    server_.addHandler(&ws_);
    server_.begin();
    started_ = true;
    LOGI("WebInterface server started, listening on 0.0.0.0:%d", kServerPort);

    if (wifiSvc_ && wifiSvc_->isConnected && wifiSvc_->isConnected(wifiSvc_->ctx)) {
        char ip[16] = {0};
        if (wifiSvc_->getIP && wifiSvc_->getIP(wifiSvc_->ctx, ip, sizeof(ip)) && ip[0] != '\0') {
            LOGI("WebInterface URL: http://%s/webinterface", ip);
        } else {
            LOGI("WebInterface URL: WiFi connected (IP unavailable)");
        }
    } else {
        LOGI("WebInterface URL: waiting for WiFi IP");
    }
}

void WebInterfaceModule::onWsEvent_(AsyncWebSocket*,
                                 AsyncWebSocketClient* client,
                                 AwsEventType type,
                                 void* arg,
                                 uint8_t* data,
                                 size_t len)
{
    if (type == WS_EVT_CONNECT) {
        if (client) client->text("[webinterface] connected");
        return;
    }

    if (type != WS_EVT_DATA || !arg || !data || len == 0) return;

    AwsFrameInfo* info = reinterpret_cast<AwsFrameInfo*>(arg);
    if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) return;

    constexpr size_t kMaxIncoming = 192;
    char msg[kMaxIncoming] = {0};
    size_t n = (len < (kMaxIncoming - 1)) ? len : (kMaxIncoming - 1);
    memcpy(msg, data, n);
    msg[n] = '\0';

    if (uartPaused_) {
        if (client) client->text("[webinterface] uart busy (firmware update in progress)");
        return;
    }

    uart_.write(reinterpret_cast<const uint8_t*>(msg), n);
    uart_.write('\n');
}

void WebInterfaceModule::handleUpdateRequest_(AsyncWebServerRequest* request, FirmwareUpdateTarget target)
{
    if (!request) return;
    if (!fwUpdateSvc_ && services_) {
        fwUpdateSvc_ = services_->get<FirmwareUpdateService>("fwupdate");
    }
    if (!fwUpdateSvc_ || !fwUpdateSvc_->start) {
        request->send(503, "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"NotReady\",\"where\":\"fwupdate.start\"}}");
        return;
    }

    const AsyncWebParameter* pUrl = request->hasParam("url", true) ? request->getParam("url", true) : nullptr;
    String urlStr;
    if (pUrl) {
        urlStr = pUrl->value();
    }
    const char* url = (urlStr.length() > 0) ? urlStr.c_str() : nullptr;

    char err[144] = {0};
    if (!fwUpdateSvc_->start(fwUpdateSvc_->ctx, target, url, err, sizeof(err))) {
        request->send(409,
                      "application/json",
                      "{\"ok\":false,\"err\":{\"code\":\"Failed\",\"where\":\"fwupdate.start\"}}");
        return;
    }

    request->send(202, "application/json", "{\"ok\":true,\"accepted\":true}");
}

bool WebInterfaceModule::isLogByte_(uint8_t c)
{
    return c == '\t' || c == 0x1B || c >= 32;
}

bool WebInterfaceModule::svcSetPaused_(void* ctx, bool paused)
{
    WebInterfaceModule* self = static_cast<WebInterfaceModule*>(ctx);
    if (!self) return false;
    self->uartPaused_ = paused;
    if (paused) {
        self->lineLen_ = 0;
    }
    return true;
}

bool WebInterfaceModule::svcIsPaused_(void* ctx)
{
    WebInterfaceModule* self = static_cast<WebInterfaceModule*>(ctx);
    if (!self) return false;
    return self->uartPaused_;
}

void WebInterfaceModule::onEventStatic_(const Event& e, void* user)
{
    WebInterfaceModule* self = static_cast<WebInterfaceModule*>(user);
    if (!self) return;
    self->onEvent_(e);
}

void WebInterfaceModule::onEvent_(const Event& e)
{
    if (e.id != EventId::DataChanged) return;
    if (!e.payload || e.len < sizeof(DataChangedPayload)) return;
    const DataChangedPayload* p = static_cast<const DataChangedPayload*>(e.payload);
    if (p->id != DataKeys::WifiReady) return;

    netReady_ = dataStore_ ? wifiReady(*dataStore_) : false;
}

void WebInterfaceModule::flushLine_(bool force)
{
    if (lineLen_ == 0) return;
    if (!force) return;

    lineBuf_[lineLen_] = '\0';
    ws_.textAll(lineBuf_);
    lineLen_ = 0;
}

void WebInterfaceModule::loop()
{
    if (!started_) {
        if (!netReady_) {
            vTaskDelay(pdMS_TO_TICKS(100));
            return;
        }
        startServer_();
    }

    if (uartPaused_) {
        if (started_) ws_.cleanupClients();
        vTaskDelay(pdMS_TO_TICKS(40));
        return;
    }

    while (uart_.available() > 0) {
        int raw = uart_.read();
        if (raw < 0) break;

        const uint8_t c = static_cast<uint8_t>(raw);

        if (c == '\r') continue;
        if (c == '\n') {
            flushLine_(true);
            continue;
        }

        if (lineLen_ >= (kLineBufferSize - 1)) {
            flushLine_(true);
        }

        if (lineLen_ < (kLineBufferSize - 1)) {
            lineBuf_[lineLen_++] = isLogByte_(c) ? static_cast<char>(c) : '.';
        }
    }

    if (started_) ws_.cleanupClients();

    vTaskDelay(pdMS_TO_TICKS(10));
}
