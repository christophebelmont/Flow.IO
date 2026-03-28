    const drawer = document.getElementById('drawer');
    const overlay = document.getElementById('overlay');
    const menuToggles = Array.from(document.querySelectorAll('[data-menu-toggle]'));
    const menuItems = Array.from(document.querySelectorAll('.menu-item'));
    const pages = Array.from(document.querySelectorAll('.page'));
    const appMeta = document.querySelector('.app-meta');
    const appRuntimeMeta = document.getElementById('appRuntimeMeta');
    const appHeapSummary = document.getElementById('appHeapSummary');
    const flowWebAssetVersionStorageKey = 'flow_web_asset_version';
    const deferredVisualAssetsStateKey = 'flow_web_deferred_visual_assets';
    const deferredBrandAssetDelayMs = 320;
    const deferredMenuAssetStartDelayMs = 520;
    const deferredMenuAssetStepMs = 140;
    const deferredBrandAssetReloadDelayMs = 2400;
    const deferredMenuAssetReloadDelayMs = 1400;
    const deferredMenuAssetReloadStepMs = 850;
    const deferredMenuAssetReloadFallbackDelayMs = 6500;
    let webAssetVersion = '';
    let loadedWebAssetVersion = '';
    let supervisorFirmwareVersion = '-';
    let supervisorUptimeMs = 0;
    let supervisorHeap = {};
    let hideMenuSvg = false;
    let unifyStatusCardIcons = false;
    let flowStatusLiveTimer = null;
    let pageLoadToken = 0;
    let deferredVisualAssetsScheduled = false;
    let brandAssetsActivated = false;
    let menuAssetsActivated = false;
    let deferredMenuAssetsArmed = false;

    function applyMenuIconPreference(hidden) {
      hideMenuSvg = !!hidden;
      document.body.classList.toggle('menu-icons-disabled', hideMenuSvg);
    }

    function applyStatusIconPreference(unified) {
      unifyStatusCardIcons = !!unified;
    }

    function resolveSupervisorFirmwareVersion() {
      if (appMeta) {
        const raw = (appMeta.textContent || '').trim();
        const match = raw.match(/^Supervisor\s+(.+)$/i);
        if (match && match[1]) {
          const version = match[1].trim();
          if (version && version !== '-') {
            return version;
          }
        }
        if (raw && raw !== '-') {
          return raw;
        }
      }
      return '-';
    }

    try {
      loadedWebAssetVersion = String(window.__FLOW_WEB_ASSET_VERSION__ || '').trim();
    } catch (err) {
      loadedWebAssetVersion = '';
    }
    webAssetVersion = loadedWebAssetVersion;
    if (!webAssetVersion) {
      webAssetVersion = getStorageValue(localStorage, flowWebAssetVersionStorageKey);
    }

    supervisorFirmwareVersion = resolveSupervisorFirmwareVersion();

    function versionedWebAssetUrl(path) {
      if (!webAssetVersion) return path;
      return path + '?v=' + encodeURIComponent(webAssetVersion);
    }

    function getStorageValue(storage, key) {
      try {
        return String(storage.getItem(key) || '').trim();
      } catch (err) {
        return '';
      }
    }

    function setStorageValue(storage, key, value) {
      try {
        storage.setItem(key, value);
      } catch (err) {
      }
    }

    async function fetchJsonResponse(url, options, fetchImpl) {
      const resolvedFetch = typeof fetchImpl === 'function' ? fetchImpl : fetch;
      const res = await resolvedFetch(url, options);
      const data = await res.json().catch(() => null);
      return { res, data };
    }

    function ensureOkJsonResponse(response, message) {
      if (!response.res.ok || !response.data || response.data.ok !== true) {
        throw new Error(message);
      }
      return response.data;
    }

    async function fetchOkJson(url, options, message, fetchImpl) {
      return ensureOkJsonResponse(await fetchJsonResponse(url, options, fetchImpl), message);
    }

    function createUrlEncodedBody(values) {
      const body = new URLSearchParams();
      Object.keys(values || {}).forEach((key) => {
        const value = values[key];
        body.set(key, value === null || typeof value === 'undefined' ? '' : String(value));
      });
      return body.toString();
    }

    function createFormPostOptions(values) {
      return {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
        body: createUrlEncodedBody(values)
      };
    }

    function runAsyncTaskSafely(task) {
      return Promise.resolve().then(task).catch(() => {});
    }

    function createIntervalRunner(task, delayMs) {
      let timer = null;
      return {
        start() {
          if (timer) return;
          timer = setInterval(() => {
            runAsyncTaskSafely(task);
          }, delayMs);
        },
        stop() {
          if (!timer) return;
          clearInterval(timer);
          timer = null;
        }
      };
    }

    function createTimeoutRunner(task) {
      let timer = null;
      return {
        schedule(delayMs) {
          this.stop();
          timer = setTimeout(() => {
            timer = null;
            runAsyncTaskSafely(task);
          }, delayMs);
        },
        stop() {
          if (!timer) return;
          clearTimeout(timer);
          timer = null;
        }
      };
    }

    function bindClickAction(el, handler) {
      if (!el) return;
      el.addEventListener('click', () => {
        runAsyncTaskSafely(handler);
      });
    }

    function getActivePageId() {
      const active = document.querySelector('.page.active');
      return active ? active.id : '';
    }

    function buildNodeGrid(className, items) {
      const nodes = (items || []).filter((item) => item && typeof item.nodeType === 'number');
      if (nodes.length === 0) return null;
      const wrapper = document.createElement('div');
      wrapper.className = className;
      nodes.forEach((node) => wrapper.appendChild(node));
      return wrapper;
    }

    function currentDeferredVisualAssetsVersion() {
      return (webAssetVersion || loadedWebAssetVersion || 'noversion').trim() || 'noversion';
    }

    function getDeferredVisualAssetsWarmState() {
      return getStorageValue(localStorage, deferredVisualAssetsStateKey);
    }

    function hasWarmDeferredVisualAssets() {
      return getDeferredVisualAssetsWarmState() === currentDeferredVisualAssetsVersion();
    }

    function markDeferredVisualAssetsWarm() {
      setStorageValue(localStorage, deferredVisualAssetsStateKey, currentDeferredVisualAssetsVersion());
    }

    function navigationType() {
      try {
        const navEntries = performance.getEntriesByType('navigation');
        if (navEntries && navEntries[0] && typeof navEntries[0].type === 'string') {
          return navEntries[0].type;
        }
      } catch (err) {
      }
      try {
        if (performance && performance.navigation) {
          if (performance.navigation.type === 1) return 'reload';
          if (performance.navigation.type === 0) return 'navigate';
        }
      } catch (err) {
      }
      return '';
    }

    function isReloadNavigation() {
      return navigationType() === 'reload';
    }

    function setDeferredVisualAssetUrl(varName, path) {
      document.documentElement.style.setProperty(varName, "url('" + versionedWebAssetUrl(path) + "')");
    }

    function activateBrandAssets() {
      if (brandAssetsActivated) return;
      brandAssetsActivated = true;
      setDeferredVisualAssetUrl('--flowio-brand-url', '/webinterface/i/f.svg');
    }

    function activateMenuAssets(deferred, stepDelayMs) {
      if (menuAssetsActivated || hideMenuSvg) return;
      menuAssetsActivated = true;
      const stepMs = Math.max(0, Number(stepDelayMs) || deferredMenuAssetStepMs);
      const steps = [
        ['--menu-icon-measures-url', '/webinterface/i/m.svg'],
        ['--menu-icon-terminal-url', '/webinterface/i/t.svg'],
        ['--menu-icon-system-url', '/webinterface/i/s.svg'],
        ['--menu-icon-flowcfg-url', '/webinterface/i/d.svg'],
        ['--menu-icon-supervisorcfg-url', '/webinterface/i/e.svg']
      ];
      if (!deferred) {
        steps.forEach((entry) => {
          setDeferredVisualAssetUrl(entry[0], entry[1]);
        });
        markDeferredVisualAssetsWarm();
        return;
      }
      steps.forEach((entry, index) => {
        setTimeout(() => {
          setDeferredVisualAssetUrl(entry[0], entry[1]);
          if (index === steps.length - 1) {
            markDeferredVisualAssetsWarm();
          }
        }, deferred ? (index * stepMs) : 0);
      });
    }

    function armDeferredMenuAssets(startDelayMs, stepDelayMs, fallbackDelayMs) {
      if (deferredMenuAssetsArmed || menuAssetsActivated || hideMenuSvg || !drawer) return;
      deferredMenuAssetsArmed = true;
      let triggered = false;
      const trigger = () => {
        if (triggered) return;
        triggered = true;
        setTimeout(() => {
          activateMenuAssets(true, stepDelayMs);
        }, Math.max(0, Number(startDelayMs) || 0));
      };
      drawer.addEventListener('pointerenter', trigger, { once: true });
      drawer.addEventListener('touchstart', trigger, { once: true, passive: true });
      drawer.addEventListener('click', trigger, { once: true });
      setTimeout(trigger, Math.max(0, Number(fallbackDelayMs) || 0));
    }

    function scheduleDeferredVisualAssets() {
      if (deferredVisualAssetsScheduled) return;
      deferredVisualAssetsScheduled = true;
      if (hasWarmDeferredVisualAssets() && !isReloadNavigation()) {
        activateBrandAssets();
        activateMenuAssets(false);
        return;
      }

      const isReload = isReloadNavigation();
      const brandDelayMs = isReload ? deferredBrandAssetReloadDelayMs : deferredBrandAssetDelayMs;
      setTimeout(() => {
        activateBrandAssets();
        if (hideMenuSvg) {
          markDeferredVisualAssetsWarm();
        }
      }, brandDelayMs);

      if (isReload) {
        armDeferredMenuAssets(
          deferredMenuAssetReloadDelayMs,
          deferredMenuAssetReloadStepMs,
          deferredMenuAssetReloadFallbackDelayMs
        );
        return;
      }

      setTimeout(() => {
        activateMenuAssets(true, deferredMenuAssetStepMs);
      }, deferredMenuAssetStartDelayMs);
    }

    async function loadWebMeta() {
      try {
        const data = await fetchOkJson('/api/web/meta', { cache: 'no-store' }, 'meta web indisponible');

        if (typeof data.web_asset_version === 'string') {
          const announcedVersion = data.web_asset_version.trim();
          if (announcedVersion) {
            webAssetVersion = announcedVersion;
            setStorageValue(localStorage, flowWebAssetVersionStorageKey, announcedVersion);
            if (loadedWebAssetVersion && loadedWebAssetVersion !== announcedVersion) {
              const reloadKey = 'flow_web_asset_reload_once';
              try {
                if (sessionStorage.getItem(reloadKey) !== announcedVersion) {
                  sessionStorage.setItem(reloadKey, announcedVersion);
                  window.location.reload();
                  return;
                }
              } catch (err) {
                window.location.reload();
                return;
              }
            }
            try {
              if (sessionStorage.getItem('flow_web_asset_reload_once') === announcedVersion) {
                sessionStorage.removeItem('flow_web_asset_reload_once');
              }
            } catch (err) {
            }
          }
        }
        applyMenuIconPreference(!!data.hide_menu_svg);
        applyStatusIconPreference(!!data.unify_status_card_icons);
        if (hasWarmDeferredVisualAssets()) {
          activateBrandAssets();
          activateMenuAssets(false);
        }
        if (typeof data.firmware_version === 'string') {
          const trimmed = data.firmware_version.trim();
          if (trimmed) {
            supervisorFirmwareVersion = trimmed;
            if (appMeta) {
              appMeta.textContent = trimmed;
            }
          }
        }
        supervisorUptimeMs = Number(data.upms) || 0;
        supervisorHeap = (data.heap && typeof data.heap === 'object') ? data.heap : {};
        refreshDrawerRuntimeMeta(false).catch(() => {});
        if (isPageActive('page-status')) {
          refreshFlowStatus(false).catch(() => {});
        }
      } catch (err) {
      }
    }

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

    function startUpgradeStatusPolling() {
      upgradeStatusPoller.start();
    }

    function stopUpgradeStatusPolling() {
      upgradeStatusPoller.stop();
    }

    let flowRemoteFetchQueue = Promise.resolve();

    function fetchFlowRemoteQueued(url, options) {
      const queued = flowRemoteFetchQueue
        .catch(() => {})
        .then(() => fetch(url, options));
      flowRemoteFetchQueue = queued.catch(() => {});
      return queued;
    }

    function waitMs(ms) {
      return new Promise((resolve) => setTimeout(resolve, ms));
    }

    function isPageActive(pageId) {
      const el = document.getElementById(pageId);
      return !!(el && el.classList.contains('active'));
    }

    function stopFlowStatusLiveTimer() {
      if (!flowStatusLiveTimer) return;
      clearInterval(flowStatusLiveTimer);
      flowStatusLiveTimer = null;
    }

    function schedulePageTask(pageId, token, delayMs, task) {
      const run = () => {
        if (token !== pageLoadToken || !isPageActive(pageId)) return;
        Promise.resolve().then(task).catch(() => {});
      };
      if (delayMs > 0) {
        setTimeout(run, delayMs);
      } else {
        run();
      }
    }

    function showPage(pageId, options) {
      const opts = options || {};
      const deferredHeavyMs = Math.max(0, Number(opts.deferHeavyMs) || 0);
      const pageToken = ++pageLoadToken;
      pages.forEach((el) => el.classList.toggle('active', el.id === pageId));
      menuItems.forEach((el) => el.classList.toggle('active', el.dataset.page === pageId));
      terminalActive = pageId === 'page-terminal';
      if (terminalActive) {
        connectLogSocket();
      } else {
        closeLogSocket();
        setWsStatusText('inactif');
      }
      if (pageId === 'page-pool-measures') {
        schedulePageTask(pageId, pageToken, deferredHeavyMs, () => onPoolMeasuresPageShown());
      } else {
        stopPoolMeasuresTimer();
      }
      if (pageId === 'page-status') {
        schedulePageTask(pageId, pageToken, deferredHeavyMs, () => refreshFlowStatus(false));
      } else {
        stopFlowStatusLiveTimer();
      }
      if (pageId === 'page-system') {
        schedulePageTask(pageId, pageToken, deferredHeavyMs, () => onConfigPageShown());
        schedulePageTask(pageId,
                         pageToken,
                         deferredHeavyMs > 0 ? (deferredHeavyMs + 180) : 0,
                         () => onUpgradePageShown());
      }
      if (pageId === 'page-control') {
        schedulePageTask(pageId,
                         pageToken,
                         deferredHeavyMs > 0 ? (deferredHeavyMs + 220) : 0,
                         () => onControlPageShown());
      } else {
        stopFlowCfgRetry();
      }
      if (pageId === 'page-local-config') {
        schedulePageTask(pageId,
                         pageToken,
                         deferredHeavyMs > 0 ? (deferredHeavyMs + 220) : 0,
                         () => onLocalConfigPageShown());
      }
      if (pageId !== 'page-system') {
        stopUpgradeStatusPolling();
      }
      closeMobileDrawer();
    }

    function resolveInitialPageId() {
      try {
        const params = new URLSearchParams(window.location.search || '');
        let requestedPage = String(params.get('page') || '').trim();
        if (requestedPage === 'page-status') {
          requestedPage = 'page-pool-measures';
        }
        if (requestedPage && pages.some((el) => el.id === requestedPage)) {
          return requestedPage;
        }
      } catch (err) {
      }
      const activePage = document.querySelector('.page.active');
      if (activePage && activePage.id) {
        return activePage.id;
      }
      return 'page-pool-measures';
    }

    menuItems.forEach((item) => item.addEventListener('click', () => showPage(item.dataset.page)));

    menuToggles.forEach((btn) => btn.addEventListener('click', () => {
      if (isMobileLayout()) {
        setMobileDrawerOpen(!drawer.classList.contains('mobile-open'));
      } else {
        if (hideMenuSvg) return;
        drawer.classList.toggle('collapsed');
        refreshDrawerRuntimeMeta(false).catch(() => {});
      }
    }));

    overlay.addEventListener('click', closeMobileDrawer);
    window.addEventListener('resize', () => {
      if (!isMobileLayout()) {
        setMobileDrawerOpen(false);
      }
      refreshDrawerRuntimeMeta(false).catch(() => {});
    });

    const term = document.getElementById('term');
    const wsStatus = document.getElementById('wsStatus');
    const logSourceSelect = document.getElementById('logSourceSelect');
    const line = document.getElementById('line');
    const sendBtn = document.getElementById('send');
    const clearBtn = document.getElementById('clear');
    const toggleAutoscrollInput = document.getElementById('toggleAutoscroll');
    let autoScrollEnabled = true;
    let terminalActive = false;
    const lineDefaultPlaceholder = line ? line.placeholder : '';

    const updateHost = document.getElementById('updateHost');
    const flowPath = document.getElementById('flowPath');
    const nextionPath = document.getElementById('nextionPath');
    const supervisorPath = document.getElementById('supervisorPath');
    const spiffsPath = document.getElementById('spiffsPath');
    const saveCfgBtn = document.getElementById('saveCfg');
    const upSupervisorBtn = document.getElementById('upSupervisor');
    const upFlowBtn = document.getElementById('upFlow');
    const upNextionBtn = document.getElementById('upNextion');
    const upSpiffsBtn = document.getElementById('upSpiffs');
    const refreshStateBtn = document.getElementById('refreshState');
    const upgradeStatusText = document.getElementById('upgradeStatusText');
    const upgradeProgressBar = document.getElementById('upgradeProgressBar');
    const upStatusChip = document.getElementById('upStatusChip');

    const wifiEnabled = document.getElementById('wifiEnabled');
    const wifiSsid = document.getElementById('wifiSsid');
    const wifiSsidList = document.getElementById('wifiSsidList');
    const wifiPass = document.getElementById('wifiPass');
    const toggleWifiPassBtn = document.getElementById('toggleWifiPass');
    const scanWifiBtn = document.getElementById('scanWifi');
    const applyWifiCfgBtn = document.getElementById('applyWifiCfg');
    const wifiConfigStatus = document.getElementById('wifiConfigStatus');
    const rebootSupervisorBtn = document.getElementById('rebootSupervisor');
    const rebootFlowBtn = document.getElementById('rebootFlow');
    const flowFactoryResetBtn = document.getElementById('flowFactoryReset');
    const systemStatusText = document.getElementById('systemStatusText');
    const flowStatusRefreshBtn = document.getElementById('flowStatusRefresh');
    const flowStatusChip = document.getElementById('flowStatusChip');
    const flowStatusGrid = document.getElementById('flowStatusGrid');
    const flowStatusRaw = document.getElementById('flowStatusRaw');
    const poolMeasuresRefreshBtn = document.getElementById('poolMeasuresRefresh');
    const poolMeasuresStatus = document.getElementById('poolMeasuresStatus');
    const poolMeasuresGrid = document.getElementById('poolMeasuresGrid');
    const flowCfgTitle = document.getElementById('flowCfgTitle');
    const flowCfgRefreshBtn = document.getElementById('flowCfgRefresh');
    const flowCfgApplyBtn = document.getElementById('flowCfgApply');
    const flowCfgSections = document.getElementById('flowCfgSections');
    const flowCfgFields = document.getElementById('flowCfgFields');
    const flowCfgStatus = document.getElementById('flowCfgStatus');
    const supCfgModuleSelect = document.getElementById('supCfgModuleSelect');
    const supCfgRefreshBtn = document.getElementById('supCfgRefresh');
    const supCfgApplyBtn = document.getElementById('supCfgApply');
    const supCfgFields = document.getElementById('supCfgFields');
    const supCfgStatus = document.getElementById('supCfgStatus');
    let flowCfgCurrentModule = '';
    let flowCfgCurrentData = {};
    let flowCfgChildrenCache = {};
    let flowCfgPath = [];
    let cfgDocSources = [];
    let flowCfgDocsLoaded = false;
    let flowCfgLoadingDepth = 0;
    let flowCfgLoadPromise = null;
    let flowCfgRetryTimer = null;
    let upgradeCfgLoadedOnce = false;
    let wifiConfigLoadedOnce = false;
    let flowCfgLoadedOnce = false;
    let supCfgLoadedOnce = false;
    let supCfgCurrentModule = '';
    let supCfgCurrentData = {};
    let wifiScanAutoRequested = false;
    let flowStatusReqSeq = 0;
    const fieldApplyCheckIcon = 'OK';
    const flowStatusDomainTtlMs = 20000;
    const flowStatusDomainKeys = ['system', 'wifi', 'mqtt', 'pool', 'i2c'];
    const flowStatusDomainCache = {
      system: { data: null, fetchedAt: 0 },
      wifi: { data: null, fetchedAt: 0 },
      mqtt: { data: null, fetchedAt: 0 },
      pool: { data: null, fetchedAt: 0 },
      i2c: { data: null, fetchedAt: 0 }
    };
    let runtimeManifestCache = null;
    let poolMeasureEntries = [];

    const wsProto = location.protocol === 'https:' ? 'wss' : 'ws';
    let logSource = 'flow';
    let logSocket = null;
    const upgradeStatusPoller = createIntervalRunner(() => refreshUpgradeStatus(), 4000);
    const poolMeasuresPoller = createIntervalRunner(() => {
      if (getActivePageId() !== 'page-pool-measures' || document.hidden) return;
      return refreshPoolMeasures(false);
    }, 10000);
    const drawerRuntimePoller = createIntervalRunner(() => {
      if (document.hidden || !isDrawerRuntimeMetaVisible()) return;
      return loadWebMeta();
    }, 15000);
    const wifiScanPoller = createTimeoutRunner(() => refreshWifiScanStatus(false));

    function setWsStatusText(status) {
      const sourceLabel = logSource === 'supervisor' ? 'Supervisor' : 'Flow.IO';
      wsStatus.textContent = sourceLabel + ' : ' + status;
    }

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

    function updateTerminalInputState() {
      const canSend = logSource === 'flow';
      line.disabled = !canSend;
      sendBtn.disabled = !canSend;
      line.placeholder = canSend ? lineDefaultPlaceholder : 'Envoi désactivé pour les journaux Supervisor';
    }

    function closeLogSocket() {
      if (!logSocket) return;
      logSocket.onopen = null;
      logSocket.onclose = null;
      logSocket.onerror = null;
      logSocket.onmessage = null;
      try {
        logSocket.close();
      } catch (err) {
        // Ignore close errors on stale sockets.
      }
      logSocket = null;
    }

    function connectLogSocket() {
      closeLogSocket();
      setWsStatusText('connexion...');
      const path = logSource === 'supervisor' ? '/wslog' : '/wsserial';
      const socket = new WebSocket(wsProto + '://' + location.host + path);
      logSocket = socket;
      socket.onopen = () => {
        if (socket !== logSocket) return;
        setWsStatusText('connecté');
      };
      socket.onclose = (ev) => {
        if (socket !== logSocket) return;
        const code = ev && Number.isFinite(ev.code) ? ev.code : 0;
        setWsStatusText(code ? ('déconnecté (' + code + ')') : 'déconnecté');
      };
      socket.onerror = () => {
        if (socket !== logSocket) return;
        setWsStatusText('erreur');
      };
      socket.onmessage = (ev) => {
        if (socket !== logSocket) return;
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
    }

    function setLogSource(source) {
      logSource = source === 'supervisor' ? 'supervisor' : 'flow';
      if (logSourceSelect && logSourceSelect.value !== logSource) {
        logSourceSelect.value = logSource;
      }
      updateTerminalInputState();
      if (terminalActive) {
        connectLogSocket();
      }
    }

    function refreshAutoscrollUi() {
      toggleAutoscrollInput.checked = autoScrollEnabled;
      toggleAutoscrollInput.setAttribute('aria-checked', autoScrollEnabled ? 'true' : 'false');
      toggleAutoscrollInput.setAttribute(
        'title',
        autoScrollEnabled ? 'Défilement auto activé' : 'Défilement auto désactivé'
      );
    }

    function sendLine() {
      const txt = line.value;
      if (!txt) return;
      if (logSource !== 'flow') return;
      if (logSocket && logSocket.readyState === WebSocket.OPEN) {
        logSocket.send(txt);
      }
      line.value = '';
      line.focus();
    }

    const iconeOeilOuvert = 'VOIR';
    const iconeOeilBarre = 'MASQ';

    function mettreAJourEtatVisibiliteMotDePasse(inputEl, toggleBtn, labelAfficher, labelMasquer) {
      if (!inputEl || !toggleBtn) return;
      const isVisible = inputEl.type === 'text';
      toggleBtn.innerHTML = isVisible ? iconeOeilOuvert : iconeOeilBarre;
      toggleBtn.setAttribute('aria-pressed', isVisible ? 'true' : 'false');
      toggleBtn.setAttribute('aria-label', isVisible ? labelMasquer : labelAfficher);
      toggleBtn.setAttribute('title', isVisible ? 'Mot de passe en clair' : 'Mot de passe masqué');
    }

    function basculerVisibiliteMotDePasse(inputEl, toggleBtn, labelAfficher, labelMasquer) {
      if (!inputEl || !toggleBtn) return;
      const isMasked = inputEl.type === 'password';
      inputEl.type = isMasked ? 'text' : 'password';
      mettreAJourEtatVisibiliteMotDePasse(inputEl, toggleBtn, labelAfficher, labelMasquer);
    }
    sendBtn.addEventListener('click', sendLine);
    line.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') sendLine();
    });
    clearBtn.addEventListener('click', () => { term.textContent = ''; });
    toggleAutoscrollInput.addEventListener('change', () => {
      autoScrollEnabled = !!toggleAutoscrollInput.checked;
      refreshAutoscrollUi();
      if (autoScrollEnabled) term.scrollTop = term.scrollHeight;
    });
    if (logSourceSelect) {
      logSourceSelect.addEventListener('change', () => {
        setLogSource(logSourceSelect.value);
      });
    }
    refreshAutoscrollUi();
    logSource = logSourceSelect ? logSourceSelect.value : 'flow';
    updateTerminalInputState();
    setWsStatusText('inactif');
    flowCfgApplyBtn.disabled = true;
    supCfgApplyBtn.disabled = true;

    function setUpgradeProgress(value) {
      const p = Math.max(0, Math.min(100, Number(value) || 0));
      upgradeProgressBar.style.width = p + '%';
    }

    function setUpgradeMessage(text) {
      upgradeStatusText.textContent = text;
    }

    function updateUpgradeView(data) {
      if (!data || data.ok !== true) return;
      const state = data.state || 'inconnu';
      const target = data.target || '-';
      const progress = Number.isFinite(data.progress) ? data.progress : 0;
      const msg = data.msg || '';
      let stateLabel = state;
      if (state === 'idle') stateLabel = 'inactif';
      else if (state === 'queued') stateLabel = 'en attente';
      else if (state === 'running') stateLabel = 'en cours';
      else if (state === 'done') stateLabel = 'terminé';
      else if (state === 'error') stateLabel = 'erreur';
      if (upStatusChip) upStatusChip.textContent = stateLabel;
      let p = progress;
      if (state === 'done') p = 100;
      if (state === 'queued' && p <= 0) p = 2;
      setUpgradeProgress(p);
      setUpgradeMessage(stateLabel + ' | cible=' + target + (msg ? ' | ' + msg : ''));
    }

    async function loadUpgradeConfig() {
      try {
        const data = await fetchOkJson('/api/fwupdate/config', { cache: 'no-store' }, 'configuration firmware indisponible');
        updateHost.value = data.update_host || '';
        flowPath.value = data.flowio_path || '';
        supervisorPath.value = data.supervisor_path || '';
        nextionPath.value = data.nextion_path || '';
        spiffsPath.value = data.spiffs_path || data.cfgdocs_path || '';
      } catch (err) {
        setUpgradeMessage('Échec du chargement de la configuration : ' + err);
      }
    }

    async function saveUpgradeConfig() {
      await fetchOkJson('/api/fwupdate/config', createFormPostOptions({
        update_host: updateHost.value.trim(),
        flowio_path: flowPath.value.trim(),
        supervisor_path: supervisorPath.value.trim(),
        nextion_path: nextionPath.value.trim(),
        spiffs_path: spiffsPath.value.trim()
      }), 'échec enregistrement');
      setUpgradeMessage('Configuration enregistrée.');
    }

    async function refreshUpgradeStatus() {
      try {
        updateUpgradeView(await fetchOkJson('/api/fwupdate/status', { cache: 'no-store' }, 'échec lecture état'));
      } catch (err) {
        setUpgradeMessage('Échec de lecture de l\'état : ' + err);
      }
    }

    async function startUpgrade(target) {
      try {
        await saveUpgradeConfig();
        let endpoint = '/fwupdate/nextion';
        if (target === 'supervisor') endpoint = '/fwupdate/supervisor';
        else if (target === 'flowio') endpoint = '/fwupdate/flowio';
        else if (target === 'spiffs') endpoint = '/fwupdate/spiffs';
        await fetchOkJson(endpoint, { method: 'POST' }, 'échec démarrage');
        setUpgradeProgress(1);
        setUpgradeMessage('Demande de mise à jour acceptée pour ' + target + '.');
        await refreshUpgradeStatus();
      } catch (err) {
        setUpgradeMessage('Échec de la mise à jour : ' + err);
      }
    }

    async function onConfigPageShown() {
      if (!wifiConfigLoadedOnce) {
        wifiConfigLoadedOnce = true;
        await loadWifiConfig();
      }
      if (!wifiScanAutoRequested) {
        wifiScanAutoRequested = true;
        await refreshWifiScanStatus(true);
      } else {
        await refreshWifiScanStatus(false);
      }
    }

    async function onControlPageShown() {
      await ensureFlowCfgLoaded(false);
    }

    async function onLocalConfigPageShown() {
      if (!flowCfgDocsLoaded) {
        await chargerFlowCfgDocs();
      }
      if (!supCfgLoadedOnce) {
        supCfgLoadedOnce = true;
        await chargerSupervisorCfgModules(false);
        return;
      }
      await chargerSupervisorCfgModules(true);
    }

    function fmtFlowStatusVal(v) {
      if (v === null || typeof v === 'undefined') return '-';
      if (typeof v === 'string') {
        const trimmed = v.trim();
        if (!trimmed || /^__FLOW_[A-Z0-9_]+__$/.test(trimmed)) return '-';
        return trimmed;
      }
      return String(v);
    }

    function fmtFlowCount(v) {
      const n = Number(v);
      return Number.isFinite(n) ? String(Math.max(0, Math.round(n))) : '-';
    }

    function buildFlowStatusBoolIcon(v) {
      const ok = !!v;
      const span = document.createElement('span');
      span.className = ok ? 'status-bool is-true' : 'status-bool is-false';
      span.setAttribute('role', 'img');
      span.setAttribute('aria-label', ok ? 'OK' : 'NOK');
      span.title = ok ? 'OK' : 'NOK';
      span.textContent = ok ? 'OK' : 'KO';
      return span;
    }

    function buildFlowStatusReadonlySwitch(v) {
      const on = !!v;
      const sw = document.createElement('span');
      sw.className = 'md3-switch status-toggle-readonly';
      sw.setAttribute('role', 'img');
      sw.setAttribute('aria-label', on ? 'Actif' : 'Inactif');
      sw.title = on ? 'Actif' : 'Inactif';

      const input = document.createElement('input');
      input.type = 'checkbox';
      input.checked = on;
      input.disabled = true;
      input.tabIndex = -1;
      input.setAttribute('aria-hidden', 'true');

      const track = document.createElement('span');
      track.className = 'md3-track';

      const thumb = document.createElement('span');
      thumb.className = 'md3-thumb';

      sw.appendChild(input);
      sw.appendChild(track);
      sw.appendChild(thumb);
      return sw;
    }

    function buildFlowReadonlyStateTile(label, value, options) {
      const stateKnown = typeof value === 'boolean';
      const opts = options && typeof options === 'object' ? options : {};
      const activeText = typeof opts.activeText === 'string' && opts.activeText.trim()
        ? opts.activeText.trim()
        : 'Actif';
      const inactiveText = typeof opts.inactiveText === 'string' && opts.inactiveText.trim()
        ? opts.inactiveText.trim()
        : 'Inactif';
      const unknownText = typeof opts.unknownText === 'string' && opts.unknownText.trim()
        ? opts.unknownText.trim()
        : 'Indisponible';

      const tile = document.createElement('div');
      tile.className = 'status-state-tile ' + (stateKnown ? (value ? 'is-true' : 'is-false') : 'is-empty');
      tile.setAttribute('role', 'img');
      tile.setAttribute(
        'aria-label',
        label + ' : ' + (stateKnown ? (value ? activeText : inactiveText) : unknownText)
      );

      const title = document.createElement('div');
      title.className = 'status-state-title';
      title.textContent = label;
      tile.appendChild(title);

      const state = document.createElement('div');
      state.className = 'status-state-value';

      const dot = document.createElement('span');
      dot.className = 'status-state-dot';
      state.appendChild(dot);

      const text = document.createElement('span');
      text.textContent = stateKnown ? (value ? activeText : inactiveText) : unknownText;
      state.appendChild(text);

      tile.appendChild(state);
      return tile;
    }

    function buildFlowReadonlyStateGrid(items) {
      return buildNodeGrid('status-state-grid', items);
    }

    function fmtFlowUptime(ms) {
      if (!Number.isFinite(ms) || ms < 0) return '-';
      const sec = Math.floor(ms / 1000);
      if (sec < 60) return sec + (sec > 1 ? ' secondes' : ' seconde');
      const min = Math.floor(sec / 60);
      if (min < 60) return min + (min > 1 ? ' minutes' : ' minute');
      const hours = Math.floor(min / 60);
      if (hours < 24) return hours + (hours > 1 ? ' heures' : ' heure');
      const days = Math.floor(hours / 24);
      if (days < 30) return days + (days > 1 ? ' jours' : ' jour');
      const months = Math.floor(days / 30);
      return months + (months > 1 ? ' mois' : ' mois');
    }

    function fmtFlowRelativeAge(ms) {
      if (!Number.isFinite(ms) || ms < 0) return '-';
      const sec = Math.floor(ms / 1000);
      if (sec < 60) return sec + ' s';
      const min = Math.floor(sec / 60);
      if (min < 60) return min + ' min';
      const hours = Math.floor(min / 60);
      if (hours < 24) return hours + ' h';
      const days = Math.floor(hours / 24);
      if (days < 30) return days + ' j';
      const months = Math.floor(days / 30);
      return months + ' mois';
    }

    function fmtFlowBytes(bytes) {
      const n = Number(bytes);
      if (!Number.isFinite(n) || n < 0) return '-';
      if (n < 1024) return Math.round(n) + ' B';
      if (n < (1024 * 1024)) return Math.round(n / 1024) + ' KB';
      return (n / (1024 * 1024)).toFixed(1) + ' MB';
    }

    function setDrawerRuntimeMetaValues(heapFreeText, heapMinText) {
      if (!appHeapSummary) return;
      const heapFreeValue = String(heapFreeText || '-');
      const heapMinValue = String(heapMinText || '-');
      appHeapSummary.textContent = 'Heap: ' + heapFreeValue + ' / ' + heapMinValue;
    }

    function isDrawerRuntimeMetaVisible() {
      return !!appRuntimeMeta && !isMobileLayout() && !drawer.classList.contains('collapsed');
    }

    function fmtFlowFixed(value, decimals, unit) {
      if (value === null || typeof value === 'undefined') return '-';
      if (typeof value === 'string' && value.trim().length === 0) return '-';
      const n = Number(value);
      if (!Number.isFinite(n)) return '-';
      const rendered = decimals > 0 ? n.toFixed(decimals) : String(Math.round(n));
      return unit ? (rendered + ' ' + unit) : rendered;
    }

    function clampFlowValue(v, min, max) {
      if (v < min) return min;
      if (v > max) return max;
      return v;
    }

    function rssiToPercent(rssi) {
      const n = Number(rssi);
      if (!Number.isFinite(n)) return null;
      const bounded = clampFlowValue(n, -95, -50);
      return Math.round(((bounded + 95) / 45) * 100);
    }

    function describeFlowRssi(percent) {
      if (!Number.isFinite(percent)) return 'Indisponible';
      if (percent >= 75) return 'Tres bon';
      if (percent >= 55) return 'Bon';
      if (percent >= 35) return 'Correct';
      return 'Faible';
    }

    function buildFlowRssiGauge(rssi, hasRssi) {
      const wrapper = document.createElement('div');
      wrapper.className = 'status-gauge' + (hasRssi ? '' : ' is-empty');

      const label = document.createElement('span');
      label.className = 'status-gauge-label';

      const track = document.createElement('span');
      track.className = 'status-gauge-track';
      const fill = document.createElement('span');
      fill.className = 'status-gauge-fill';

      if (hasRssi) {
        const percent = rssiToPercent(rssi);
        fill.style.width = (percent === null ? 0 : percent) + '%';
        label.textContent = describeFlowRssi(percent) + ' (' + fmtFlowStatusVal(rssi) + ' dBm)';
      } else {
        fill.style.width = '0%';
        label.textContent = 'Signal indisponible';
      }

      track.appendChild(fill);
      wrapper.appendChild(track);
      wrapper.appendChild(label);
      return wrapper;
    }

    function createFlowSvgElement(tagName, attrs) {
      const el = document.createElementNS('http://www.w3.org/2000/svg', tagName);
      Object.keys(attrs || {}).forEach((key) => {
        if (attrs[key] === null || typeof attrs[key] === 'undefined') return;
        el.setAttribute(key, String(attrs[key]));
      });
      return el;
    }

    function flowGaugePolarToCartesian(cx, cy, radius, angleDeg) {
      const rad = ((angleDeg - 90) * Math.PI) / 180;
      return {
        x: cx + (radius * Math.cos(rad)),
        y: cy + (radius * Math.sin(rad))
      };
    }

    function buildFlowGaugeArcPath(cx, cy, radius, startAngle, endAngle) {
      const start = flowGaugePolarToCartesian(cx, cy, radius, startAngle);
      const end = flowGaugePolarToCartesian(cx, cy, radius, endAngle);
      const largeArc = Math.abs(endAngle - startAngle) > 180 ? 1 : 0;
      const sweep = endAngle >= startAngle ? 1 : 0;
      return 'M ' + start.x.toFixed(2) + ' ' + start.y.toFixed(2) +
        ' A ' + radius + ' ' + radius + ' 0 ' + largeArc + ' ' + sweep + ' ' +
        end.x.toFixed(2) + ' ' + end.y.toFixed(2);
    }

    function buildFlowGaugeTrianglePoints(cx, cy, angleDeg, tipRadius, baseRadius, halfWidth) {
      const tip = flowGaugePolarToCartesian(cx, cy, tipRadius, angleDeg);
      const baseCenter = flowGaugePolarToCartesian(cx, cy, baseRadius, angleDeg);
      const rad = ((angleDeg - 90) * Math.PI) / 180;
      const tangentX = -Math.sin(rad);
      const tangentY = Math.cos(rad);
      const baseLeftX = baseCenter.x + (tangentX * halfWidth);
      const baseLeftY = baseCenter.y + (tangentY * halfWidth);
      const baseRightX = baseCenter.x - (tangentX * halfWidth);
      const baseRightY = baseCenter.y - (tangentY * halfWidth);
      return [
        tip.x.toFixed(2) + ',' + tip.y.toFixed(2),
        baseLeftX.toFixed(2) + ',' + baseLeftY.toFixed(2),
        baseRightX.toFixed(2) + ',' + baseRightY.toFixed(2)
      ].join(' ');
    }

    function fmtFlowGaugeNumber(value, decimals) {
      const n = Number(value);
      if (!Number.isFinite(n)) return '-';
      return decimals > 0 ? n.toFixed(decimals) : String(Math.round(n));
    }

    function fmtFlowGaugeLabel(value, decimals, unit) {
      const rendered = fmtFlowGaugeNumber(value, decimals);
      if (rendered === '-') return rendered;
      return unit ? (rendered + ' ' + unit) : rendered;
    }

    function createFlowFiveZoneBands(config) {
      const min = Number(config && config.min);
      const max = Number(config && config.max);
      const criticalLowEnd = Number(config && config.criticalLowEnd);
      const warningLowEnd = Number(config && config.warningLowEnd);
      const warningHighStart = Number(config && config.warningHighStart);
      const criticalHighStart = Number(config && config.criticalHighStart);

      if (
        !Number.isFinite(min) ||
        !Number.isFinite(max) ||
        !Number.isFinite(criticalLowEnd) ||
        !Number.isFinite(warningLowEnd) ||
        !Number.isFinite(warningHighStart) ||
        !Number.isFinite(criticalHighStart)
      ) {
        return [];
      }

      if (
        !(min < criticalLowEnd) ||
        !(criticalLowEnd < warningLowEnd) ||
        !(warningLowEnd < warningHighStart) ||
        !(warningHighStart < criticalHighStart) ||
        !(criticalHighStart < max)
      ) {
        return [];
      }

      return [
        { from: min, to: criticalLowEnd, color: config.criticalLowColor || '#D14C66' },
        { from: criticalLowEnd, to: warningLowEnd, color: config.warningLowColor || '#F0B255' },
        { from: warningLowEnd, to: warningHighStart, color: config.okColor || '#2F9E68' },
        { from: warningHighStart, to: criticalHighStart, color: config.warningHighColor || '#F0B255' },
        { from: criticalHighStart, to: max, color: config.criticalHighColor || '#D14C66' }
      ];
    }

    function resolveFlowGaugeValueColor(value, bands) {
      const numericValue = Number(value);
      if (!Number.isFinite(numericValue)) return '#102B4C';
      if (!Array.isArray(bands) || bands.length === 0) return '#102B4C';
      const minBound = Number(bands[0].from);
      const maxBound = Number(bands[bands.length - 1].to);
      const clampedValue = (
        Number.isFinite(minBound) &&
        Number.isFinite(maxBound) &&
        maxBound > minBound
      ) ? clampFlowValue(numericValue, minBound, maxBound) : numericValue;
      for (let i = 0; i < bands.length; ++i) {
        const band = bands[i];
        const from = Number(band.from);
        const to = Number(band.to);
        if (!Number.isFinite(from) || !Number.isFinite(to)) continue;
        if (clampedValue >= from && clampedValue <= to) {
          return band.color || '#102B4C';
        }
      }
      return '#102B4C';
    }

    function buildFlowArcGauge(config) {
      const min = Number(config && config.min);
      const max = Number(config && config.max);
      const rawValue = Number(config && config.value);
      const hasValue = Number.isFinite(rawValue);
      const decimals = Math.max(0, Number(config && config.decimals) || 0);
      const unit = typeof (config && config.unit) === 'string' ? config.unit.trim() : '';
      const label = String((config && config.label) || 'Mesure');
      const startAngle = -100;
      const sweepAngle = 200;
      const endAngle = startAngle + sweepAngle;
      const cx = 120;
      const cy = 84;
      const radius = 60;
      const gapAngle = 2.2;
      const bands = (Array.isArray(config && config.bands) ? config.bands : [])
        .map((band) => ({
          from: clampFlowValue(Number(band.from), min, max),
          to: clampFlowValue(Number(band.to), min, max),
          color: band.color || '#AFC3D2'
        }))
        .filter((band) => Number.isFinite(band.from) && Number.isFinite(band.to) && band.to > band.from);
      const activeBands = bands.length > 0 ? bands : [{ from: min, to: max, color: '#AFC3D2' }];
      const gaugeColor = resolveFlowGaugeValueColor(rawValue, activeBands);
      const valueToAngle = function(value) {
        if (!Number.isFinite(min) || !Number.isFinite(max) || max <= min) return startAngle;
        const ratio = (clampFlowValue(value, min, max) - min) / (max - min);
        return startAngle + (ratio * sweepAngle);
      };

      const wrapper = document.createElement('div');
      wrapper.className = 'status-arc-gauge' + (hasValue ? '' : ' is-empty');
      wrapper.setAttribute('role', 'img');
      wrapper.setAttribute(
        'aria-label',
        label + ' : ' + (hasValue ? fmtFlowGaugeLabel(rawValue, decimals, unit) : 'indisponible')
      );

      const svg = createFlowSvgElement('svg', {
        class: 'status-arc-gauge-svg',
        viewBox: '0 0 240 118',
        'aria-hidden': 'true'
      });

      svg.appendChild(createFlowSvgElement('path', {
        class: 'status-arc-gauge-track',
        d: buildFlowGaugeArcPath(cx, cy, radius, startAngle, endAngle)
      }));

      activeBands.forEach((band, index) => {
        const segmentStart = valueToAngle(band.from) + (index > 0 ? (gapAngle / 2) : 0);
        const segmentEnd = valueToAngle(band.to) - (index < (activeBands.length - 1) ? (gapAngle / 2) : 0);
        if (!(segmentEnd > segmentStart)) return;
        svg.appendChild(createFlowSvgElement('path', {
          class: 'status-arc-gauge-band',
          d: buildFlowGaugeArcPath(cx, cy, radius, segmentStart, segmentEnd),
          stroke: band.color
        }));
      });

      const markerAngle = hasValue ? valueToAngle(rawValue) : ((startAngle + endAngle) / 2);
      svg.appendChild(createFlowSvgElement('polygon', {
        class: 'status-arc-gauge-marker',
        points: buildFlowGaugeTrianglePoints(cx, cy, markerAngle, radius + 6, radius + 18, 5),
        fill: hasValue ? gaugeColor : '#8FA1B3'
      }));

      const valueText = createFlowSvgElement('text', {
        class: 'status-arc-gauge-value',
        x: cx,
        y: 74,
        fill: hasValue ? gaugeColor : '#8FA1B3'
      });
      valueText.textContent = hasValue ? fmtFlowGaugeLabel(rawValue, decimals, unit) : '--';
      svg.appendChild(valueText);

      wrapper.appendChild(svg);

      const title = document.createElement('div');
      title.className = 'status-arc-gauge-title';
      title.textContent = label;
      wrapper.appendChild(title);

      return wrapper;
    }

    function buildFlowArcGaugeGrid(items) {
      return buildNodeGrid('status-arc-grid', items);
    }

    window.FlowWebComponents = Object.assign({}, window.FlowWebComponents, {
      buildArcGauge: buildFlowArcGauge,
      createFiveZoneBands: createFlowFiveZoneBands
    });

    function appendFlowStatusRow(kv, label, value) {
      const line = document.createElement('div');
      const key = document.createElement('span');
      key.textContent = label;
      const renderedValue = document.createElement('b');
      if (value && typeof value === 'object' && typeof value.nodeType === 'number') {
        renderedValue.appendChild(value);
      } else {
        renderedValue.textContent = fmtFlowStatusVal(value);
      }
      line.appendChild(key);
      line.appendChild(renderedValue);
      kv.appendChild(line);
    }

    function createFlowLiveValue(kind, initialMs, nowMs) {
      const node = document.createElement('span');
      node.dataset.flowLive = kind;
      node.dataset.baseMs = String(Number(initialMs) || 0);
      node.dataset.baseNow = String(Number(nowMs) || Date.now());
      updateFlowLiveValue(node, Date.now());
      return node;
    }

    function updateFlowLiveValue(node, nowMs) {
      if (!node) return;
      const kind = String(node.dataset.flowLive || '');
      if (kind !== 'uptime') return;
      const baseMs = Number(node.dataset.baseMs) || 0;
      const baseNow = Number(node.dataset.baseNow) || nowMs;
      const value = baseMs + Math.max(0, nowMs - baseNow);
      node.textContent = formatRuntimeDurationMs(value);
    }

    function refreshFlowStatusLiveValues() {
      if (!isPageActive('page-status') || !flowStatusGrid) return;
      const nowMs = Date.now();
      flowStatusGrid.querySelectorAll('[data-flow-live]').forEach((node) => updateFlowLiveValue(node, nowMs));
    }

    function ensureFlowStatusLiveTimer() {
      if (flowStatusLiveTimer) return;
      flowStatusLiveTimer = setInterval(() => {
        refreshFlowStatusLiveValues();
      }, 1000);
    }

    function buildFlowStatusCardIcon(iconKey, ok, label) {
      const iconText = {
        wifi: 'WF',
        supervisor: 'SUP',
        system: 'SYS',
        mqtt: 'MQ',
        pool: 'PL',
        pump: 'PP'
      };
      const span = document.createElement('span');
      span.className = ok ? 'status-card-icon is-true' : 'status-card-icon is-false';
      span.setAttribute('role', 'img');
      span.setAttribute('aria-label', label || (ok ? 'OK' : 'NOK'));
      span.title = label || (ok ? 'OK' : 'NOK');
      span.textContent = iconText[iconKey] || iconText.system;
      return span;
    }

    function buildMqttStatsStrip(items) {
      const wrapper = document.createElement('div');
      wrapper.className = 'status-mqtt-strip';
      (items || []).forEach((item) => {
        const numericValue = Number(item ? item.value : null);
        const hasIssue = Number.isFinite(numericValue) && numericValue > 0;
        const cell = document.createElement('div');
        cell.className = 'status-mqtt-metric ' + (hasIssue ? 'is-alert' : 'is-ok');
        if (item && typeof item.title === 'string' && item.title.trim()) {
          cell.title = item.title.trim() + ' : ' + fmtFlowCount(item.value);
          cell.setAttribute('aria-label', cell.title);
        }

        const label = document.createElement('span');
        label.className = 'status-mqtt-metric-label';
        label.textContent = String(item && item.label ? item.label : '').slice(0, 1) || '-';

        const value = document.createElement('strong');
        value.className = 'status-mqtt-metric-value';
        value.textContent = fmtFlowCount(item ? item.value : null);

        cell.appendChild(label);
        cell.appendChild(value);
        wrapper.appendChild(cell);
      });
      return wrapper;
    }

    function normalizeIpValue(ip) {
      if (typeof ip === 'string') {
        const trimmed = ip.trim();
        return trimmed || '-';
      }
      if (Array.isArray(ip)) {
        return ip.map((part) => String(part)).join('.') || '-';
      }
      if (ip && typeof ip === 'object') {
        const keys = Object.keys(ip).sort((a, b) => Number(a) - Number(b));
        if (keys.length > 0) {
          return keys.map((key) => String(ip[key])).join('.') || '-';
        }
      }
      if (typeof ip === 'number' && Number.isFinite(ip)) {
        return String(ip);
      }
      return '-';
    }

    function buildFlowStatusFromDomains(domainData) {
      const merged = { ok: true };
      let anyDomainOk = false;

      const system = domainData.system;
      if (system && system.ok === true) {
        anyDomainOk = true;
        merged.fw = system.fw || '';
        merged.upms = system.upms ?? 0;
        merged.heap = (system.heap && typeof system.heap === 'object') ? system.heap : {};
      }

      const wifi = domainData.wifi;
      if (wifi && wifi.ok === true && wifi.wifi && typeof wifi.wifi === 'object') {
        anyDomainOk = true;
        merged.wifi = Object.assign({}, wifi.wifi, { ip: normalizeIpValue(wifi.wifi.ip) });
      }

      const mqtt = domainData.mqtt;
      if (mqtt && mqtt.ok === true && mqtt.mqtt && typeof mqtt.mqtt === 'object') {
        anyDomainOk = true;
        merged.mqtt = mqtt.mqtt;
      }

      const pool = domainData.pool;
      if (pool && pool.ok === true && pool.pool && typeof pool.pool === 'object') {
        anyDomainOk = true;
        merged.pool = pool.pool;
      }

      const i2c = domainData.i2c;
      if (i2c && i2c.ok === true && i2c.i2c && typeof i2c.i2c === 'object') {
        anyDomainOk = true;
        merged.i2c = i2c.i2c;
      }

      return anyDomainOk ? merged : null;
    }

    function getCachedFlowStatusData() {
      const domainData = {};
      flowStatusDomainKeys.forEach((domainKey) => {
        domainData[domainKey] = flowStatusDomainCache[domainKey].data;
      });
      return buildFlowStatusFromDomains(domainData);
    }

    async function fetchFlowStatusDomain(domainKey, forceRefresh) {
      const cacheEntry = flowStatusDomainCache[domainKey];
      const now = Date.now();
      const cacheValid = !!cacheEntry.data && ((now - cacheEntry.fetchedAt) < flowStatusDomainTtlMs);
      if (!forceRefresh && cacheValid) {
        return cacheEntry.data;
      }

      try {
        const data = await fetchOkJson(
          '/api/flow/status/domain?d=' + encodeURIComponent(domainKey),
          { cache: 'no-store' },
          'statut ' + domainKey + ' indisponible',
          fetchFlowRemoteQueued
        );
        cacheEntry.data = data;
        cacheEntry.fetchedAt = Date.now();
        return data;
      } catch (err) {
        if (cacheEntry.data) {
          return cacheEntry.data;
        }
        throw err;
      }
    }

    function appendFlowStatusCard(config) {
      const card = document.createElement('div');
      card.className = 'status-card';
      if (config.cardClass) {
        card.classList.add(config.cardClass);
      }

      const head = document.createElement('div');
      head.className = 'status-card-head';
      const titleBlock = document.createElement('div');

      const heading = document.createElement('h3');
      heading.textContent = config.title;
      titleBlock.appendChild(heading);

      if (config.summary) {
        const summary = document.createElement('p');
        summary.className = 'status-card-summary';
        summary.textContent = config.summary;
        titleBlock.appendChild(summary);
      }

      head.appendChild(titleBlock);
      head.appendChild(buildFlowStatusCardIcon(config.icon, !!config.ok, config.iconLabel));
      card.appendChild(head);

      const rows = Array.isArray(config.rows) ? config.rows : [];
      if (rows.length > 0) {
        const kv = document.createElement('div');
        kv.className = 'status-kv';
        rows.forEach((row) => appendFlowStatusRow(kv, row[0], row[1]));
        card.appendChild(kv);
      }
      (config.extras || []).forEach((extra) => {
        if (!extra || typeof extra.nodeType !== 'number') return;
        card.appendChild(extra);
      });
      flowStatusGrid.appendChild(card);
    }

    function createSkeletonLine(className, widthPercent) {
      const line = document.createElement('div');
      line.className = className ? ('skeleton-line ' + className) : 'skeleton-line';
      if (Number.isFinite(widthPercent) && widthPercent > 0) {
        line.style.width = widthPercent + '%';
      }
      return line;
    }

    function appendFlowStatusSkeletonCard() {
      const card = document.createElement('div');
      card.className = 'status-card status-card-skeleton';
      const title = createSkeletonLine('skeleton-title', 46);
      card.appendChild(title);

      const kv = document.createElement('div');
      kv.className = 'status-kv';
      const widths = [
        [44, 24],
        [40, 30],
        [35, 20],
        [48, 26]
      ];
      widths.forEach((pair) => {
        const row = document.createElement('div');
        row.appendChild(createSkeletonLine('skeleton-key', pair[0]));
        row.appendChild(createSkeletonLine('skeleton-value', pair[1]));
        kv.appendChild(row);
      });
      card.appendChild(kv);
      flowStatusGrid.appendChild(card);
    }

    function renderFlowStatusSkeleton() {
      flowStatusChip.textContent = 'chargement...';
      flowStatusGrid.innerHTML = '';
      for (let i = 0; i < 6; ++i) {
        appendFlowStatusSkeletonCard();
      }
      flowStatusRaw.hidden = true;
      flowStatusRaw.classList.remove('is-skeleton');
      flowStatusRaw.innerHTML = '';
    }

    function renderFlowStatus(data) {
      const wifi = (data && typeof data.wifi === 'object') ? data.wifi : {};
      const mqtt = (data && typeof data.mqtt === 'object') ? data.mqtt : {};
      const pool = (data && typeof data.pool === 'object') ? data.pool : {};
      const heap = (data && data.heap && typeof data.heap === 'object') ? data.heap : {};
      const i2c = (data && data.i2c && typeof data.i2c === 'object') ? data.i2c : {};
      const firmware = fmtFlowStatusVal(data.fw);
      const uptimeMs = Number(data.upms) || 0;
      const wifiReady = !!wifi.rdy;
      const wifiIp = normalizeIpValue(wifi.ip);
      const wifiHasRssi = !!wifi.hrss;
      const wifiRssi = wifi.rssi ?? '-';
      const mqttReady = !!mqtt.rdy;
      const mqttServer = fmtFlowStatusVal(mqtt.srv);
      const mqttRxDrop = mqtt.rxdrp ?? 0;
      const mqttParseFail = mqtt.prsf ?? 0;
      const mqttHandlerFail = mqtt.hndf ?? 0;
      const mqttOversizeDrop = mqtt.ovr ?? 0;
      const i2cLinkOk = !!i2c.lnk;
      const mqttIssueCount = (Number(mqttRxDrop) || 0) +
        (Number(mqttParseFail) || 0) +
        (Number(mqttHandlerFail) || 0) +
        (Number(mqttOversizeDrop) || 0);
      const waterTemp = pool.wat;
      const airTemp = pool.air;
      const phValue = pool.ph;
      const orpValue = pool.orp;
      const hasPoolModes = !!pool.has;
      const autoModeOn = hasPoolModes ? !!pool.auto : null;
      const winterModeOn = hasPoolModes ? !!pool.wint : null;
      const filtrationOn = (typeof pool.fil === 'boolean') ? pool.fil : null;
      const poolMetricsReady =
        Number.isFinite(Number(waterTemp)) ||
        Number.isFinite(Number(airTemp)) ||
        Number.isFinite(Number(phValue)) ||
        Number.isFinite(Number(orpValue));
      const poolStatesReady =
        typeof autoModeOn === 'boolean' ||
        typeof filtrationOn === 'boolean' ||
        typeof winterModeOn === 'boolean';
      const heapFree = ('free' in heap) ? heap.free : null;
      const heapMin = ('min_free' in heap) ? heap.min_free : null;
      const systemReady = firmware !== '-' || uptimeMs > 0 || heapFree !== null;
      const supervisorHeapFree = ('free' in supervisorHeap) ? supervisorHeap.free : null;
      const supervisorHeapMin = ('min_free' in supervisorHeap) ? supervisorHeap.min_free : null;
      const supervisorReady =
        supervisorFirmwareVersion !== '-' ||
        supervisorUptimeMs > 0 ||
        supervisorHeapFree !== null;
      const poolGaugeNodes = [
        buildFlowArcGauge({
          label: 'Temperature eau',
          value: waterTemp,
          min: 0,
          max: 40,
          unit: '°C',
          decimals: 1,
          bands: createFlowFiveZoneBands({
            min: 0,
            criticalLowEnd: 8,
            warningLowEnd: 14,
            warningHighStart: 30,
            criticalHighStart: 34,
            max: 40
          })
        }),
        buildFlowArcGauge({
          label: 'Temperature air',
          value: airTemp,
          min: -10,
          max: 45,
          unit: '°C',
          decimals: 1,
          bands: createFlowFiveZoneBands({
            min: -10,
            criticalLowEnd: 0,
            warningLowEnd: 8,
            warningHighStart: 28,
            criticalHighStart: 35,
            max: 45
          })
        }),
        buildFlowArcGauge({
          label: 'pH',
          value: phValue,
          min: 6.4,
          max: 8.4,
          decimals: 2,
          bands: createFlowFiveZoneBands({
            min: 6.4,
            criticalLowEnd: 6.8,
            warningLowEnd: 7.0,
            warningHighStart: 7.6,
            criticalHighStart: 7.8,
            max: 8.4
          })
        }),
        buildFlowArcGauge({
          label: 'ORP',
          value: orpValue,
          min: 350,
          max: 900,
          unit: 'mV',
          decimals: 0,
          bands: createFlowFiveZoneBands({
            min: 350,
            criticalLowEnd: 500,
            warningLowEnd: 620,
            warningHighStart: 760,
            criticalHighStart: 820,
            max: 900
          })
        })
      ];
      const poolGaugeGrid = buildFlowArcGaugeGrid(poolGaugeNodes);
      const poolStateGrid = buildFlowReadonlyStateGrid([
        buildFlowReadonlyStateTile('Mode auto', autoModeOn, {
          activeText: 'Actif',
          inactiveText: 'Manuel'
        }),
        buildFlowReadonlyStateTile('Filtration', filtrationOn, {
          activeText: 'En marche',
          inactiveText: 'Arret'
        }),
        buildFlowReadonlyStateTile('Hivernage', winterModeOn, {
          activeText: 'Actif',
          inactiveText: 'Arret'
        })
      ]);

      flowStatusGrid.innerHTML = '';
      appendFlowStatusCard({
        title: 'WiFi',
        icon: 'wifi',
        ok: wifiReady,
        iconLabel: wifiReady ? 'WiFi connecte' : 'WiFi deconnecte',
        rows: [
          ['Adresse IP', wifiIp],
          ['Signal', buildFlowRssiGauge(wifiRssi, wifiHasRssi)]
        ]
      });
      appendFlowStatusCard({
        title: 'MQTT',
        cardClass: 'status-card-mqtt',
        icon: 'mqtt',
        ok: mqttReady,
        iconLabel: mqttReady ? 'MQTT connecte' : 'MQTT deconnecte',
        rows: [
          ['Serveur', mqttServer]
        ],
        extras: [
          buildMqttStatsStrip([
            {
              label: 'Anomalies',
              title: 'Anomalies MQTT totalisees (ignores + contenu invalide + erreurs de traitement)',
              value: mqttIssueCount
            },
            {
              label: 'Ignores',
              title: 'Messages MQTT ignores ou rejetes (drops RX + payload trop volumineux)',
              value: (Number(mqttRxDrop) || 0) + (Number(mqttOversizeDrop) || 0)
            },
            {
              label: 'Contenu',
              title: 'Messages MQTT recus mais invalides ou impossibles a parser',
              value: mqttParseFail
            },
            {
              label: 'Traitement',
              title: 'Messages MQTT recus mais ayant echoue pendant le traitement applicatif',
              value: mqttHandlerFail
            }
          ])
        ]
      });
      appendFlowStatusCard({
        title: 'Mesures Bassin',
        cardClass: 'status-card-pool-metrics',
        icon: 'pool',
        ok: poolMetricsReady,
        iconLabel: poolMetricsReady ? 'Mesures bassin disponibles' : 'Mesures bassin indisponibles',
        rows: [],
        extras: poolGaugeGrid ? [poolGaugeGrid] : []
      });
      appendFlowStatusCard({
        title: 'Etats Bassin',
        icon: 'pump',
        ok: poolStatesReady,
        iconLabel: poolStatesReady ? 'Etats bassin disponibles' : 'Etats bassin indisponibles',
        rows: [],
        extras: poolStateGrid ? [poolStateGrid] : []
      });
      appendFlowStatusCard({
        title: 'Superviseur',
        icon: 'system',
        ok: supervisorReady,
        iconLabel: supervisorReady ? 'Superviseur disponible' : 'Superviseur indisponible',
        rows: [
          ['Firmware', supervisorFirmwareVersion],
          ['Uptime', createFlowLiveValue('uptime', supervisorUptimeMs, Date.now())],
          ['Heap libre', fmtFlowBytes(supervisorHeapFree)],
          ['Heap min', fmtFlowBytes(supervisorHeapMin)]
        ]
      });
      appendFlowStatusCard({
        title: 'Flow.IO',
        icon: 'system',
        ok: systemReady,
        iconLabel: systemReady ? 'Systeme joignable' : 'Systeme indisponible',
        rows: [
          ['Firmware', firmware],
          ['Uptime', createFlowLiveValue('uptime', uptimeMs, Date.now())],
          ['Heap libre', fmtFlowBytes(heapFree)],
          ['Heap min', fmtFlowBytes(heapMin)]
        ]
      });

      flowStatusChip.textContent = i2cLinkOk
        ? 'Flow.IO disponible'
        : 'Connexion Flow.IO a verifier';
      flowStatusRaw.hidden = true;
      flowStatusRaw.classList.remove('is-skeleton');
      flowStatusRaw.innerHTML = '';
      refreshFlowStatusLiveValues();
      const pageStatus = document.getElementById('page-status');
      if (pageStatus && pageStatus.classList.contains('active')) {
        ensureFlowStatusLiveTimer();
      } else {
        stopFlowStatusLiveTimer();
      }
    }

    async function refreshFlowStatus(forceRefresh) {
      const reqSeq = ++flowStatusReqSeq;
      const cachedData = !forceRefresh ? getCachedFlowStatusData() : null;
      if (cachedData) {
        renderFlowStatus(cachedData);
      } else {
        renderFlowStatusSkeleton();
      }
      try {
        const domainData = {};
        for (const domainKey of flowStatusDomainKeys) {
          domainData[domainKey] = await fetchFlowStatusDomain(domainKey, !!forceRefresh);
          if (reqSeq !== flowStatusReqSeq) return;
        }
        if (reqSeq !== flowStatusReqSeq) return;
        const data = buildFlowStatusFromDomains(domainData);
        if (!data) throw new Error('statut indisponible');
        renderFlowStatus(data);
      } catch (err) {
        if (reqSeq !== flowStatusReqSeq) return;
        const fallbackData = getCachedFlowStatusData();
        if (fallbackData) {
          renderFlowStatus(fallbackData);
          flowStatusChip.textContent = 'statut affiche depuis le cache local';
          return;
        }
        flowStatusRaw.hidden = true;
        flowStatusRaw.classList.remove('is-skeleton');
        flowStatusChip.textContent = 'erreur lecture statut';
        flowStatusGrid.innerHTML = '';
        flowStatusRaw.innerHTML = '';
      }
    }

    function stopPoolMeasuresTimer() {
      poolMeasuresPoller.stop();
    }

    function showPoolMeasuresError(err) {
      poolMeasuresGrid.innerHTML = '';
      poolMeasuresStatus.textContent = 'Chargement mesures echoue: ' + err;
    }

    function startPoolMeasuresTimer() {
      poolMeasuresPoller.start();
    }

    async function loadRuntimeManifest(forceRefresh) {
      if (!forceRefresh && runtimeManifestCache && Array.isArray(runtimeManifestCache.values)) {
        return runtimeManifestCache;
      }
      const data = await fetchOkJson(versionedWebAssetUrl('/webinterface/runtimeui.json'), undefined, 'manifeste runtime indisponible');
      if (!Array.isArray(data.values)) throw new Error('manifeste runtime indisponible');
      runtimeManifestCache = data;
      return data;
    }

    function runtimeManifestEntryByKey(manifest, key) {
      const values = Array.isArray(manifest && manifest.values) ? manifest.values : [];
      return values.find((entry) => entry && String(entry.key || '') === String(key || '')) || null;
    }

    async function refreshDrawerRuntimeMeta(forceRefresh) {
      if (!appRuntimeMeta || !isDrawerRuntimeMetaVisible()) return;
      const heapFreeValue =
        supervisorHeap && Object.prototype.hasOwnProperty.call(supervisorHeap, 'free')
          ? supervisorHeap.free
          : null;
      const heapMinValue =
        supervisorHeap && Object.prototype.hasOwnProperty.call(supervisorHeap, 'min_free')
          ? supervisorHeap.min_free
          : null;
      const heapFreeText = heapFreeValue === null ? '-' : fmtFlowBytes(heapFreeValue);
      const heapMinText = heapMinValue === null ? '-' : fmtFlowBytes(heapMinValue);
      setDrawerRuntimeMetaValues(heapFreeText, heapMinText);
    }

    function startDrawerRuntimeTimer() {
      drawerRuntimePoller.start();
    }

    function runtimeManifestMeasureEntries(manifest) {
      const values = Array.isArray(manifest && manifest.values) ? manifest.values : [];
      return values
        .filter((entry) => entry && Number.isFinite(Number(entry.id)))
        .sort((a, b) => {
          const domainA = String(a.domain || '');
          const domainB = String(b.domain || '');
          if (domainA !== domainB) return domainA.localeCompare(domainB, 'fr');
          const groupA = String(a.group || '');
          const groupB = String(b.group || '');
          if (groupA !== groupB) return groupA.localeCompare(groupB, 'fr');
          const orderA = Number(a.order) || 0;
          const orderB = Number(b.order) || 0;
          if (orderA !== orderB) return orderA - orderB;
          return (Number(a.id) || 0) - (Number(b.id) || 0);
        });
    }

    function formatRuntimeDomainLabel(domain) {
      const key = String(domain || '').trim().toLowerCase();
      if (key === 'pool') return 'Pool';
      if (key === 'mqtt') return 'MQTT';
      if (key === 'wifi') return 'WiFi';
      if (key === 'i2c') return 'I2C';
      if (key === 'system') return 'Systeme';
      if (!key) return 'Runtime';
      return key.charAt(0).toUpperCase() + key.slice(1);
    }

    function formatRuntimeGroupCardTitle(domain, group) {
      const domainLabel = formatRuntimeDomainLabel(domain);
      const groupLabel = String(group || '').trim();
      if (!groupLabel) return domainLabel;
      if (groupLabel.localeCompare(domainLabel, 'fr', { sensitivity: 'base' }) === 0) return domainLabel;
      return domainLabel + ' · ' + groupLabel;
    }

    function formatRuntimeDurationMs(ms) {
      const totalMs = Number(ms);
      if (!Number.isFinite(totalMs) || totalMs < 0) return '-';
      if (totalMs < 1000) return Math.round(totalMs) + ' ms';

      const totalSec = Math.floor(totalMs / 1000);
      const days = Math.floor(totalSec / 86400);
      const hours = Math.floor((totalSec % 86400) / 3600);
      const minutes = Math.floor((totalSec % 3600) / 60);
      const seconds = totalSec % 60;
      const parts = [];

      if (days > 0) parts.push(days + ' j');
      if (hours > 0) parts.push(hours + ' h');
      if (minutes > 0) parts.push(minutes + ' min');
      if (seconds > 0 || parts.length === 0) parts.push(seconds + ' s');

      return parts.slice(0, 2).join(' ');
    }

    async function fetchRuntimeValues(ids) {
      const data = await fetchOkJson('/api/runtime/values', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ids: ids })
      }, 'lecture runtime indisponible', fetchFlowRemoteQueued);
      if (!Array.isArray(data.values)) throw new Error('lecture runtime indisponible');
      return data.values;
    }

    function formatRuntimeMeasureValue(entry, runtimeValue) {
      if (!runtimeValue || runtimeValue.status === 'not_found' || runtimeValue.status === 'unavailable') {
        return 'Indisponible';
      }

      const display = runtimeMeasureDisplayKind(entry);
      const type = String(entry.type || runtimeValue.type || '');
      const unit = entry.unit ? String(entry.unit) : '';
      const decimals = Number.isFinite(Number(entry.decimals)) ? Number(entry.decimals) : null;
      const rawValue = runtimeValue.value;

      if (display === 'time' && Number.isFinite(Number(rawValue))) {
        return formatRuntimeDurationMs(Number(rawValue));
      }
      if (type === 'bool') {
        return rawValue ? 'Actif' : 'Arret';
      }
      if (type === 'float' && Number.isFinite(Number(rawValue))) {
        const value = Number(rawValue);
        const text = (decimals !== null) ? value.toFixed(decimals) : String(value);
        return unit ? (text + ' ' + unit) : text;
      }
      if ((type === 'int32' || type === 'uint32' || type === 'enum') && Number.isFinite(Number(rawValue))) {
        if (type === 'enum' && entry.enum && typeof entry.enum === 'object') {
          const enumKey = String(Number(rawValue));
          if (Object.prototype.hasOwnProperty.call(entry.enum, enumKey)) {
            return String(entry.enum[enumKey]);
          }
        }
        const text = String(Number(rawValue));
        return unit ? (text + ' ' + unit) : text;
      }
      if (type === 'string') {
        const text = (rawValue === null || rawValue === undefined || rawValue === '') ? '-' : String(rawValue);
        return unit ? (text + ' ' + unit) : text;
      }
      if (rawValue === null || rawValue === undefined || rawValue === '') return '-';
      return unit ? (String(rawValue) + ' ' + unit) : String(rawValue);
    }

    function runtimeMeasureDisplayKind(entry) {
      const explicit = String(entry && entry.display ? entry.display : '').trim();
      if (explicit === 'gauge') return 'circ-gauge';
      if (explicit === 'circ-gauge' || explicit === 'horiz-gauge' || explicit === 'badge' || explicit === 'boolean' || explicit === 'time' || explicit === 'value') {
        return explicit;
      }
      return String(entry && entry.type ? entry.type : '') === 'bool' ? 'boolean' : 'value';
    }

    function runtimeMeasureDisplayConfig(entry) {
      return (entry && entry.displayConfig && typeof entry.displayConfig === 'object' && !Array.isArray(entry.displayConfig))
        ? entry.displayConfig
        : {};
    }

    function buildRuntimeMeasureCircGaugeNode(entry, runtimeValue) {
      const displayConfig = runtimeMeasureDisplayConfig(entry);
      let bands = [];
      let min = Number(displayConfig.min);
      let max = Number(displayConfig.max);
      if (displayConfig.bands && typeof displayConfig.bands === 'object' && !Array.isArray(displayConfig.bands)) {
        bands = createFlowFiveZoneBands(displayConfig.bands);
        if (!Number.isFinite(min)) min = Number(displayConfig.bands.min);
        if (!Number.isFinite(max)) max = Number(displayConfig.bands.max);
      } else if (Array.isArray(displayConfig.bands)) {
        bands = displayConfig.bands;
      }

      if (!Number.isFinite(min) || !Number.isFinite(max) || max <= min) {
        return null;
      }

      const value = (!runtimeValue || runtimeValue.status === 'not_found' || runtimeValue.status === 'unavailable')
        ? null
        : runtimeValue.value;

      return buildFlowArcGauge({
        label: String(entry.label || entry.key || entry.id || 'Mesure'),
        value: value,
        min: min,
        max: max,
        unit: entry.unit ? String(entry.unit) : '',
        decimals: Number.isFinite(Number(entry.decimals)) ? Number(entry.decimals) : 0,
        bands: bands
      });
    }

    function buildRuntimeMeasureHorizGaugeNode(entry, runtimeValue) {
      const hasValue = !!runtimeValue && runtimeValue.status !== 'not_found' && runtimeValue.status !== 'unavailable' &&
        Number.isFinite(Number(runtimeValue.value));
      return buildFlowRssiGauge(hasValue ? Number(runtimeValue.value) : null, hasValue);
    }

    function buildRuntimeMeasureBooleanNode(entry, runtimeValue) {
      const displayConfig = runtimeMeasureDisplayConfig(entry);
      const value = (!runtimeValue || runtimeValue.status === 'not_found' || runtimeValue.status === 'unavailable')
        ? null
        : (typeof runtimeValue.value === 'boolean' ? runtimeValue.value : null);

      return buildFlowReadonlyStateTile(
        String(entry.label || entry.key || entry.id || 'Etat'),
        value,
        {
          activeText: displayConfig.activeText,
          inactiveText: displayConfig.inactiveText,
          unknownText: displayConfig.unknownText
        }
      );
    }

    function buildRuntimeMeasureBadgeNode(entry, runtimeValue) {
      const displayConfig = runtimeMeasureDisplayConfig(entry);
      let text = formatRuntimeMeasureValue(entry, runtimeValue);
      if (String(entry.type || '') === 'bool') {
        if (!runtimeValue || runtimeValue.status === 'not_found' || runtimeValue.status === 'unavailable') {
          text = String(displayConfig.unknownText || 'Indisponible');
        } else {
          text = runtimeValue.value
            ? String(displayConfig.activeText || 'Actif')
            : String(displayConfig.inactiveText || 'Arret');
        }
      }
      const badge = document.createElement('span');
      badge.className = 'status-chip';
      const badgeLabel = String(displayConfig.badgeLabel || '').trim();
      badge.textContent = badgeLabel ? (badgeLabel + ' : ' + text) : text;
      return badge;
    }

    function renderPoolMeasures(values) {
      const valueById = new Map();
      (values || []).forEach((item) => {
        const id = Number(item && item.id);
        if (Number.isFinite(id)) valueById.set(id, item);
      });

      poolMeasuresGrid.innerHTML = '';
      if (!poolMeasureEntries.length) {
        poolMeasuresStatus.textContent = 'Aucune valeur runtime exposee.';
        return;
      }

      const groups = [];
      const groupsByName = new Map();
      poolMeasureEntries.forEach((entry) => {
        const domainKey = String(entry.domain || 'runtime');
        const groupKey = String(entry.group || '').trim();
        const cardKey = domainKey + '::' + groupKey;
        const cardTitle = formatRuntimeGroupCardTitle(domainKey, groupKey);
        let group = groupsByName.get(cardKey);
        if (!group) {
          group = { name: cardTitle, entries: [] };
          groupsByName.set(cardKey, group);
          groups.push(group);
        }
        group.entries.push(entry);
      });

      groups.forEach((group) => {
        const card = document.createElement('div');
        card.className = 'status-card';

        const heading = document.createElement('h3');
        heading.textContent = group.name;
        card.appendChild(heading);
        const badgeNodes = [];
        const circGaugeNodes = [];
        const horizGaugeRows = [];
        const booleanNodes = [];
        const valueRows = [];

        group.entries.forEach((entry) => {
          const runtimeValue = valueById.get(Number(entry.id));
          const display = runtimeMeasureDisplayKind(entry);
          if (display === 'badge') {
            badgeNodes.push(buildRuntimeMeasureBadgeNode(entry, runtimeValue));
            return;
          }
          if (display === 'circ-gauge') {
            const gaugeNode = buildRuntimeMeasureCircGaugeNode(entry, runtimeValue);
            if (gaugeNode) {
              circGaugeNodes.push(gaugeNode);
              return;
            }
          }
          if (display === 'horiz-gauge') {
            horizGaugeRows.push([
              entry.label || entry.key || String(entry.id),
              buildRuntimeMeasureHorizGaugeNode(entry, runtimeValue)
            ]);
            return;
          }
          if (display === 'boolean') {
            booleanNodes.push(buildRuntimeMeasureBooleanNode(entry, runtimeValue));
            return;
          }
          valueRows.push([
            entry.label || entry.key || String(entry.id),
            formatRuntimeMeasureValue(entry, runtimeValue)
          ]);
        });

        if (badgeNodes.length) {
          const badgeRow = document.createElement('div');
          badgeRow.className = 'status-chip-row';
          badgeNodes.forEach((node) => badgeRow.appendChild(node));
          card.appendChild(badgeRow);
        }

        if (circGaugeNodes.length) {
          const gaugeGrid = buildFlowArcGaugeGrid(circGaugeNodes);
          if (gaugeGrid) card.appendChild(gaugeGrid);
        }

        if (booleanNodes.length) {
          const stateGrid = buildFlowReadonlyStateGrid(booleanNodes);
          if (stateGrid) card.appendChild(stateGrid);
        }

        if (horizGaugeRows.length || valueRows.length) {
          const kv = document.createElement('div');
          kv.className = 'status-kv';
          horizGaugeRows.forEach((row) => appendFlowStatusRow(kv, row[0], row[1]));
          valueRows.forEach((row) => appendFlowStatusRow(kv, row[0], row[1]));
          card.appendChild(kv);
        }

        poolMeasuresGrid.appendChild(card);
      });
      poolMeasuresStatus.textContent =
        'Mesures chargees : ' + poolMeasureEntries.length + ' valeurs.';
    }

    async function refreshPoolMeasures(forceManifest) {
      poolMeasuresStatus.textContent = 'chargement...';

      const manifest = await loadRuntimeManifest(!!forceManifest);
      poolMeasureEntries = runtimeManifestMeasureEntries(manifest);
      const ids = poolMeasureEntries.map((entry) => Number(entry.id)).filter((id) => Number.isFinite(id));
      if (!ids.length) {
        renderPoolMeasures([]);
        return;
      }

      const values = await fetchRuntimeValues(ids);
      renderPoolMeasures(values);
    }

    async function onPoolMeasuresPageShown() {
      try {
        await refreshPoolMeasures(false);
        startPoolMeasuresTimer();
      } catch (err) {
        showPoolMeasuresError(err);
      }
    }

    async function onUpgradePageShown() {
      if (!upgradeCfgLoadedOnce) {
        upgradeCfgLoadedOnce = true;
        await loadUpgradeConfig();
      }
      await refreshUpgradeStatus();
      startUpgradeStatusPolling();
    }

    function stopWifiScanPolling() {
      wifiScanPoller.stop();
    }

    function scheduleWifiScanPolling() {
      wifiScanPoller.schedule(1200);
    }

    function renderWifiScanList(data) {
      const networks = (data && Array.isArray(data.networks)) ? data.networks : [];
      const currentSsid = (wifiSsid.value || '').trim();
      const prevSelected = wifiSsidList.value || '';
      const maxWifiOptionLabelLen = 56;

      wifiSsidList.innerHTML = '';
      const manualOpt = document.createElement('option');
      manualOpt.value = '';
      manualOpt.textContent = 'Saisie manuelle';
      wifiSsidList.appendChild(manualOpt);

      for (const net of networks) {
        if (!net || typeof net.ssid !== 'string' || net.ssid.length === 0) continue;
        if (net.hidden) continue;
        const opt = document.createElement('option');
        const secureSuffix = net.secure ? ' (securise)' : ' (ouvert)';
        const rssiSuffix = Number.isFinite(net.rssi) ? (' ' + net.rssi + ' dBm') : '';
        const fullLabel = net.ssid + secureSuffix + rssiSuffix;
        opt.value = net.ssid;
        opt.textContent = fullLabel.length > maxWifiOptionLabelLen
            ? (fullLabel.slice(0, maxWifiOptionLabelLen - 1) + '…')
            : fullLabel;
        wifiSsidList.appendChild(opt);
      }

      const values = Array.from(wifiSsidList.options).map((o) => o.value);
      if (currentSsid && values.includes(currentSsid)) {
        wifiSsidList.value = currentSsid;
      } else if (prevSelected && values.includes(prevSelected)) {
        wifiSsidList.value = prevSelected;
      } else {
        wifiSsidList.value = '';
      }
    }

    function updateWifiScanStatusText(data, reqError) {
      if (reqError) {
        wifiConfigStatus.textContent = 'Scan WiFi indisponible: ' + reqError;
        return;
      }
      if (!data || data.ok !== true) {
        wifiConfigStatus.textContent = 'Scan WiFi : réponse invalide.';
        return;
      }

      const running = !!data.running;
      const requested = !!data.requested;
      const count = Number.isFinite(data.count) ? data.count : 0;
      const totalFound = Number.isFinite(data.total_found) ? data.total_found : count;
      if (running || requested) {
        wifiConfigStatus.textContent = 'Scan WiFi en cours...';
        return;
      }
      if (count > 0) {
        wifiConfigStatus.textContent = 'Scan WiFi terminé : ' + count + ' réseaux affichés (' + totalFound + ' détectés).';
        return;
      }
      wifiConfigStatus.textContent = 'Aucun réseau visible détecté.';
    }

    function toBool(v) {
      if (typeof v === 'boolean') return v;
      if (typeof v === 'number') return v !== 0;
      if (typeof v === 'string') {
        const s = v.trim().toLowerCase();
        return s === '1' || s === 'true' || s === 'on' || s === 'yes';
      }
      return false;
    }

    async function requestWifiScan(force) {
      return fetchOkJson('/api/wifi/scan', createFormPostOptions({
        force: force ? '1' : '0'
      }), 'échec démarrage scan');
    }

    async function refreshWifiScanStatus(triggerScan) {
      try {
        if (triggerScan) {
          await requestWifiScan(true);
        }
        const data = await fetchOkJson('/api/wifi/scan', { cache: 'no-store' }, 'échec lecture état');

        renderWifiScanList(data);
        updateWifiScanStatusText(data, null);

        if (data.running || data.requested) {
          scheduleWifiScanPolling();
        } else {
          stopWifiScanPolling();
        }
      } catch (err) {
        stopWifiScanPolling();
        updateWifiScanStatusText(null, err);
      }
    }

    async function loadWifiConfig() {
      try {
        const data = await fetchOkJson('/api/wifi/config', { cache: 'no-store' }, 'chargement wifi indisponible');
        wifiEnabled.checked = toBool(data.enabled);
        wifiSsid.value = data.ssid || '';
        wifiPass.value = data.pass || '';
        wifiConfigStatus.textContent = 'Configuration WiFi chargée.';
      } catch (err) {
        wifiConfigStatus.textContent = 'Chargement WiFi échoué: ' + err;
      }
    }

    async function saveWifiConfig() {
      await fetchOkJson('/api/wifi/config', createFormPostOptions({
        enabled: wifiEnabled.checked ? '1' : '0',
        ssid: wifiSsid.value.trim(),
        pass: wifiPass.value
      }), 'échec application');
      wifiConfigStatus.textContent = 'Configuration WiFi appliquée (reconnexion en cours).';
    }

    function nettoyerNomFlowCfg(moduleName) {
      return String(moduleName || '').trim().replace(/^\/+|\/+$/g, '');
    }

    function cheminFlowCfgCourant() {
      return flowCfgPath.length > 0 ? flowCfgPath.join('/') : '';
    }

    function flowCfgTitreDepuisChemin(pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      if (!cleanPath) return 'Configuration';
      return 'Configuration > ' + cleanPath.split('/').join(' > ');
    }

    function flowCfgRootIconMarkup() {
      return '<span class="crumb-root-icon icon-flowcfg" aria-hidden="true"></span>';
    }

    function flowCfgCachedChildren(prefix) {
      const p = nettoyerNomFlowCfg(prefix);
      const key = flowCfgCacheKey(p);
      const node = flowCfgChildrenCache[key];
      if (!node || !Array.isArray(node.children)) return [];
      return node.children
        .filter((name) => !isConfigPathHidden(p ? (p + '/' + name) : name))
        .slice();
    }

    function closeFlowCfgCrumbMenus(except) {
      const wrappers = flowCfgTitle.querySelectorAll('.control-crumb.open');
      wrappers.forEach((wrapper) => {
        if (except && wrapper === except) return;
        wrapper.classList.remove('open');
      });
    }

    function renderFlowCfgTitle(pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      const segs = cleanPath.length > 0 ? cleanPath.split('/') : [];
      flowCfgTitle.innerHTML = '';
      flowCfgTitle.setAttribute('aria-label', flowCfgTitreDepuisChemin(cleanPath));

      const rootBtn = document.createElement('button');
      rootBtn.type = 'button';
      rootBtn.className = 'control-title-root-btn' + (segs.length === 0 ? ' active' : '');
      rootBtn.setAttribute('aria-label', 'Racine configuration');
      rootBtn.innerHTML = flowCfgRootIconMarkup();
      if (segs.length === 0) {
        rootBtn.disabled = true;
      } else {
        rootBtn.addEventListener('click', async () => {
          closeFlowCfgCrumbMenus();
          flowCfgPath = [];
          await renderFlowCfgNavigator(false);
        });
      }
      flowCfgTitle.appendChild(rootBtn);

      if (segs.length === 0) {
        return;
      }

      const rootSep = document.createElement('span');
      rootSep.className = 'control-title-sep';
      rootSep.textContent = '/';
      flowCfgTitle.appendChild(rootSep);

      for (let i = 0; i < segs.length; ++i) {
        if (i > 0) {
          const sep = document.createElement('span');
          sep.className = 'control-title-sep';
          sep.textContent = '/';
          flowCfgTitle.appendChild(sep);
        }

        const crumb = document.createElement('span');
        crumb.className = 'control-crumb';
        const depth = i + 1;
        const isActive = depth === segs.length;

        const labelBtn = document.createElement('button');
        labelBtn.type = 'button';
        labelBtn.className = 'control-title-crumb-btn' + (isActive ? ' active' : '');
        labelBtn.textContent = segs[i];
        if (isActive) {
          labelBtn.disabled = true;
        } else {
          labelBtn.addEventListener('click', async () => {
            closeFlowCfgCrumbMenus();
            flowCfgPath = segs.slice(0, depth);
            await renderFlowCfgNavigator(false);
          });
        }
        crumb.appendChild(labelBtn);

        const parentPrefix = i === 0 ? '' : segs.slice(0, i).join('/');
        const siblings = flowCfgCachedChildren(parentPrefix);
        const menuChoices = siblings.length > 0 ? siblings : [segs[i]];

        const toggleBtn = document.createElement('button');
        toggleBtn.type = 'button';
        toggleBtn.className = 'control-crumb-toggle';
        toggleBtn.setAttribute('aria-label', 'Choisir une branche au niveau ' + (i + 1));
        toggleBtn.innerHTML = '<span class="control-crumb-arrows" aria-hidden="true"></span>';
        toggleBtn.addEventListener('click', (event) => {
          event.stopPropagation();
          const willOpen = !crumb.classList.contains('open');
          closeFlowCfgCrumbMenus(crumb);
          crumb.classList.toggle('open', willOpen);
        });
        crumb.appendChild(toggleBtn);

        const menu = document.createElement('div');
        menu.className = 'control-crumb-menu';
        const sortedChoices = Array.from(new Set(menuChoices)).sort((a, b) => a.localeCompare(b));
        for (const choice of sortedChoices) {
          const item = document.createElement('button');
          item.type = 'button';
          item.className = 'control-crumb-menu-item' + (choice === segs[i] ? ' active' : '');
          item.textContent = choice;
          item.addEventListener('click', async (event) => {
            event.stopPropagation();
            closeFlowCfgCrumbMenus();
            flowCfgPath = segs.slice(0, i).concat([choice]);
            await renderFlowCfgNavigator(false);
          });
          menu.appendChild(item);
        }
        crumb.appendChild(menu);
        flowCfgTitle.appendChild(crumb);
      }
    }

    function renderFlowCfgSections(node, currentPath) {
      flowCfgSections.innerHTML = '';
      const cleanPath = nettoyerNomFlowCfg(currentPath);
      const children = (node && Array.isArray(node.children))
        ? node.children.filter((name) => !isConfigPathHidden(cleanPath ? (cleanPath + '/' + name) : name))
        : [];
      if (children.length === 0) {
        const empty = document.createElement('div');
        empty.className = 'control-chip-empty';
        empty.textContent = 'Aucune sous-section.';
        flowCfgSections.appendChild(empty);
        return;
      }

      for (const child of children) {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'control-chip';
        button.textContent = child;
        button.addEventListener('click', async () => {
          const nextPath = currentPath ? (currentPath + '/' + child) : child;
          flowCfgPath = nextPath.split('/');
          await renderFlowCfgNavigator(false);
        });
        flowCfgSections.appendChild(button);
      }
    }

    function flowCfgCacheKey(prefix) {
      const p = nettoyerNomFlowCfg(prefix);
      return p.length > 0 ? p : '__root__';
    }

    function renderFlowCfgSectionsSkeleton() {
      flowCfgSections.innerHTML = '';
      const widths = [16, 22, 14, 20, 18];
      widths.forEach((w) => {
        const chip = document.createElement('span');
        chip.className = 'control-chip control-chip-skeleton';
        chip.style.width = w + '%';
        flowCfgSections.appendChild(chip);
      });
    }

    function renderFlowCfgFieldsSkeleton() {
      flowCfgFields.innerHTML = '';
      for (let i = 0; i < 5; ++i) {
        const row = document.createElement('div');
        row.className = 'control-row control-row-skeleton';

        const labelWrap = document.createElement('div');
        labelWrap.className = 'control-label-wrap';
        labelWrap.appendChild(createSkeletonLine('', i % 2 === 0 ? 38 : 44));
        if (i % 2 === 0) {
          labelWrap.appendChild(createSkeletonLine('', 62));
        }
        row.appendChild(labelWrap);

        const inputSkel = document.createElement('div');
        if (i === 1) {
          inputSkel.className = 'control-switch-skeleton';
        } else {
          inputSkel.className = 'control-input-skeleton';
        }
        row.appendChild(inputSkel);
        flowCfgFields.appendChild(row);
      }
    }

    function beginFlowCfgLoading(statusText) {
      flowCfgLoadingDepth += 1;
      if (flowCfgLoadingDepth === 1) {
        flowCfgTitle.classList.add('is-loading');
        flowCfgRefreshBtn.disabled = true;
        flowCfgApplyBtn.disabled = true;
        renderFlowCfgSectionsSkeleton();
        renderFlowCfgFieldsSkeleton();
      }
      if (statusText) {
        flowCfgStatus.textContent = statusText;
      }
    }

    function endFlowCfgLoading() {
      if (flowCfgLoadingDepth > 0) {
        flowCfgLoadingDepth -= 1;
      }
      if (flowCfgLoadingDepth === 0) {
        flowCfgTitle.classList.remove('is-loading');
        flowCfgRefreshBtn.disabled = false;
      }
    }

    function stopFlowCfgRetry() {
      if (!flowCfgRetryTimer) return;
      clearTimeout(flowCfgRetryTimer);
      flowCfgRetryTimer = null;
    }

    function scheduleFlowCfgRetry(delayMs) {
      stopFlowCfgRetry();
      flowCfgRetryTimer = setTimeout(() => {
        flowCfgRetryTimer = null;
        if (!isPageActive('page-control')) return;
        ensureFlowCfgLoaded(true).catch(() => {});
      }, delayMs);
    }

    async function chargerFlowCfgDocs() {
      flowCfgDocsLoaded = true;
      try {
        const res = await fetch(versionedWebAssetUrl('/webinterface/cfgdocs.fr.json'));
        const data = await res.json();
        if (!res.ok || !data || typeof data !== 'object') {
          throw new Error('invalid docs payload');
        }
        const docs = (data.docs && typeof data.docs === 'object') ? data.docs : {};
        const meta = (data.meta && typeof data.meta === 'object')
          ? data.meta
          : ((data._meta && typeof data._meta === 'object') ? data._meta : {});
        cfgDocSources = [{ docs: docs, meta: meta }];
        try {
          const modsRes = await fetch(versionedWebAssetUrl('/webinterface/cfgmods.fr.json'));
          const modsData = await modsRes.json();
          if (modsRes.ok && modsData && typeof modsData === 'object') {
            const modsDocs = (modsData.docs && typeof modsData.docs === 'object') ? modsData.docs : {};
            const modsMeta = (modsData.meta && typeof modsData.meta === 'object')
              ? modsData.meta
              : ((modsData._meta && typeof modsData._meta === 'object') ? modsData._meta : {});
            cfgDocSources.push({ docs: modsDocs, meta: modsMeta });
          }
        } catch (err) {
          // Ignore optional manual cfgmods file failures.
        }
      } catch (err) {
        cfgDocSources = [];
      }
    }

    function normalizeDocSource(source) {
      if (!source || typeof source !== 'object') return null;
      const docs = (source.docs && typeof source.docs === 'object') ? source.docs : {};
      const meta = (source.meta && typeof source.meta === 'object')
        ? source.meta
        : ((source._meta && typeof source._meta === 'object') ? source._meta : {});
      return { docs: docs, meta: meta };
    }

    function resolveEnumOptions(enumSetName, sources) {
      const setName = String(enumSetName || '').trim();
      if (!setName) return null;
      for (const src of sources) {
        const normalized = normalizeDocSource(src);
        if (!normalized) continue;
        const enumSets = (normalized.meta && normalized.meta.enum_sets &&
          typeof normalized.meta.enum_sets === 'object')
          ? normalized.meta.enum_sets
          : null;
        if (enumSets && Array.isArray(enumSets[setName])) {
          return enumSets[setName];
        }
      }
      return null;
    }

    function enrichResolvedDoc(doc, sources) {
      if (!doc || typeof doc !== 'object') return null;
      const resolved = Object.assign({}, doc);
      const enumSetName = (typeof resolved.enum_set === 'string') ? resolved.enum_set.trim() : '';
      const enumOptions = resolveEnumOptions(enumSetName, sources);
      if (enumSetName && Array.isArray(enumOptions)) {
        resolved._enumOptions = enumOptions;
      }
      return resolved;
    }

    function formatConfigValueForDisplay(value, displayFormat) {
      if (displayFormat === 'hex' && typeof value === 'number' && Number.isFinite(value)) {
        const raw = Math.max(0, Math.trunc(value));
        const width = raw <= 0xFF ? 2 : 0;
        const hex = raw.toString(16).toUpperCase();
        return '0x' + (width > 0 ? hex.padStart(width, '0') : hex);
      }
      return String(value ?? '');
    }

    function parseConfigNumericValue(rawValue, kind, displayFormat) {
      const raw = String(rawValue ?? '').trim();
      if (displayFormat === 'hex') {
        const normalized = raw.length > 0 ? raw : '0';
        const parsed = normalized.startsWith('0x') || normalized.startsWith('0X')
          ? Number.parseInt(normalized, 16)
          : Number.parseInt(normalized, 16);
        if (!Number.isFinite(parsed)) return 0;
        return (kind === 'float') ? parsed : parsed;
      }
      if (kind === 'float') {
        const parsed = Number.parseFloat(raw);
        return Number.isFinite(parsed) ? parsed : 0;
      }
      const parsed = Number.parseInt(raw, 10);
      return Number.isFinite(parsed) ? parsed : 0;
    }

    function configDocFor(moduleName, key, extraSources) {
      const m = nettoyerNomFlowCfg(moduleName);
      const k = String(key || '').trim();
      if (!m || !k) return null;

      const sources = [];
      if (Array.isArray(extraSources)) {
        extraSources.forEach((src) => {
          const normalized = normalizeDocSource(src);
          if (normalized) sources.push(normalized);
        });
      }
      cfgDocSources.forEach((src) => {
        const normalized = normalizeDocSource(src);
        if (normalized) sources.push(normalized);
      });

      let merged = null;
      for (const source of sources) {
        const docs = source.docs;
        const full = m + '/' + k;
        const wildcard = docs['*/' + k];
        if (wildcard && typeof wildcard === 'object') {
          merged = Object.assign(merged || {}, wildcard);
        }
        const exact = docs[full];
        if (exact && typeof exact === 'object') {
          merged = Object.assign(merged || {}, exact);
        }
      }
      return enrichResolvedDoc(merged, sources);
    }

    function configPathMeta(pathValue) {
      const p = nettoyerNomFlowCfg(pathValue);
      if (!p) return null;
      const sources = [];
      for (const src of cfgDocSources) {
        const normalized = normalizeDocSource(src);
        if (!normalized) continue;
        sources.push(normalized);
      }
      let merged = null;
      for (const normalized of sources) {
        const exact = normalized.docs[p];
        if (exact && typeof exact === 'object') {
          merged = Object.assign(merged || {}, exact);
        }
      }
      return enrichResolvedDoc(merged, sources);
    }

    function isConfigPathHidden(pathValue) {
      const meta = configPathMeta(pathValue);
      return !!(meta && meta.hidden === true);
    }

    function flowCfgApplyPerFieldEnabled(moduleName) {
      const meta = configPathMeta(moduleName);
      return !!(meta && meta.apply_per_field === true);
    }

    async function chargerFlowCfgChildren(prefix, forceReload) {
      const p = nettoyerNomFlowCfg(prefix);
      const key = flowCfgCacheKey(p);
      if (!forceReload && flowCfgChildrenCache[key]) {
        return flowCfgChildrenCache[key];
      }

      const url = p.length > 0
        ? ('/api/flowcfg/children?prefix=' + encodeURIComponent(p))
        : '/api/flowcfg/children';
      const res = await fetchFlowRemoteQueued(url, { cache: 'no-store' });
      const data = await res.json();
      if (!res.ok || !data || data.ok !== true || !Array.isArray(data.children)) {
        throw new Error('liste enfants indisponible');
      }

      const children = data.children
        .filter((name) => typeof name === 'string' && name.length > 0)
        .map((name) => nettoyerNomFlowCfg(name))
        .filter((name) => name.length > 0)
        .sort((a, b) => a.localeCompare(b));

      const node = {
        prefix: p,
        hasExact: !!data.has_exact,
        children: Array.from(new Set(children))
      };
      flowCfgChildrenCache[key] = node;
      return node;
    }

    function resetFlowCfgEditor(message) {
      flowCfgCurrentModule = '';
      flowCfgCurrentData = {};
      flowCfgFields.innerHTML = '';
      flowCfgApplyBtn.hidden = false;
      flowCfgApplyBtn.disabled = true;
      if (message) {
        flowCfgStatus.textContent = message;
      }
    }

    function storeConfigFieldInitialValue(el, value) {
      if (!el) return;
      el.dataset.initialValue = JSON.stringify(value);
    }

    function readConfigFieldValue(el) {
      if (!el) return null;
      const kind = String(el.dataset.kind || '').trim();
      const displayFormat = String(el.dataset.format || '').trim();
      if (kind === 'bool') {
        return !!el.checked;
      }
      if (kind === 'int' || kind === 'float') {
        return parseConfigNumericValue(el.value, kind, displayFormat);
      }
      const masked = el.dataset.masked === '1';
      const raw = String(el.value ?? '');
      if (masked && raw.length === 0) {
        try {
          return JSON.parse(el.dataset.initialValue || 'null');
        } catch (err) {
          return '';
        }
      }
      return raw;
    }

    function configFieldIsDirty(el) {
      if (!el) return false;
      let initialValue = null;
      try {
        initialValue = JSON.parse(el.dataset.initialValue || 'null');
      } catch (err) {
        initialValue = null;
      }
      return JSON.stringify(readConfigFieldValue(el)) !== JSON.stringify(initialValue);
    }

    function updateControlFieldApplyState(inputEl, applyBtn) {
      if (!inputEl || !applyBtn) return;
      const dirty = configFieldIsDirty(inputEl);
      applyBtn.disabled = !dirty;
      applyBtn.classList.toggle('is-dirty', dirty);
      applyBtn.classList.remove('is-pending');
      applyBtn.title = dirty ? 'Appliquer ce changement' : 'Aucun changement a appliquer';
      applyBtn.setAttribute('aria-label', applyBtn.title);
      const row = inputEl.closest('.control-row');
      if (row) row.classList.toggle('is-dirty', dirty);
    }

    function buildFlowCfgSingleFieldPatchJson(moduleName, inputEl) {
      if (!moduleName || !inputEl) throw new Error('champ non disponible');
      const key = String(inputEl.dataset.key || '').trim();
      if (!key) throw new Error('cle de configuration absente');
      const patch = {};
      patch[moduleName] = {
        [key]: readConfigFieldValue(inputEl)
      };
      return JSON.stringify(patch);
    }

    function renderConfigFields(containerEl, moduleName, dataObj) {
      containerEl.innerHTML = '';
      const data = (dataObj && typeof dataObj === 'object') ? dataObj : {};
      const perFieldApply = (containerEl === flowCfgFields) && flowCfgApplyPerFieldEnabled(moduleName);
      if (containerEl === flowCfgFields) {
        flowCfgApplyBtn.hidden = perFieldApply;
      }
      const keys = Object.keys(data).sort();
      if (keys.length === 0) {
        const row = document.createElement('div');
        row.className = 'control-row';
        const label = document.createElement('span');
        label.className = 'control-label';
        label.textContent = 'Aucun champ configurable dans cette branche.';
        row.appendChild(label);
        containerEl.appendChild(row);
        return;
      }

      for (const key of keys) {
        const value = data[key];
        const row = document.createElement('div');
        row.className = 'control-row';

        const doc = configDocFor(moduleName, key, []);
        const labelWrap = document.createElement('div');
        labelWrap.className = 'control-label-wrap';
        const label = document.createElement('span');
        label.className = 'control-label';
        label.textContent = (doc && typeof doc.label === 'string' && doc.label.length > 0) ? doc.label : key;
        labelWrap.appendChild(label);

        const helpTxt = (doc && typeof doc.help === 'string') ? doc.help : '';
        if (helpTxt.length > 0) {
          const help = document.createElement('span');
          help.className = 'control-help';
          help.textContent = helpTxt;
          labelWrap.appendChild(help);
        }
        row.appendChild(labelWrap);

        const enumOptions = (doc && Array.isArray(doc._enumOptions)) ? doc._enumOptions : null;
        let inputEl = null;
        const valueWrap = document.createElement('div');
        valueWrap.className = 'control-value-wrap';

        if (typeof value === 'boolean') {
          row.classList.add('control-row-bool');
          const sw = document.createElement('label');
          sw.className = 'md3-switch';
          const input = document.createElement('input');
          input.type = 'checkbox';
          input.checked = value;
          input.dataset.key = key;
          input.dataset.kind = 'bool';
          input.setAttribute('aria-label', label.textContent || key);
          const track = document.createElement('span');
          track.className = 'md3-track';
          const thumb = document.createElement('span');
          thumb.className = 'md3-thumb';
          storeConfigFieldInitialValue(input, value);
          sw.appendChild(input);
          sw.appendChild(track);
          sw.appendChild(thumb);
          inputEl = input;
          valueWrap.classList.add('control-value-wrap-bool');
          valueWrap.appendChild(sw);
        } else if (enumOptions && enumOptions.length > 0) {
          const select = document.createElement('select');
          select.className = 'control-input';
          select.dataset.key = key;
          select.dataset.kind = (typeof value === 'number' && Number.isInteger(value)) ? 'int' : (typeof value === 'number' ? 'float' : 'string');
          if (doc && typeof doc.display_format === 'string') {
            select.dataset.format = doc.display_format;
          }
          const currentValue = String(value);
          enumOptions.forEach((opt) => {
            if (!opt || typeof opt !== 'object') return;
            const optionEl = document.createElement('option');
            optionEl.value = String(opt.value);
            optionEl.textContent = (typeof opt.label === 'string' && opt.label.length > 0)
              ? opt.label
              : String(opt.value);
            if (optionEl.value === currentValue) {
              optionEl.selected = true;
            }
            select.appendChild(optionEl);
          });
          storeConfigFieldInitialValue(select, value);
          inputEl = select;
          valueWrap.appendChild(select);
        } else if (typeof value === 'number') {
          const input = document.createElement('input');
          input.className = 'control-input';
          const displayFormat = (doc && typeof doc.display_format === 'string') ? doc.display_format : '';
          input.type = displayFormat === 'hex' ? 'text' : 'number';
          input.value = formatConfigValueForDisplay(value, displayFormat);
          if (displayFormat !== 'hex') {
            input.step = Number.isInteger(value) ? '1' : '0.001';
          }
          input.dataset.key = key;
          input.dataset.kind = Number.isInteger(value) ? 'int' : 'float';
          if (displayFormat) {
            input.dataset.format = displayFormat;
          }
          storeConfigFieldInitialValue(input, value);
          inputEl = input;
          valueWrap.appendChild(input);
        } else {
          const isSecret = /pass|token|secret/i.test(key);
          const textValue = String(value ?? '');
          const input = document.createElement('input');
          input.className = 'control-input';
          input.type = isSecret ? 'password' : 'text';
          if (isSecret && textValue === '***') {
            input.value = '';
            input.placeholder = 'Conserver (masqué)';
            input.dataset.masked = '1';
          } else {
            input.value = textValue;
            input.dataset.masked = '0';
          }
          input.dataset.key = key;
          input.dataset.kind = 'string';
          storeConfigFieldInitialValue(input, value);
          inputEl = input;
          valueWrap.appendChild(input);
        }

        if (perFieldApply && inputEl) {
          const applyBtn = document.createElement('button');
          applyBtn.type = 'button';
          applyBtn.className = 'control-field-apply';
          applyBtn.innerHTML = fieldApplyCheckIcon;
          applyBtn.disabled = true;
          applyBtn.title = 'Aucun changement a appliquer';
          applyBtn.setAttribute('aria-label', applyBtn.title);
          applyBtn.addEventListener('click', async () => {
            await appliquerFlowCfgField(inputEl, applyBtn);
          });

          const syncApplyState = () => updateControlFieldApplyState(inputEl, applyBtn);
          inputEl.addEventListener('input', syncApplyState);
          inputEl.addEventListener('change', syncApplyState);
          updateControlFieldApplyState(inputEl, applyBtn);
          valueWrap.appendChild(applyBtn);
        }

        row.appendChild(valueWrap);
        containerEl.appendChild(row);
      }
    }

    function renderFlowCfgFields(dataObj) {
      renderConfigFields(flowCfgFields, flowCfgCurrentModule, dataObj);
    }

    function renderSupervisorCfgFields(dataObj) {
      renderConfigFields(supCfgFields, supCfgCurrentModule, dataObj);
    }

    function buildPatchJsonFromFields(fieldsContainer, moduleName) {
      if (!moduleName) throw new Error('branche non sélectionnée');
      const patch = {};
      const modulePatch = {};
      const fields = fieldsContainer.querySelectorAll('[data-key]');
      fields.forEach((el) => {
        const key = el.dataset.key;
        const kind = el.dataset.kind;
        const displayFormat = String(el.dataset.format || '').trim();
        if (!key || !kind) return;
        if (kind === 'bool') {
          modulePatch[key] = !!el.checked;
          return;
        }
        if (kind === 'int') {
          modulePatch[key] = parseConfigNumericValue(el.value, kind, displayFormat);
          return;
        }
        if (kind === 'float') {
          modulePatch[key] = parseConfigNumericValue(el.value, kind, displayFormat);
          return;
        }
        const masked = el.dataset.masked === '1';
        const raw = String(el.value ?? '');
        if (masked && raw.length === 0) return;
        modulePatch[key] = raw;
      });
      patch[moduleName] = modulePatch;
      return JSON.stringify(patch);
    }

    function buildFlowCfgPatchJson() {
      return buildPatchJsonFromFields(flowCfgFields, flowCfgCurrentModule);
    }

    function buildSupervisorCfgPatchJson() {
      return buildPatchJsonFromFields(supCfgFields, supCfgCurrentModule);
    }

    async function chargerFlowCfgModule(moduleName) {
      beginFlowCfgLoading('Chargement de la branche distante...');
      const m = nettoyerNomFlowCfg(moduleName);
      try {
        if (!m) {
          renderFlowCfgTitle(cheminFlowCfgCourant());
          resetFlowCfgEditor('Aucune branche sélectionnée.');
          return;
        }
        const res = await fetchFlowRemoteQueued(
          '/api/flowcfg/module?name=' + encodeURIComponent(m),
          { cache: 'no-store' }
        );
        const data = await res.json();
        if (!res.ok || !data || data.ok !== true || typeof data.data !== 'object') {
          throw new Error('lecture module impossible');
        }
        flowCfgCurrentModule = m;
        flowCfgCurrentData = data.data;
        renderFlowCfgTitle(m);
        renderFlowCfgFields(flowCfgCurrentData);
        flowCfgApplyBtn.disabled = flowCfgApplyBtn.hidden;
        flowCfgStatus.textContent = data.truncated
          ? 'Branche chargée (tronquée, buffer distant atteint).'
          : 'Branche chargée.';
      } catch (err) {
        renderFlowCfgTitle(cheminFlowCfgCourant());
        resetFlowCfgEditor('Chargement branche échoué: ' + err);
      } finally {
        endFlowCfgLoading();
      }
    }

    async function renderFlowCfgNavigator(forceReloadCurrent) {
      beginFlowCfgLoading('Chargement de la configuration distante...');
      try {
        const currentPath = cheminFlowCfgCourant();
        const node = await chargerFlowCfgChildren(currentPath, !!forceReloadCurrent);
        renderFlowCfgTitle(currentPath);
        renderFlowCfgSections(node, currentPath);
        if (node.hasExact) {
          await chargerFlowCfgModule(currentPath);
          return;
        }
        if (node.children.length > 0) {
          resetFlowCfgEditor('Sélectionnez une section.');
        } else {
          resetFlowCfgEditor('Aucune sous-branche disponible.');
        }
      } finally {
        endFlowCfgLoading();
      }
    }

    async function chargerFlowCfgModules(forceReload) {
      const force = !!forceReload;
      beginFlowCfgLoading('Chargement de la configuration distante...');
      try {
        if (force) {
          flowCfgChildrenCache = {};
        }
        try {
          await chargerFlowCfgChildren(cheminFlowCfgCourant(), force);
        } catch (err) {
          flowCfgPath = [];
          await chargerFlowCfgChildren('', force);
        }
        await renderFlowCfgNavigator(force);
        return true;
      } catch (err) {
        flowCfgStatus.textContent = 'Chargement des branches échoué: ' + err;
        return false;
      } finally {
        endFlowCfgLoading();
      }
    }

    async function ensureFlowCfgLoaded(forceReload) {
      const force = !!forceReload;
      if (flowCfgLoadPromise) {
        await flowCfgLoadPromise;
        if (!force) {
          return;
        }
      }

      flowCfgLoadPromise = (async () => {
        if (!flowCfgDocsLoaded) {
          await chargerFlowCfgDocs();
        }

        const wasLoaded = flowCfgLoadedOnce;
        const retryDelaysMs = (wasLoaded && !force) ? [0] : [0, 900, 2200, 3600];
        for (let attempt = 0; attempt < retryDelaysMs.length; ++attempt) {
          if (retryDelaysMs[attempt] > 0) {
            await waitMs(retryDelaysMs[attempt]);
          }
          const loaded = await chargerFlowCfgModules(force || attempt > 0);
          if (loaded) {
            flowCfgLoadedOnce = true;
            stopFlowCfgRetry();
            return;
          }
          if (attempt + 1 < retryDelaysMs.length) {
            flowCfgStatus.textContent = 'Flow.IO se prépare... nouvelle tentative.';
          }
        }

        if (isPageActive('page-control')) {
          flowCfgStatus.textContent = 'Flow.IO indisponible pour le moment. Nouvelle tentative automatique...';
          scheduleFlowCfgRetry(2500);
        }
      })();

      try {
        await flowCfgLoadPromise;
      } finally {
        flowCfgLoadPromise = null;
      }
    }

    function formatFlowCfgApplyError(data) {
      const err = (data && typeof data === 'object' && data.err && typeof data.err === 'object') ? data.err : {};
      const code = typeof err.code === 'string' ? err.code : '';
      const where = typeof err.where === 'string' ? err.where : '';

      if (code === 'ArgsTooLarge' || code === 'CfgTruncated') {
        return 'Trop de changements en une seule fois pour le lien I2C (' + (where || 'flowcfg') + ').';
      }
      if (code === 'NotReady') {
        return 'Lien I2C temporairement indisponible (' + (where || 'flowcfg') + ').';
      }
      if (code === 'IoError') {
        return 'Erreur de communication I2C (' + (where || 'flowcfg') + ').';
      }
      if (code === 'BadCfgJson') {
        return 'Patch de configuration invalide (' + (where || 'flowcfg') + ').';
      }
      if (code === 'CfgApplyFailed') {
        return 'Flow.IO a refusé la configuration (' + (where || 'flowcfg') + ').';
      }
      if (code) {
        return code + (where ? ' (' + where + ')' : '');
      }
      return 'apply refusé';
    }

    async function appliquerFlowCfgField(inputEl, applyBtn) {
      if (!inputEl || !applyBtn || !flowCfgCurrentModule) return;
      const key = String(inputEl.dataset.key || '').trim();
      if (!key) return;
      if (!configFieldIsDirty(inputEl)) {
        updateControlFieldApplyState(inputEl, applyBtn);
        return;
      }

      try {
        applyBtn.disabled = true;
        applyBtn.classList.add('is-pending');
        flowCfgStatus.textContent = 'Application du champ "' + key + '"...';

        const patch = buildFlowCfgSingleFieldPatchJson(flowCfgCurrentModule, inputEl);
        const body = new URLSearchParams();
        body.set('patch', patch);
        const res = await fetchFlowRemoteQueued('/api/flowcfg/apply', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
          body: body.toString()
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok || !data || data.ok !== true) {
          throw new Error(formatFlowCfgApplyError(data));
        }

        await chargerFlowCfgModule(flowCfgCurrentModule);
        flowCfgStatus.textContent = 'Champ "' + key + '" applique.';
      } catch (err) {
        flowCfgStatus.textContent = 'Application du champ echouee: ' + err;
        updateControlFieldApplyState(inputEl, applyBtn);
      }
    }

    async function appliquerFlowCfg() {
      try {
        const patch = buildFlowCfgPatchJson();
        const body = new URLSearchParams();
        body.set('patch', patch);
        const res = await fetchFlowRemoteQueued('/api/flowcfg/apply', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
          body: body.toString()
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok || !data || data.ok !== true) {
          throw new Error(formatFlowCfgApplyError(data));
        }
        flowCfgStatus.textContent = 'Configuration appliquée sur Flow.IO.';
        await chargerFlowCfgModule(flowCfgCurrentModule);
      } catch (err) {
        flowCfgStatus.textContent = 'Application cfg échouée: ' + err;
      }
    }

    function resetSupervisorCfgEditor(message) {
      supCfgCurrentModule = '';
      supCfgCurrentData = {};
      supCfgFields.innerHTML = '';
      supCfgApplyBtn.disabled = true;
      if (message) {
        supCfgStatus.textContent = message;
      }
    }

    async function chargerSupervisorCfgModule(moduleName) {
      const m = nettoyerNomFlowCfg(moduleName);
      if (!m) {
        resetSupervisorCfgEditor('Aucune branche locale sélectionnée.');
        return;
      }
      try {
        const res = await fetch('/api/supervisorcfg/module?name=' + encodeURIComponent(m), { cache: 'no-store' });
        const data = await res.json();
        if (!res.ok || !data || data.ok !== true || typeof data.data !== 'object') {
          throw new Error('lecture module impossible');
        }
        supCfgCurrentModule = m;
        supCfgCurrentData = data.data;
        renderSupervisorCfgFields(supCfgCurrentData);
        supCfgApplyBtn.disabled = false;
        supCfgStatus.textContent = data.truncated
          ? 'Branche locale chargée (tronquée, buffer atteint).'
          : 'Branche locale chargée.';
      } catch (err) {
        resetSupervisorCfgEditor('Chargement branche locale échoué: ' + err);
      }
    }

    async function chargerSupervisorCfgModules(keepCurrentSelection) {
      try {
        const res = await fetch('/api/supervisorcfg/modules', { cache: 'no-store' });
        const data = await res.json();
        if (!res.ok || !data || data.ok !== true || !Array.isArray(data.modules)) {
          throw new Error('liste modules indisponible');
        }

        const modules = data.modules
          .filter((name) => typeof name === 'string' && name.length > 0)
          .map((name) => nettoyerNomFlowCfg(name))
          .filter((name) => name.length > 0)
          .filter((name) => !isConfigPathHidden(name))
          .sort((a, b) => a.localeCompare(b));

        supCfgModuleSelect.innerHTML = '';
        if (modules.length === 0) {
          const empty = document.createElement('option');
          empty.value = '';
          empty.textContent = 'Aucune branche locale';
          supCfgModuleSelect.appendChild(empty);
          resetSupervisorCfgEditor('Aucune branche locale disponible.');
          return;
        }

        modules.forEach((name) => {
          const opt = document.createElement('option');
          opt.value = name;
          opt.textContent = name;
          supCfgModuleSelect.appendChild(opt);
        });

        const current = keepCurrentSelection ? nettoyerNomFlowCfg(supCfgCurrentModule) : '';
        const selected = modules.includes(current) ? current : modules[0];
        supCfgModuleSelect.value = selected;
        await chargerSupervisorCfgModule(selected);
      } catch (err) {
        resetSupervisorCfgEditor('Chargement des branches locales échoué: ' + err);
      }
    }

    async function appliquerSupervisorCfg() {
      try {
        const patch = buildSupervisorCfgPatchJson();
        const body = new URLSearchParams();
        body.set('patch', patch);
        const res = await fetch('/api/supervisorcfg/apply', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
          body: body.toString()
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok || !data || data.ok !== true) {
          throw new Error('apply refusé');
        }
        supCfgStatus.textContent = 'Configuration locale appliquée.';
        await chargerSupervisorCfgModule(supCfgCurrentModule);
      } catch (err) {
        supCfgStatus.textContent = 'Application cfg locale échouée: ' + err;
      }
    }

    async function callSystemAction(target, action) {
      let endpoint = '/api/system/reboot';
      if (target === 'flow' && action === 'reboot') endpoint = '/api/flow/system/reboot';
      else if (target === 'flow' && action === 'factory_reset') endpoint = '/api/flow/system/factory-reset';
      else if (target === 'supervisor' && action === 'factory_reset') endpoint = '/api/system/factory-reset';
      await fetchOkJson(endpoint, { method: 'POST' }, 'échec action', target === 'flow' ? fetchFlowRemoteQueued : fetch);
      if (target === 'flow' && action === 'factory_reset') {
        systemStatusText.textContent = 'Réinitialisation usine de Flow.IO lancée. Redémarrage en cours...';
      } else if (target === 'flow' && action === 'reboot') {
        systemStatusText.textContent = 'Redémarrage de Flow.IO lancé...';
      } else if (target === 'supervisor' && action === 'factory_reset') {
        systemStatusText.textContent = 'Réinitialisation usine du Superviseur lancée. Redémarrage en cours...';
      } else {
        systemStatusText.textContent = 'Redémarrage du Superviseur lancé...';
      }
    }

    function initUpgradeBindings() {
      bindClickAction(saveCfgBtn, async () => {
        try {
          await saveUpgradeConfig();
        } catch (err) {
          setUpgradeMessage('Échec de l\'enregistrement : ' + err);
        }
      });
      bindClickAction(upSupervisorBtn, () => startUpgrade('supervisor'));
      bindClickAction(upFlowBtn, () => startUpgrade('flowio'));
      bindClickAction(upNextionBtn, () => startUpgrade('nextion'));
      bindClickAction(upSpiffsBtn, () => startUpgrade('spiffs'));
      bindClickAction(refreshStateBtn, () => refreshUpgradeStatus());
    }

    function initStatusBindings() {
      bindClickAction(flowStatusRefreshBtn, async () => {
        try {
          await refreshFlowStatus(true);
        } catch (err) {
          flowStatusChip.textContent = 'erreur lecture statut';
        }
      });
      bindClickAction(poolMeasuresRefreshBtn, async () => {
        try {
          await refreshPoolMeasures(true);
        } catch (err) {
          showPoolMeasuresError(err);
        }
      });
    }

    function initWifiBindings() {
      if (toggleWifiPassBtn && wifiPass) {
        mettreAJourEtatVisibiliteMotDePasse(
          wifiPass,
          toggleWifiPassBtn,
          'Afficher le mot de passe WiFi',
          'Masquer le mot de passe WiFi'
        );
        toggleWifiPassBtn.addEventListener('click', () => {
          basculerVisibiliteMotDePasse(
            wifiPass,
            toggleWifiPassBtn,
            'Afficher le mot de passe WiFi',
            'Masquer le mot de passe WiFi'
          );
        });
      }
      wifiSsidList.addEventListener('change', () => {
        const picked = (wifiSsidList.value || '').trim();
        if (picked.length > 0) {
          wifiSsid.value = picked;
        }
      });
      bindClickAction(scanWifiBtn, () => refreshWifiScanStatus(true));
      bindClickAction(applyWifiCfgBtn, async () => {
        try {
          await saveWifiConfig();
        } catch (err) {
          wifiConfigStatus.textContent = 'Application WiFi échouée: ' + err;
        }
      });
    }

    function initSystemBindings() {
      bindClickAction(rebootSupervisorBtn, async () => {
        try {
          await callSystemAction('supervisor', 'reboot');
        } catch (err) {
          systemStatusText.textContent = 'Redémarrage du Superviseur échoué : ' + err;
        }
      });
      bindClickAction(rebootFlowBtn, async () => {
        try {
          await callSystemAction('flow', 'reboot');
        } catch (err) {
          systemStatusText.textContent = 'Redémarrage de Flow.IO échoué : ' + err;
        }
      });
      bindClickAction(flowFactoryResetBtn, async () => {
        if (!confirm('Confirmer la réinitialisation usine de Flow.IO ? Cette action efface la configuration distante.')) return;
        try {
          await callSystemAction('flow', 'factory_reset');
        } catch (err) {
          systemStatusText.textContent = 'Réinitialisation usine de Flow.IO échouée : ' + err;
        }
      });
    }

    function initConfigBindings() {
      bindClickAction(flowCfgRefreshBtn, () => ensureFlowCfgLoaded(true));
      bindClickAction(flowCfgApplyBtn, () => appliquerFlowCfg());
      bindClickAction(supCfgRefreshBtn, () => chargerSupervisorCfgModules(true));
      supCfgModuleSelect.addEventListener('change', async () => {
        const selected = nettoyerNomFlowCfg(supCfgModuleSelect.value);
        await chargerSupervisorCfgModule(selected);
      });
      bindClickAction(supCfgApplyBtn, () => appliquerSupervisorCfg());
    }

    function initGlobalUiBindings() {
      document.addEventListener('click', (event) => {
        if (!flowCfgTitle.contains(event.target)) {
          closeFlowCfgCrumbMenus();
        }
      });
      document.addEventListener('keydown', (event) => {
        if (event.key === 'Escape') {
          closeFlowCfgCrumbMenus();
        }
      });
      document.addEventListener('visibilitychange', () => {
        const activePageId = getActivePageId();
        const onUpgradePage = activePageId === 'page-system';
        const onTerminalPage = activePageId === 'page-terminal';
        if (document.hidden || !onUpgradePage) {
          stopUpgradeStatusPolling();
        } else {
          startUpgradeStatusPolling();
        }
        if (document.hidden || activePageId !== 'page-pool-measures') {
          stopPoolMeasuresTimer();
        } else {
          startPoolMeasuresTimer();
        }
        if (document.hidden || !onTerminalPage) {
          closeLogSocket();
          setWsStatusText('inactif');
        } else if (terminalActive) {
          connectLogSocket();
        }
      });
    }

    initUpgradeBindings();
    initStatusBindings();
    initWifiBindings();
    initSystemBindings();
    initConfigBindings();
    initGlobalUiBindings();

    setUpgradeProgress(0);
    startDrawerRuntimeTimer();
    const initialPageId = resolveInitialPageId();
    const startInitialUi = () => {
      showPage(initialPageId, { deferHeavyMs: 260 });
      setTimeout(() => {
        loadWebMeta().catch(() => {});
      }, 60);
    };
    if (typeof window.requestAnimationFrame === 'function') {
      window.requestAnimationFrame(startInitialUi);
    } else {
      setTimeout(startInitialUi, 16);
    }
    scheduleDeferredVisualAssets();
