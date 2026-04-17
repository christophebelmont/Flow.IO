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
    const upgradeUiSessionStorageKey = 'flow_upgrade_ui_session';
    const upgradeStatusPollActiveMs = 900;
    const upgradeStatusPollReconnectMs = 5000;
    const upgradeStatusPollDoneMs = 7000;
    const upgradeStatusPollIdleMs = 15000;
    const upgradeStatusPollErrorMs = 10000;
    const rebootActionDelaySeconds = 5;
    const deferredMenuAssetStartDelayMs = 520;
    const deferredMenuAssetStepMs = 140;
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
    let menuAssetsActivated = false;
    let deferredMenuAssetsArmed = false;
    const pendingSystemActionCountdowns = new Map();
    let activeColorPickerPopover = null;

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

    function assetUrl(path) {
      if (!webAssetVersion) return path;
      return path + '?v=' + encodeURIComponent(webAssetVersion);
    }

    function iconAssetUrl(iconName) {
      return assetUrl('/webinterface/i/' + iconName + '.svg');
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

    function extractApiErrorMessage(data, fallback) {
      if (!data || typeof data !== 'object') return fallback;
      const err = data.err;
      if (!err || typeof err !== 'object') return fallback;

      const msg = typeof err.msg === 'string' ? err.msg.trim() : '';
      const code = typeof err.code === 'string' ? err.code.trim() : '';
      const where = typeof err.where === 'string' ? err.where.trim() : '';
      const detail = msg || [code, where].filter(Boolean).join(' @ ');
      if (!detail) return fallback;
      return fallback ? (fallback + ' : ' + detail) : detail;
    }

    function ensureOkJsonResponse(response, message) {
      if (!response.res.ok || !response.data || response.data.ok !== true) {
        throw new Error(extractApiErrorMessage(response.data, message));
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

    function utf8ByteLength(value) {
      const text = String(value || '');
      if (typeof TextEncoder === 'function') {
        return new TextEncoder().encode(text).length;
      }
      return text.length;
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

    function setDeferredVisualAssetUrl(varName, url) {
      document.documentElement.style.setProperty(varName, "url('" + url + "')");
    }

    function setDeferredFaviconUrl(url) {
      if (!url) return;
      const resolvedUrl = url.indexOf('?') >= 0 ? (url + '&slot=favicon') : (url + '?slot=favicon');
      let link = document.getElementById('appFavicon');
      if (!link) {
        link = document.createElement('link');
        link.id = 'appFavicon';
        document.head.appendChild(link);
      }
      link.setAttribute('data-flow-favicon', '1');
      link.rel = 'icon';
      link.type = 'image/svg+xml';
      link.sizes = 'any';
      if (link.href !== resolvedUrl) {
        link.href = resolvedUrl;
      }

      let shortcutLink = document.getElementById('appFaviconShortcut');
      if (!shortcutLink) {
        shortcutLink = document.createElement('link');
        shortcutLink.id = 'appFaviconShortcut';
        document.head.appendChild(shortcutLink);
      }
      shortcutLink.setAttribute('data-flow-favicon', '1');
      shortcutLink.rel = 'shortcut icon';
      shortcutLink.type = 'image/svg+xml';
      shortcutLink.sizes = 'any';
      if (shortcutLink.href !== resolvedUrl) {
        shortcutLink.href = resolvedUrl;
      }
    }

    function applyDeferredVisualAsset(entry) {
      if (!entry || !entry[0]) return;
      if (entry[0] === 'favicon') {
        setDeferredFaviconUrl(entry[1]);
        return;
      }
      setDeferredVisualAssetUrl(entry[0], entry[1]);
    }

    function activateMenuAssets(deferred, stepDelayMs) {
      if (menuAssetsActivated) return;
      menuAssetsActivated = true;
      const stepMs = Math.max(0, Number(stepDelayMs) || deferredMenuAssetStepMs);
      const steps = [];
      if (!hideMenuSvg) {
        steps.push(
          ['--menu-icon-measures-url', iconAssetUrl('m')],
          ['--menu-icon-terminal-url', iconAssetUrl('t')],
          ['--menu-icon-system-url', iconAssetUrl('d')],
          ['--menu-icon-flowcfg-url', iconAssetUrl('s')]
        );
      }
      steps.push(
        ['--ui-refresh-icon-url', iconAssetUrl('r')],
        ['--ui-crumb-arrow-icon-url', iconAssetUrl('u')],
        ['favicon', iconAssetUrl('f')]
      );
      if (!deferred) {
        steps.forEach((entry) => {
          applyDeferredVisualAsset(entry);
        });
        markDeferredVisualAssetsWarm();
        return;
      }
      steps.forEach((entry, index) => {
        setTimeout(() => {
          applyDeferredVisualAsset(entry);
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
        activateMenuAssets(false);
        return;
      }

      const isReload = isReloadNavigation();
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

    async function loadWebMeta(options) {
      const opts = options || {};
      try {
        const data = await fetchOkJson('/api/web/meta', { cache: 'no-store' }, 'meta web indisponible');
        const currentUpgradeSession = readUpgradeUiSession();
        if (currentUpgradeSession && currentUpgradeSession.awaitingReconnect) {
          handleUpgradeReconnectSuccess();
        }

        if (typeof data.web_asset_version === 'string') {
          const announcedVersion = data.web_asset_version.trim();
          if (announcedVersion) {
            const previousVersion = (webAssetVersion || '').trim();
            webAssetVersion = announcedVersion;
            if (previousVersion && previousVersion !== announcedVersion) {
              runtimeManifestDomainCache = null;
              runtimeManifestDomainLoadPromise = null;
            }
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
        if (!opts.skipDrawerRuntimeRender) {
          renderDrawerRuntimeMeta();
        }
        if (isPageActive('page-status')) {
          refreshFlowStatus(false).catch(() => {});
        }
      } catch (err) {
      }
    }

    function isMobileLayout() {
      return window.innerWidth <= 900;
    }

    function isDrawerExpanded() {
      return isMobileLayout()
        ? drawer.classList.contains('mobile-open')
        : !drawer.classList.contains('collapsed');
    }

    function setMobileDrawerOpen(open) {
      drawer.classList.toggle('mobile-open', open);
      overlay.classList.toggle('visible', open);
      if (open) {
        refreshDrawerRuntimeMeta(true).catch(() => {});
      }
    }

    function closeMobileDrawer() {
      if (isMobileLayout()) {
        setMobileDrawerOpen(false);
      }
    }

    function currentUpgradeStatusPollDelayMs() {
      const current = readUpgradeUiSession();
      const phase = String(current && current.phase ? current.phase : 'idle');
      if (current && (current.awaitingReconnect || phase === 'reboot' || phase === 'reconnect')) {
        return upgradeStatusPollReconnectMs;
      }
      if (phase === 'target' || phase === 'download' || phase === 'flash') {
        return upgradeStatusPollActiveMs;
      }
      if (phase === 'done') return upgradeStatusPollDoneMs;
      if (phase === 'error') return upgradeStatusPollErrorMs;
      return upgradeStatusPollIdleMs;
    }

    function scheduleNextUpgradeStatusPoll(delayMs) {
      if (document.hidden || getActivePageId() !== 'page-system') return;
      const nextDelay = Math.max(0, Number.isFinite(delayMs) ? delayMs : currentUpgradeStatusPollDelayMs());
      upgradeStatusPoller.schedule(nextDelay);
    }

    async function pollUpgradeStatusTick() {
      if (document.hidden || getActivePageId() !== 'page-system') return;
      await refreshUpgradeStatus();
      scheduleNextUpgradeStatusPoll();
    }

    function startUpgradeStatusPolling(immediate) {
      if (immediate) {
        scheduleNextUpgradeStatusPoll(0);
        return;
      }
      scheduleNextUpgradeStatusPoll();
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
        schedulePageTask(pageId,
                         pageToken,
                         deferredHeavyMs > 0 ? (deferredHeavyMs + 180) : 0,
                         () => onUpgradePageShown());
      }
      if (pageId === 'page-wifi') {
        schedulePageTask(pageId,
                         pageToken,
                         deferredHeavyMs > 0 ? (deferredHeavyMs + 180) : 0,
                         () => onWifiPageShown());
      }
      if (pageId === 'page-control') {
        schedulePageTask(pageId,
                         pageToken,
                         deferredHeavyMs > 0 ? (deferredHeavyMs + 220) : 0,
                         () => onControlPageShown());
      } else {
        stopFlowCfgRetry();
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
        refreshDrawerRuntimeMeta(isDrawerExpanded()).catch(() => {});
      }
    }));

    overlay.addEventListener('click', closeMobileDrawer);
    window.addEventListener('resize', () => {
      if (!isMobileLayout()) {
        setMobileDrawerOpen(false);
      }
      refreshDrawerRuntimeMeta(isDrawerExpanded()).catch(() => {});
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
    const applyUpdateHostBtn = document.getElementById('applyUpdateHost');
    const applyFlowPathBtn = document.getElementById('applyFlowPath');
    const applyNextionPathBtn = document.getElementById('applyNextionPath');
    const applySupervisorPathBtn = document.getElementById('applySupervisorPath');
    const applySpiffsPathBtn = document.getElementById('applySpiffsPath');
    const upSupervisorBtn = document.getElementById('upSupervisor');
    const upFlowBtn = document.getElementById('upFlow');
    const upNextionBtn = document.getElementById('upNextion');
    const upSpiffsBtn = document.getElementById('upSpiffs');
    const upgradeProgressBar = document.getElementById('upgradeProgressBar');
    const upgradePct = document.getElementById('upgradePct');
    const upgradeJourneyLabel = document.getElementById('upgradeJourneyLabel');
    const upgradeSteps = document.getElementById('upgradeSteps');
    const upgradeFooterStatus = document.getElementById('upgradeFooterStatus');
    const upgradeEta = document.getElementById('upgradeEta');
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
    const rebootNextionBtn = document.getElementById('rebootNextion');
    const flowFactoryResetBtn = document.getElementById('flowFactoryReset');
    const systemStatusText = document.getElementById('systemStatusText');
    const flowStatusRefreshBtn = document.getElementById('flowStatusRefresh');
    const flowStatusChip = document.getElementById('flowStatusChip');
    const flowStatusGrid = document.getElementById('flowStatusGrid');
    const flowStatusRaw = document.getElementById('flowStatusRaw');
    const poolMeasuresRefreshBtn = document.getElementById('poolMeasuresRefresh');
    const poolMeasuresDomains = document.getElementById('poolMeasuresDomains');
    const poolMeasuresStatus = document.getElementById('poolMeasuresStatus');
    const poolMeasuresGrid = document.getElementById('poolMeasuresGrid');
    const flowCfgRefreshBtn = document.getElementById('flowCfgRefresh');
    const flowCfgExportBtn = document.getElementById('flowCfgExport');
    const flowCfgImportBtn = document.getElementById('flowCfgImport');
    const flowCfgImportFileInput = document.getElementById('flowCfgImportFile');
    const flowCfgApplyBtn = document.getElementById('flowCfgApply');
    const flowCfgTree = document.getElementById('flowCfgTree');
    const flowCfgPathLabel = document.getElementById('flowCfgCurrentPath');
    const flowCfgPathMeta = document.getElementById('flowCfgPathMeta');
    const flowCfgFields = document.getElementById('flowCfgFields');
    const flowCfgStatus = document.getElementById('flowCfgStatus');
    const flowCfgBackupStatus = document.getElementById('flowCfgBackupStatus');
    const flowCfgTreePane = flowCfgTree ? flowCfgTree.closest('.cfg-pane') : null;
    const flowCfgDetailPane = flowCfgFields ? flowCfgFields.closest('.cfg-pane') : null;
    let flowCfgCurrentModule = '';
    let flowCfgCurrentData = {};
    let flowCfgChildrenCache = {};
    let flowCfgPath = [];
    let flowCfgExpandedNodes = new Set();
    let flowCfgRootExpanded = true;
    let cfgTreeSelectedSource = 'flow';
    let cfgDocSources = [];
    let flowCfgDocsLoaded = false;
    let flowCfgTreeLoadingDepth = 0;
    let flowCfgDetailLoadingDepth = 0;
    let flowCfgLoadPromise = null;
    let flowCfgRetryTimer = null;
    let flowCfgFlowOnlyFailureStreak = 0;
    let upgradeCfgLoadedOnce = false;
    let wifiConfigLoadedOnce = false;
    let flowCfgLoadedOnce = false;
    let cfgTreeAliases = [];
    let cfgTreeVirtualBranches = [];
    let supCfgCurrentModule = '';
    let supCfgCurrentData = {};
    let supCfgTreePath = '';
    let supCfgChildrenCache = {};
    let supCfgExpandedNodes = new Set();
    let supCfgRootExpanded = true;
    let wifiScanAutoRequested = false;
    let flowStatusReqSeq = 0;
    const fieldApplyCheckIcon = '✓';
    const flowCfgBackupFormat = 'flowio-configstore-backup';
    const flowCfgBackupVersion = 1;
    const flowCfgBackupRedactedToken = '__REDACTED__';
    const flowCfgBackupPatchTargetBytes = 1300;
    let flowCfgBackupBusy = false;
    const flowStatusDomainTtlMs = 20000;
    const flowStatusDashboardRuntimeUiIds = Object.freeze({
      waterTemp: 2101,
      airTemp: 2102,
      ph: 2103,
      orp: 2104
    });
    const flowStatusDomainKeys = ['system', 'wifi', 'mqtt', 'pool', 'i2c'];
    const flowStatusDomainCache = {
      system: { data: null, fetchedAt: 0 },
      wifi: { data: null, fetchedAt: 0 },
      mqtt: { data: null, fetchedAt: 0 },
      pool: { data: null, fetchedAt: 0 },
      i2c: { data: null, fetchedAt: 0 }
    };
    const flowStatusDashboardSlotsCache = { data: [], fetchedAt: 0 };
    const runtimeMeasureDomainKeys = ['pool', 'mqtt', 'system', 'wifi', 'alarm'];
    let runtimeManifestDomainCache = null;
    let runtimeManifestDomainLoadPromise = null;
    const poolMeasureDomainState = {
      pool: { active: false, loading: false, entries: [], values: [], dashboardSlots: [], error: '', requestSeq: 0 },
      mqtt: { active: false, loading: false, entries: [], values: [], dashboardSlots: [], error: '', requestSeq: 0 },
      system: { active: false, loading: false, entries: [], values: [], dashboardSlots: [], error: '', requestSeq: 0 },
      wifi: { active: false, loading: false, entries: [], values: [], dashboardSlots: [], error: '', requestSeq: 0 },
      alarm: { active: false, loading: false, entries: [], values: [], dashboardSlots: [], error: '', requestSeq: 0 }
    };
    const poolMeasureDomainAnimations = {};
    const upgradeReconnectFetchTimeoutMs = 1400;
    const upgradeConfigFieldDefs = [
      { key: 'update_host', input: updateHost, button: applyUpdateHostBtn, successMessage: 'Serveur HTTP enregistré.' },
      { key: 'flowio_path', input: flowPath, button: applyFlowPathBtn, successMessage: 'Chemin Flow.io enregistré.' },
      { key: 'nextion_path', input: nextionPath, button: applyNextionPathBtn, successMessage: 'Chemin Nextion enregistré.' },
      { key: 'supervisor_path', input: supervisorPath, button: applySupervisorPathBtn, successMessage: 'Chemin Supervisor enregistré.' },
      { key: 'spiffs_path', input: spiffsPath, button: applySpiffsPathBtn, successMessage: 'Chemin SPIFFS enregistré.' }
    ];

    const wsProto = location.protocol === 'https:' ? 'wss' : 'ws';
    let logSource = 'flow';
    let logSocket = null;
    const upgradeStatusPoller = createTimeoutRunner(() => pollUpgradeStatusTick());
    const upgradeReconnectStageTimer = createTimeoutRunner(() => enterUpgradeReconnectPhase());
    const upgradeReconnectCompletionTimer = createTimeoutRunner(() => markUpgradeUiCompletedAfterReconnect());
    const upgradeReconnectMonitor = createIntervalRunner(() => probeUpgradeReconnect(), 1500);
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
      const sourceLabel = logSource === 'supervisor' ? 'Supervisor' : 'Flow.io';
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

    const iconeOeilOuvert = 'Cacher';
    const iconeOeilBarre = 'Voir';

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
    if (flowCfgApplyBtn) flowCfgApplyBtn.disabled = true;

    function setUpgradeProgress(value) {
      const p = Math.max(0, Math.min(100, Number(value) || 0));
      upgradeProgressBar.style.width = p + '%';
      upgradeProgressBar.classList.toggle('is-complete', p >= 100);
      if (upgradePct) {
        upgradePct.textContent = p + '%';
      }
    }

    function setUpgradeMessage(text) {
      const message = String(text || '').trim() || 'Aucune opération en cours.';
      if (upgradeFooterStatus) {
        upgradeFooterStatus.innerHTML = '<span class="sdot"></span>' + message;
      }
    }

    function readUpgradeUiSession() {
      const raw = getStorageValue(sessionStorage, upgradeUiSessionStorageKey);
      if (!raw) return null;
      try {
        const parsed = JSON.parse(raw);
        return parsed && typeof parsed === 'object' ? parsed : null;
      } catch (err) {
        return null;
      }
    }

    function writeUpgradeUiSession(session) {
      if (!session || typeof session !== 'object') return;
      setStorageValue(sessionStorage, upgradeUiSessionStorageKey, JSON.stringify(session));
    }

    function clearUpgradeUiSession() {
      stopUpgradeReconnectFlow();
      try {
        sessionStorage.removeItem(upgradeUiSessionStorageKey);
      } catch (err) {
      }
    }

    function upgradeTargetLabel(target) {
      const key = String(target || '').trim().toLowerCase();
      if (key === 'flowio') return 'Flow.io';
      if (key === 'supervisor') return 'Superviseur';
      if (key === 'nextion') return 'Nextion';
      if (key === 'spiffs') return 'SPIFFS';
      return 'Firmware';
    }

    function upgradeUsesReconnect(target) {
      const key = String(target || '').trim().toLowerCase();
      return key === 'supervisor' || key === 'spiffs';
    }

    function upgradeStepDefinitions(target) {
      return [
        { id: 'target', label: 'Sélection de la cible ' + upgradeTargetLabel(target) },
        { id: 'download', label: 'Téléchargement du firmware' },
        { id: 'flash', label: 'Mise à jour' },
        { id: 'reboot', label: 'Redémarrage' },
        { id: 'reconnect', label: 'Attente de Reconnection' },
        { id: 'done', label: 'Mise à jour terminée' }
      ];
    }

    function upgradePhaseIndex(phase) {
      if (phase === 'target') return 0;
      if (phase === 'download') return 1;
      if (phase === 'flash') return 2;
      if (phase === 'reboot') return 3;
      if (phase === 'reconnect') return 4;
      if (phase === 'done') return 5;
      return -1;
    }

    function upgradePhasePercent(session) {
      const phase = String(session && session.phase ? session.phase : 'idle');
      const progress = Math.max(0, Math.min(100, Number(session && session.backendProgress) || 0));
      const reconnectProgress = Math.max(0, Math.min(100, Number(session && session.reconnectProgress) || 0));
      if (phase === 'target') return 1;
      if (phase === 'download') return 1 + Math.round(progress * 0.04);
      if (phase === 'flash') return 5 + Math.round(progress * 0.90);
      if (phase === 'reboot') return 97;
      if (phase === 'reconnect') return 97 + Math.round(reconnectProgress * 0.03);
      if (phase === 'done') return 100;
      if (phase === 'error') return Math.max(6, Math.min(96, Number(session && session.lastPercent) || 12));
      return 0;
    }

    function upgradeStepProgress(stepId, state, session) {
      if (state === 'done') return 100;
      if (state !== 'active') return null;
      const phase = String(session && session.phase ? session.phase : '');
      if (stepId !== phase) return null;
      if (phase === 'target') return 100;
      if (phase === 'download' || phase === 'flash') {
        return Math.max(0, Math.min(100, Number(session && session.backendProgress) || 0));
      }
      if (phase === 'reboot') return 100;
      if (phase === 'reconnect') {
        return Math.max(0, Math.min(100, Number(session && session.reconnectProgress) || 0));
      }
      if (phase === 'done') return 100;
      return null;
    }

    function upgradeStepStatusLabel(stepId, state, session) {
      if (state === 'done') return 'OK';
      const progress = upgradeStepProgress(stepId, state, session);
      if (progress !== null) return progress + '%';
      if (state === 'error') return 'erreur';
      return '';
    }

    function upgradeStepState(stepId, session) {
      const phase = String(session && session.phase ? session.phase : 'idle');
      if (phase === 'idle') return 'pending';
      if (phase === 'error') {
        const failedStep = String(session && session.failedStep ? session.failedStep : 'flash');
        const failedIndex = upgradePhaseIndex(failedStep);
        const stepIndex = upgradePhaseIndex(stepId);
        if (stepIndex < failedIndex) return 'done';
        if (stepId === failedStep) return 'error';
        return 'pending';
      }
      const activeIndex = upgradePhaseIndex(phase);
      const stepIndex = upgradePhaseIndex(stepId);
      if (stepIndex < activeIndex) return 'done';
      if (stepIndex === activeIndex) return phase === 'done' ? 'done' : 'active';
      return 'pending';
    }

    function renderUpgradeSteps(session) {
      if (!upgradeSteps) return;
      const defs = upgradeStepDefinitions(session && session.target);
      upgradeSteps.innerHTML = '';
      defs.forEach((step) => {
        const state = upgradeStepState(step.id, session);
        const row = document.createElement('div');
        row.className = 'step-row';

        const icon = document.createElement('span');
        icon.className = 'step-ic ' + state;
        if (state === 'active') {
          const refreshIcon = document.createElement('span');
          refreshIcon.className = 'step-refresh-icon';
          refreshIcon.setAttribute('aria-hidden', 'true');
          icon.appendChild(refreshIcon);
        } else if (state === 'done') {
          icon.textContent = '✓';
        } else if (state === 'error') {
          icon.textContent = '!';
        } else {
          icon.textContent = '○';
        }
        row.appendChild(icon);

        const label = document.createElement('span');
        label.className = 'step-lbl ' + state;
        label.textContent = step.label;
        row.appendChild(label);

        const trailing = document.createElement('span');
        trailing.className = 'step-t';
        trailing.textContent = upgradeStepStatusLabel(step.id, state, session);
        row.appendChild(trailing);

        upgradeSteps.appendChild(row);
      });
    }

    function renderUpgradeJourney(session) {
      const safeSession = session && typeof session === 'object' ? session : { phase: 'idle', target: '' };
      const phase = String(safeSession.phase || 'idle');
      const detail = String(safeSession.detail || '');
      const targetLabel = upgradeTargetLabel(safeSession.target);
      const stateLabel = phase === 'idle'
        ? 'Prêt'
        : phase === 'target'
          ? 'Cible sélectionnée'
          : phase === 'download'
            ? 'Téléchargement'
            : phase === 'flash'
              ? 'Mise à jour'
              : phase === 'reboot'
                ? 'Redémarrage'
                : phase === 'reconnect'
                  ? 'Attente de Reconnection'
                  : phase === 'done'
                    ? 'Mise à jour terminée'
                    : 'Erreur';

      if (upgradeJourneyLabel) {
        upgradeJourneyLabel.textContent = safeSession.target ? ('Upgrade ' + targetLabel) : 'Upgrade firmware';
      }
      setUpgradeProgress(upgradePhasePercent(safeSession));
      setUpgradeMessage(detail || (phase === 'idle' ? 'Aucune opération en cours.' : stateLabel));
      renderUpgradeSteps(safeSession);
      if (upStatusChip) {
        upStatusChip.textContent = stateLabel;
      }
    }

    function updateUpgradeUiSession(patch) {
      const current = readUpgradeUiSession() || {
        phase: 'idle',
        target: '',
        detail: 'Aucune opération en cours.',
        backendProgress: 0,
        lastPercent: 0,
        awaitingReconnect: false,
        reconnectShown: false,
        reconnectProgress: 0
      };
      const next = Object.assign({}, current, patch || {});
      next.lastPercent = upgradePhasePercent(next);
      writeUpgradeUiSession(next);
      renderUpgradeJourney(next);
      return next;
    }

    function startUpgradeUiSession(target) {
      stopUpgradeReconnectFlow();
      return updateUpgradeUiSession({
        phase: 'target',
        target: target,
        detail: 'Sélection de la cible ' + upgradeTargetLabel(target) + '.',
        backendProgress: 0,
        awaitingReconnect: false,
        reconnectShown: false,
        reconnectProgress: 0,
        failedStep: ''
      });
    }

    function stopUpgradeReconnectFlow() {
      upgradeReconnectStageTimer.stop();
      upgradeReconnectCompletionTimer.stop();
      upgradeReconnectMonitor.stop();
    }

    function scheduleUpgradeReconnectPhase(delayMs) {
      upgradeReconnectStageTimer.schedule(Math.max(0, Number(delayMs) || 0));
    }

    function startUpgradeReconnectMonitor() {
      upgradeReconnectMonitor.start();
    }

    function scheduleUpgradeReconnectCompletion(delayMs) {
      upgradeReconnectCompletionTimer.schedule(Math.max(0, Number(delayMs) || 0));
    }

    function markUpgradeUiAwaitingReconnect() {
      const current = readUpgradeUiSession();
      if (!current || !current.awaitingReconnect) return null;
      return updateUpgradeUiSession({
        phase: 'reconnect',
        detail: 'Attente de Reconnection.',
        reconnectShown: true,
        reconnectProgress: Math.max(5, Math.min(95, Number(current.reconnectProgress) || 0))
      });
    }

    function markUpgradeUiCompletedAfterReconnect() {
      const current = readUpgradeUiSession();
      if (!current || !current.awaitingReconnect) return null;
      stopUpgradeReconnectFlow();
      return updateUpgradeUiSession({
        phase: 'done',
        detail: 'Mise à jour terminée.',
        backendProgress: 100,
        awaitingReconnect: false,
        reconnectShown: true,
        reconnectProgress: 100,
        failedStep: ''
      });
    }

    function handleUpgradeReconnectSuccess() {
      const current = readUpgradeUiSession();
      if (!current || !current.awaitingReconnect) return null;
      if (!current.reconnectShown || current.phase === 'reboot') {
        upgradeReconnectStageTimer.stop();
        upgradeReconnectMonitor.stop();
        markUpgradeUiAwaitingReconnect();
        scheduleUpgradeReconnectCompletion(320);
        return readUpgradeUiSession();
      }
      return markUpgradeUiCompletedAfterReconnect();
    }

    function incrementUpgradeReconnectProgress() {
      const current = readUpgradeUiSession();
      if (!current || !current.awaitingReconnect) return null;
      const nextProgress = Math.max(5, Math.min(95, (Number(current.reconnectProgress) || 0) + 12));
      return updateUpgradeUiSession({
        phase: 'reconnect',
        detail: 'Attente de Reconnection.',
        reconnectShown: true,
        reconnectProgress: nextProgress
      });
    }

    function enterUpgradeReconnectPhase() {
      const current = readUpgradeUiSession();
      if (!current || !current.awaitingReconnect) return null;
      markUpgradeUiAwaitingReconnect();
      startUpgradeReconnectMonitor();
      return readUpgradeUiSession();
    }

    async function fetchUpgradeReconnectHeartbeat() {
      const supportsAbort = typeof AbortController === 'function';
      const controller = supportsAbort ? new AbortController() : null;
      const timeoutId = controller
        ? setTimeout(() => {
            try {
              controller.abort();
            } catch (err) {
            }
          }, upgradeReconnectFetchTimeoutMs)
        : null;
      try {
        return await fetchOkJson('/api/web/meta', {
          cache: 'no-store',
          signal: controller ? controller.signal : undefined
        }, 'meta web indisponible');
      } finally {
        if (timeoutId) clearTimeout(timeoutId);
      }
    }

    async function probeUpgradeReconnect() {
      const current = readUpgradeUiSession();
      if (!current || !current.awaitingReconnect) {
        stopUpgradeReconnectFlow();
        return;
      }
      try {
        await fetchUpgradeReconnectHeartbeat();
        handleUpgradeReconnectSuccess();
      } catch (err) {
        incrementUpgradeReconnectProgress();
      }
    }

    function resumeUpgradeReconnectFlow() {
      const current = readUpgradeUiSession();
      if (!current || !current.awaitingReconnect) return;
      if (current.reconnectShown || current.phase === 'reconnect') {
        startUpgradeReconnectMonitor();
        return;
      }
      scheduleUpgradeReconnectPhase(700);
    }

    function updateUpgradeView(data) {
      if (!data || data.ok !== true) return;
      const current = readUpgradeUiSession();
      const state = String(data.state || 'idle');
      const target = String(data.target || (current && current.target) || '').trim().toLowerCase();
      const progress = Math.max(0, Math.min(100, Number(data.progress) || 0));
      const msg = String(data.msg || '').trim();

      if (state === 'idle') {
        if (current && current.awaitingReconnect) {
          handleUpgradeReconnectSuccess();
        } else if (!current || current.phase === 'idle') {
          clearUpgradeUiSession();
          renderUpgradeJourney({ phase: 'idle', target: '', detail: 'Aucune opération en cours.' });
        }
        return;
      }

      if (state === 'queued') {
        stopUpgradeReconnectFlow();
        updateUpgradeUiSession({
          phase: 'target',
          target: target,
          detail: 'Sélection de la cible ' + upgradeTargetLabel(target) + '.',
          backendProgress: progress,
          awaitingReconnect: false,
          reconnectShown: false,
          reconnectProgress: 0,
          failedStep: ''
        });
        return;
      }

      if (state === 'downloading') {
        stopUpgradeReconnectFlow();
        updateUpgradeUiSession({
          phase: 'download',
          target: target,
          detail: 'Téléchargement du firmware.',
          backendProgress: progress,
          awaitingReconnect: false,
          reconnectShown: false,
          reconnectProgress: 0,
          failedStep: ''
        });
        return;
      }

      if (state === 'flashing') {
        stopUpgradeReconnectFlow();
        updateUpgradeUiSession({
          phase: 'flash',
          target: target,
          detail: 'Mise à jour en cours.',
          backendProgress: progress,
          awaitingReconnect: false,
          reconnectShown: false,
          reconnectProgress: 0,
          failedStep: ''
        });
        return;
      }

      if (state === 'rebooting') {
        stopUpgradeReconnectFlow();
        updateUpgradeUiSession({
          phase: 'reboot',
          target: target,
          detail: 'Redémarrage.',
          backendProgress: 100,
          awaitingReconnect: true,
          reconnectShown: false,
          reconnectProgress: 0,
          failedStep: ''
        });
        scheduleUpgradeReconnectPhase(900);
        return;
      }

      if (state === 'done') {
        if (upgradeUsesReconnect(target)) {
          stopUpgradeReconnectFlow();
          updateUpgradeUiSession({
            phase: 'reboot',
            target: target,
            detail: 'Redémarrage.',
            backendProgress: 100,
            awaitingReconnect: true,
            reconnectShown: false,
            reconnectProgress: 0,
            failedStep: ''
          });
          scheduleUpgradeReconnectPhase(900);
        } else {
          stopUpgradeReconnectFlow();
          updateUpgradeUiSession({
            phase: 'done',
            target: target,
            detail: 'Mise à jour terminée.',
            backendProgress: 100,
            awaitingReconnect: false,
            reconnectShown: true,
            reconnectProgress: 100,
            failedStep: ''
          });
        }
        return;
      }

      if (state === 'error') {
        stopUpgradeReconnectFlow();
        updateUpgradeUiSession({
          phase: 'error',
          target: target,
          detail: msg || 'Erreur de mise à jour.',
          backendProgress: progress,
          awaitingReconnect: false,
          reconnectShown: false,
          reconnectProgress: 0,
          failedStep: current && current.phase && current.phase !== 'idle' ? current.phase : 'flash'
        });
      }
    }

    async function loadUpgradeConfig() {
      try {
        const data = await fetchOkJson('/api/fwupdate/config', { cache: 'no-store' }, 'configuration firmware indisponible');
        updateHost.value = data.update_host || '';
        flowPath.value = data.flowio_path || '';
        supervisorPath.value = data.supervisor_path || '';
        nextionPath.value = data.nextion_path || '';
        spiffsPath.value = data.spiffs_path || data.cfgdocs_path || '';
        syncUpgradeConfigFieldInitialValues();
      } catch (err) {
        setUpgradeMessage('Échec du chargement de la configuration : ' + err);
      }
    }

    function buildUpgradeConfigPayload() {
      return {
        update_host: updateHost.value.trim(),
        flowio_path: flowPath.value.trim(),
        supervisor_path: supervisorPath.value.trim(),
        nextion_path: nextionPath.value.trim(),
        spiffs_path: spiffsPath.value.trim()
      };
    }

    function syncUpgradeConfigFieldInitialValues(keys) {
      const changedKeys = Array.isArray(keys) ? new Set(keys) : null;
      upgradeConfigFieldDefs.forEach((def) => {
        if (!def || !def.input || !def.button) return;
        if (changedKeys && !changedKeys.has(def.key)) return;
        def.input.dataset.initialValue = def.input.value.trim();
        updateUpgradeConfigFieldApplyState(def);
      });
    }

    function isUpgradeConfigFieldDirty(def) {
      if (!def || !def.input) return false;
      return def.input.value.trim() !== String(def.input.dataset.initialValue || '');
    }

    function updateUpgradeConfigFieldApplyState(def) {
      if (!def || !def.button) return;
      const dirty = isUpgradeConfigFieldDirty(def);
      def.button.disabled = !dirty;
      def.button.classList.toggle('is-dirty', dirty);
      def.button.classList.remove('is-pending');
      def.button.title = dirty ? 'Appliquer ce changement' : 'Aucun changement a appliquer';
      def.button.setAttribute('aria-label', def.button.title);
    }

    async function saveUpgradeConfig(values, successMessage) {
      const payload = values && typeof values === 'object'
        ? values
        : buildUpgradeConfigPayload();
      await fetchOkJson('/api/fwupdate/config', createFormPostOptions(payload), 'échec enregistrement');
      syncUpgradeConfigFieldInitialValues(Object.keys(payload));
      setUpgradeMessage(successMessage || 'Configuration enregistrée.');
    }

    async function applyUpgradeConfigField(def) {
      if (!def || !def.input || !def.button || !isUpgradeConfigFieldDirty(def)) return;
      def.button.disabled = true;
      def.button.classList.add('is-pending');
      try {
        await saveUpgradeConfig({ [def.key]: def.input.value.trim() }, def.successMessage);
      } finally {
        updateUpgradeConfigFieldApplyState(def);
      }
    }

    async function refreshUpgradeStatus() {
      try {
        updateUpgradeView(await fetchOkJson('/api/fwupdate/status', { cache: 'no-store' }, 'échec lecture état'));
      } catch (err) {
        const current = readUpgradeUiSession();
        if (current && (current.awaitingReconnect || current.phase === 'reboot')) {
          enterUpgradeReconnectPhase();
          return;
        }
        setUpgradeMessage('Échec de lecture de l\'état : ' + err);
      }
    }

    async function startUpgrade(target) {
      try {
        startUpgradeUiSession(target);
        startUpgradeStatusPolling(true);
        await saveUpgradeConfig();
        let endpoint = '/fwupdate/nextion';
        if (target === 'supervisor') endpoint = '/fwupdate/supervisor';
        else if (target === 'flowio') endpoint = '/fwupdate/flowio';
        else if (target === 'spiffs') endpoint = '/fwupdate/spiffs';
        await fetchOkJson(endpoint, { method: 'POST' }, 'échec démarrage');
        await refreshUpgradeStatus();
      } catch (err) {
        stopUpgradeReconnectFlow();
        updateUpgradeUiSession({
          phase: 'error',
          target: target,
          detail: 'Échec de la mise à jour : ' + err,
          backendProgress: 0,
          awaitingReconnect: false,
          reconnectShown: false,
          reconnectProgress: 0,
          failedStep: 'target'
        });
        setUpgradeMessage('Échec de la mise à jour : ' + err);
      }
    }

    async function onWifiPageShown() {
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
      const shouldShowInitialTreeSkeleton = !flowCfgLoadedOnce;
      if (shouldShowInitialTreeSkeleton) {
        beginFlowCfgLoading('Chargement de la configuration distante...', { tree: true, detail: false });
      }
      try {
        await ensureFlowCfgLoaded(false);
      } finally {
        if (shouldShowInitialTreeSkeleton) {
          endFlowCfgLoading({ tree: true, detail: false });
        }
      }
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
      return !!appRuntimeMeta
        && isDrawerExpanded();
    }

    function renderDrawerRuntimeMeta() {
      if (!appRuntimeMeta) return;
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

    function buildFlowStatusFromDomains(domainData, dashboardSlots) {
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

      if (Array.isArray(dashboardSlots)) {
        merged.dashboardSlots = dashboardSlots;
      }

      return anyDomainOk ? merged : null;
    }

    function getCachedFlowStatusData() {
      const domainData = {};
      flowStatusDomainKeys.forEach((domainKey) => {
        domainData[domainKey] = flowStatusDomainCache[domainKey].data;
      });
      return buildFlowStatusFromDomains(domainData, flowStatusDashboardSlotsCache.data);
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
      const dashboardSlots = Array.isArray(data && data.dashboardSlots) ? data.dashboardSlots : [];
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
      const dashboardLabelByRuntimeUiId = new Map();
      dashboardSlots.forEach((slot) => {
        const runtimeUiId = dashboardSlotRuntimeUiId(slot);
        const label = (slot && typeof slot.label === 'string') ? slot.label.trim() : '';
        if (!runtimeUiId || !label || dashboardLabelByRuntimeUiId.has(runtimeUiId)) return;
        dashboardLabelByRuntimeUiId.set(runtimeUiId, label);
      });
      const waterTempLabel = dashboardLabelByRuntimeUiId.get(flowStatusDashboardRuntimeUiIds.waterTemp) || 'Temperature eau';
      const airTempLabel = dashboardLabelByRuntimeUiId.get(flowStatusDashboardRuntimeUiIds.airTemp) || 'Temperature air';
      const phLabel = dashboardLabelByRuntimeUiId.get(flowStatusDashboardRuntimeUiIds.ph) || 'pH';
      const orpLabel = dashboardLabelByRuntimeUiId.get(flowStatusDashboardRuntimeUiIds.orp) || 'ORP';
      const poolGaugeNodes = [
        buildFlowArcGauge({
          label: waterTempLabel,
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
          label: airTempLabel,
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
          label: phLabel,
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
          label: orpLabel,
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
        title: 'Flow.io',
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
        ? 'Flow.io disponible'
        : 'Connexion Flow.io a verifier';
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
        const dashboardSlots = await fetchFlowStatusDashboardSlots(!!forceRefresh).catch(() => []);
        if (reqSeq !== flowStatusReqSeq) return;
        const data = buildFlowStatusFromDomains(domainData, dashboardSlots);
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
      poolMeasuresStatus.textContent = 'Chargement mesures echoue: ' + err;
    }

    function startPoolMeasuresTimer() {
      poolMeasuresPoller.start();
    }

    function normalizeRuntimeMeasureDomainKey(domain) {
      const key = String(domain || '').trim().toLowerCase();
      return runtimeMeasureDomainKeys.includes(key) ? key : '';
    }

    function createEmptyRuntimeManifestDomainCache() {
      return {
        pool: [],
        mqtt: [],
        system: [],
        wifi: [],
        alarm: []
      };
    }

    function activePoolMeasureDomainKeys() {
      return runtimeMeasureDomainKeys.filter((domainKey) => poolMeasureDomainState[domainKey].active);
    }

    function countJsonBraces(line) {
      let depth = 0;
      for (let i = 0; i < line.length; ++i) {
        const ch = line.charAt(i);
        if (ch === '{') depth += 1;
        if (ch === '}') depth -= 1;
      }
      return depth;
    }

    function registerRuntimeManifestEntry(cache, entry) {
      if (!entry || !Number.isFinite(Number(entry.id))) return;
      const domainKey = normalizeRuntimeMeasureDomainKey(entry.domain);
      if (!domainKey || !Array.isArray(cache[domainKey])) return;
      cache[domainKey].push(entry);
    }

    async function parseRuntimeManifestStreamIntoCache(response, cache) {
      if (!response.body || typeof response.body.getReader !== 'function' || typeof TextDecoder !== 'function') {
        const data = await response.json().catch(() => null);
        const values = Array.isArray(data && data.values) ? data.values : [];
        values.forEach((entry) => registerRuntimeManifestEntry(cache, entry));
        return;
      }

      const reader = response.body.getReader();
      const decoder = new TextDecoder();
      let buffer = '';
      let insideValues = false;
      let insideEntry = false;
      let braceDepth = 0;
      let entryLines = [];

      const processLine = (rawLine) => {
        const line = String(rawLine || '').trim();
        if (!line) return;
        if (!insideValues) {
          if (line.indexOf('"values"') !== -1 && line.indexOf('[') !== -1) {
            insideValues = true;
          }
          return;
        }
        if (!insideEntry) {
          if (line.charAt(0) === '{') {
            insideEntry = true;
            braceDepth = countJsonBraces(line);
            entryLines = [line];
          }
          return;
        }

        entryLines.push(line);
        braceDepth += countJsonBraces(line);
        if (braceDepth > 0) return;

        const objectText = entryLines.join('\n').replace(/,$/, '');
        entryLines = [];
        insideEntry = false;
        try {
          registerRuntimeManifestEntry(cache, JSON.parse(objectText));
        } catch (err) {
          throw new Error('manifeste runtime invalide');
        }
      };

      while (true) {
        const chunk = await reader.read();
        if (chunk.done) break;
        buffer += decoder.decode(chunk.value, { stream: true });
        const lines = buffer.split('\n');
        buffer = lines.pop() || '';
        lines.forEach(processLine);
      }
      buffer += decoder.decode();
      if (buffer) processLine(buffer);
    }

    async function loadRuntimeManifestDomains(forceRefresh) {
      if (!forceRefresh && runtimeManifestDomainCache) {
        return runtimeManifestDomainCache;
      }
      if (!forceRefresh && runtimeManifestDomainLoadPromise) {
        return runtimeManifestDomainLoadPromise;
      }

      runtimeManifestDomainLoadPromise = (async () => {
        const response = await fetch(assetUrl('/webinterface/runtimeui.json'), {
          cache: forceRefresh ? 'no-store' : 'default'
        });
        if (!response.ok) throw new Error('manifeste runtime indisponible');
        const nextCache = createEmptyRuntimeManifestDomainCache();
        await parseRuntimeManifestStreamIntoCache(response, nextCache);
        runtimeManifestDomainCache = nextCache;
        return runtimeManifestDomainCache;
      })();

      try {
        return await runtimeManifestDomainLoadPromise;
      } finally {
        runtimeManifestDomainLoadPromise = null;
      }
    }

    async function runtimeMeasureEntriesForDomain(domainKey, forceRefresh) {
      const cleanDomain = normalizeRuntimeMeasureDomainKey(domainKey);
      if (!cleanDomain) return [];
      const cache = await loadRuntimeManifestDomains(!!forceRefresh);
      return Array.isArray(cache[cleanDomain]) ? cache[cleanDomain] : [];
    }

    async function refreshDrawerRuntimeMeta(forceRefresh) {
      if (!appRuntimeMeta) return;
      renderDrawerRuntimeMeta();
      if (!isDrawerRuntimeMetaVisible() || !forceRefresh) return;
      await loadWebMeta({ skipDrawerRuntimeRender: false });
    }

    function startDrawerRuntimeTimer() {
      drawerRuntimePoller.start();
    }

    function formatRuntimeDomainLabel(domain) {
      const key = String(domain || '').trim().toLowerCase();
      if (key === 'pool') return 'Piscine';
      if (key === 'mqtt') return 'MQTT';
      if (key === 'wifi') return 'WiFi';
      if (key === 'i2c') return 'I2C';
      if (key === 'system') return 'Système';
      if (key === 'alarm') return 'Alarmes';
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

    function runtimeMeasureCssSlug(value) {
      const source = String(value || '').trim().toLowerCase();
      if (!source) return 'default';
      const normalized = typeof source.normalize === 'function'
        ? source.normalize('NFD').replace(/[\u0300-\u036f]/g, '')
        : source;
      const slug = normalized.replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '');
      return slug || 'default';
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
      const cleanIds = (ids || [])
        .map((id) => Number(id))
        .filter((id) => Number.isFinite(id) && id > 0);
      if (!cleanIds.length) return [];
      const query = cleanIds.join(',');
      const data = await fetchOkJson(
        '/api/runtime/values?ids=' + encodeURIComponent(query),
        { cache: 'no-store' },
        'lecture runtime indisponible',
        fetchFlowRemoteQueued
      );
      if (!Array.isArray(data.values)) throw new Error('lecture runtime indisponible');
      return data.values;
    }

    async function fetchPoolDashboardSlots() {
      const data = await fetchOkJson(
        '/api/runtime/dashboard_slots',
        { cache: 'no-store' },
        'lecture sondes supervisor indisponible'
      );
      return Array.isArray(data.slots) ? data.slots : [];
    }

    function dashboardSlotRuntimeUiId(slot) {
      if (!slot || typeof slot !== 'object') return 0;
      const raw = slot.runtime_ui_id ?? slot.runtimeUiId ?? 0;
      const id = Number(raw);
      return Number.isFinite(id) && id > 0 ? id : 0;
    }

    async function fetchFlowStatusDashboardSlots(forceRefresh) {
      const now = Date.now();
      const cacheValid = Array.isArray(flowStatusDashboardSlotsCache.data) &&
        ((now - flowStatusDashboardSlotsCache.fetchedAt) < flowStatusDomainTtlMs);
      if (!forceRefresh && cacheValid) {
        return flowStatusDashboardSlotsCache.data;
      }

      try {
        const slots = await fetchPoolDashboardSlots();
        flowStatusDashboardSlotsCache.data = Array.isArray(slots) ? slots : [];
        flowStatusDashboardSlotsCache.fetchedAt = Date.now();
        return flowStatusDashboardSlotsCache.data;
      } catch (err) {
        if (Array.isArray(flowStatusDashboardSlotsCache.data) && flowStatusDashboardSlotsCache.data.length) {
          return flowStatusDashboardSlotsCache.data;
        }
        throw err;
      }
    }

    function isPoolDashboardSondesEntry(entry) {
      if (!entry || String(entry.domain || '').trim().toLowerCase() !== 'pool') return false;
      return String(entry.group || '').trim().localeCompare('Sondes', 'fr', { sensitivity: 'base' }) === 0;
    }

    function poolDashboardSlotDisplayParts(slot) {
      const available = !!(slot && slot.available);
      const unit = (slot && typeof slot.unit === 'string') ? slot.unit.trim() : '';
      const rawValue = (slot && typeof slot.value === 'string') ? slot.value.trim() : '';
      if (!available) {
        return { valueText: '--', unitText: '' };
      }
      if (!rawValue) {
        return { valueText: '-', unitText: '' };
      }
      if (!unit) {
        return { valueText: rawValue, unitText: '' };
      }

      const suffix = ' ' + unit;
      if (rawValue.length > suffix.length && rawValue.endsWith(suffix)) {
        return {
          valueText: rawValue.slice(0, rawValue.length - suffix.length).trim(),
          unitText: unit
        };
      }
      return { valueText: rawValue, unitText: unit };
    }

    function dashboardSlotBgColor(slot) {
      const color = (slot && typeof slot.bg_color === 'string') ? slot.bg_color.trim() : '';
      return /^#[0-9A-Fa-f]{6}$/.test(color) ? color : '';
    }

    function buildPoolDashboardSlotsCard(slots) {
      const cleanSlots = Array.isArray(slots) ? slots : [];
      if (!cleanSlots.length) return null;

      const card = document.createElement('div');
      card.className = 'status-card status-card-runtime status-card-runtime-domain-pool status-card-runtime-group-sondes';

      const heading = document.createElement('h3');
      heading.textContent = formatRuntimeGroupCardTitle('pool', 'Sondes');
      card.appendChild(heading);

      const grid = document.createElement('div');
      grid.className = 'status-dashboard-slot-grid';
      cleanSlots.forEach((slot) => {
        const label = (slot && typeof slot.label === 'string') ? slot.label.trim() : '';
        const display = poolDashboardSlotDisplayParts(slot);
        if (!label && !display.valueText && !display.unitText) return;

        const tile = document.createElement('div');
        tile.className = 'status-dashboard-slot';
        if (!(slot && slot.available)) tile.classList.add('is-empty');
        const bgColor = dashboardSlotBgColor(slot);
        if (bgColor) {
          tile.style.background = bgColor;
        }

        const title = document.createElement('div');
        title.className = 'status-dashboard-slot-title';
        title.textContent = label || 'Mesure';
        tile.appendChild(title);

        const metric = document.createElement('div');
        metric.className = 'status-dashboard-slot-metric';

        const value = document.createElement('span');
        value.className = 'status-dashboard-slot-value';
        value.textContent = display.valueText || '-';
        metric.appendChild(value);

        if (display.unitText) {
          const unit = document.createElement('span');
          unit.className = 'status-dashboard-slot-unit';
          unit.textContent = display.unitText;
          metric.appendChild(unit);
        }

        tile.appendChild(metric);
        grid.appendChild(tile);
      });
      if (!grid.childNodes.length) return null;
      card.appendChild(grid);
      return card;
    }

    function buildDashboardSlotLabelByRuntimeUiId(slots) {
      const labelByRuntimeUiId = new Map();
      (Array.isArray(slots) ? slots : []).forEach((slot) => {
        const runtimeUiId = dashboardSlotRuntimeUiId(slot);
        const label = (slot && typeof slot.label === 'string') ? slot.label.trim() : '';
        if (!runtimeUiId || !label || labelByRuntimeUiId.has(runtimeUiId)) return;
        labelByRuntimeUiId.set(runtimeUiId, label);
      });
      return labelByRuntimeUiId;
    }

    function runtimeMeasureDisplayLabel(entry) {
      return entry.label || entry.key || String(entry.id);
    }

    function runtimeMeasureResolvedLabel(entry, options) {
      const opts = options && typeof options === 'object' ? options : {};
      if (typeof opts.displayLabelResolver === 'function') {
        const resolved = String(opts.displayLabelResolver(entry) || '').trim();
        if (resolved) return resolved;
      }
      return runtimeMeasureDisplayLabel(entry);
    }

    function formatRuntimeFloatValue(value, decimals) {
      const n = Number(value);
      if (!Number.isFinite(n)) return '-';
      if (decimals !== null) {
        return n.toFixed(decimals);
      }
      const rounded = Math.round(n * 1000) / 1000;
      const text = rounded.toFixed(3).replace(/(?:\.0+|(\.\d*?)0+)$/, '$1');
      return text === '-0' ? '0' : text;
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
        const text = formatRuntimeFloatValue(value, decimals);
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
      if (explicit === 'circ-gauge' || explicit === 'horiz-gauge' || explicit === 'badge' || explicit === 'boolean' || explicit === 'time' || explicit === 'value' || explicit === 'flags') {
        return explicit;
      }
      return String(entry && entry.type ? entry.type : '') === 'bool' ? 'boolean' : 'value';
    }

    function runtimeMeasureDisplayConfig(entry) {
      return (entry && entry.displayConfig && typeof entry.displayConfig === 'object' && !Array.isArray(entry.displayConfig))
        ? entry.displayConfig
        : {};
    }

    function buildRuntimeMeasureCircGaugeNode(entry, runtimeValue, options) {
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
        label: String(runtimeMeasureResolvedLabel(entry, options) || 'Mesure'),
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

    function buildRuntimeMeasureBooleanNode(entry, runtimeValue, options) {
      const displayConfig = runtimeMeasureDisplayConfig(entry);
      const opts = options && typeof options === 'object' ? options : {};
      const value = (!runtimeValue || runtimeValue.status === 'not_found' || runtimeValue.status === 'unavailable')
        ? null
        : (typeof runtimeValue.value === 'boolean' ? runtimeValue.value : null);
      const booleanTexts = (opts.booleanTexts && typeof opts.booleanTexts === 'object') ? opts.booleanTexts : {};

      return buildFlowReadonlyStateTile(
        String(runtimeMeasureResolvedLabel(entry, opts) || 'Etat'),
        value,
        {
          activeText: booleanTexts.activeText || displayConfig.activeText,
          inactiveText: booleanTexts.inactiveText || displayConfig.inactiveText,
          unknownText: booleanTexts.unknownText || displayConfig.unknownText
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

    function runtimeMeasureFlagRole(entry) {
      const displayConfig = runtimeMeasureDisplayConfig(entry);
      const role = String(displayConfig.flagRole || '').trim().toLowerCase();
      if (role === 'active' || role === 'resettable' || role === 'condition') return role;
      return '';
    }

    function runtimeMeasureFlagColumnLabel(entry) {
      const displayConfig = runtimeMeasureDisplayConfig(entry);
      const explicit = String(displayConfig.columnLabel || '').trim();
      if (explicit) return explicit;
      const role = runtimeMeasureFlagRole(entry);
      if (role === 'active') return 'Act.';
      if (role === 'resettable') return 'Reset';
      if (role === 'condition') return 'Cond.';
      return runtimeMeasureDisplayLabel(entry);
    }

    function normalizeRuntimeMeasureFlags(entry) {
      const rawFlags = Array.isArray(entry && entry.flags) ? entry.flags : [];
      return rawFlags
        .map((flag, index) => {
          const mask = Number(flag && flag.mask);
          const label = String(flag && flag.label ? flag.label : '').trim();
          if (!Number.isFinite(mask) || mask <= 0 || !label) return null;
          return {
            mask: Math.trunc(mask),
            label,
            order: Number.isFinite(Number(flag && flag.order)) ? Number(flag.order) : index
          };
        })
        .filter((flag) => !!flag)
        .sort((left, right) => left.order - right.order);
    }

    function runtimeMeasureMaskValue(runtimeValue) {
      if (!runtimeValue || runtimeValue.status === 'not_found' || runtimeValue.status === 'unavailable') {
        return null;
      }
      const value = Number(runtimeValue.value);
      if (!Number.isFinite(value)) return null;
      return Math.trunc(value);
    }

    function mergeRuntimeAlarmFlags(entries) {
      const byMask = new Map();

      (entries || []).forEach((entry) => {
        normalizeRuntimeMeasureFlags(entry).forEach((flag) => {
          if (!flag || !Number.isFinite(Number(flag.mask))) return;
          const mask = Math.trunc(Number(flag.mask));
          if (mask <= 0) return;
          const existing = byMask.get(mask);
          if (!existing) {
            byMask.set(mask, flag);
            return;
          }
          if ((!existing.label || !existing.label.trim()) && flag.label && flag.label.trim()) {
            byMask.set(mask, flag);
          }
        });
      });

      return Array.from(byMask.values())
        .sort((left, right) => {
          const leftOrder = Number.isFinite(Number(left && left.order)) ? Number(left.order) : 9999;
          const rightOrder = Number.isFinite(Number(right && right.order)) ? Number(right.order) : 9999;
          if (leftOrder !== rightOrder) return leftOrder - rightOrder;
          return Number(left.mask) - Number(right.mask);
        });
    }

    function buildRuntimeMeasureFlagCell(value, label, columnLabel) {
      const known = typeof value === 'boolean';
      const state = known ? (value ? 'is-true' : 'is-false') : 'is-empty';
      const marker = document.createElement('span');
      marker.className = 'status-flag-check ' + state;
      marker.textContent = known ? (value ? '✓' : '') : '?';
      marker.setAttribute(
        'aria-label',
        label + ' / ' + columnLabel + ' : ' + (known ? (value ? 'oui' : 'non') : 'indisponible')
      );
      return marker;
    }

    function isRuntimeAlarmGroup(group) {
      if (!group) return false;
      if (String(group.domainKey || '').trim().toLowerCase() !== 'alarm') return false;
      return String(group.groupKey || '').trim().localeCompare('Alarmes', 'fr', { sensitivity: 'base' }) === 0;
    }

    function buildRuntimeAlarmStateNode(value) {
      const known = typeof value === 'boolean';
      const node = document.createElement('div');
      node.className = 'status-alarm-slot-state ' + (known ? (value ? 'is-true' : 'is-false') : 'is-empty');

      const dot = document.createElement('span');
      dot.className = 'status-state-dot';
      node.appendChild(dot);

      const text = document.createElement('span');
      text.className = 'status-alarm-slot-state-text';
      text.textContent = known ? (value ? 'Déclenchée' : 'OK') : 'Indispo';
      node.appendChild(text);
      return node;
    }

    function buildRuntimeAlarmConditionNode(value) {
      const known = typeof value === 'boolean';
      const node = document.createElement('div');
      node.className = 'status-alarm-slot-condition ' + (known ? (value ? 'is-true' : 'is-false') : 'is-empty');
      node.textContent = known ? ('Statut: ' + (value ? 'KO' : 'OK')) : 'Statut: ?';
      return node;
    }

    function buildRuntimeAlarmGrid(entries, valueById) {
      const columnsByRole = new Map();
      const flagDefs = mergeRuntimeAlarmFlags(entries);

      (entries || []).forEach((entry) => {
        const role = runtimeMeasureFlagRole(entry);
        if (!role || columnsByRole.has(role)) return;
        columnsByRole.set(role, entry);
      });

      const activeEntry = columnsByRole.get('active') || null;
      const conditionEntry = columnsByRole.get('condition') || null;
      if (!flagDefs.length || (!activeEntry && !conditionEntry)) return null;

      const activeMaskValue = activeEntry ? runtimeMeasureMaskValue(valueById.get(Number(activeEntry.id))) : null;
      const conditionMaskValue = conditionEntry ? runtimeMeasureMaskValue(valueById.get(Number(conditionEntry.id))) : null;
      const maxSlots = 8;

      const grid = document.createElement('div');
      grid.className = 'status-alarm-slot-grid';

      for (let index = 0; index < maxSlots; index += 1) {
        const flag = flagDefs[index] || null;
        const tile = document.createElement('div');
        tile.className = 'status-alarm-slot';

        if (!flag) {
          tile.classList.add('is-empty');
          grid.appendChild(tile);
          continue;
        }

        const title = document.createElement('div');
        title.className = 'status-alarm-slot-title';
        title.textContent = flag.label;
        tile.appendChild(title);

        const footer = document.createElement('div');
        footer.className = 'status-alarm-slot-row';

        const activeValue = activeMaskValue === null ? null : ((activeMaskValue & flag.mask) !== 0);
        const conditionValue = conditionMaskValue === null ? null : ((conditionMaskValue & flag.mask) !== 0);
        footer.appendChild(buildRuntimeAlarmStateNode(activeValue));
        footer.appendChild(buildRuntimeAlarmConditionNode(conditionValue));

        tile.appendChild(footer);
        grid.appendChild(tile);
      }

      return grid;
    }

    function buildRuntimeMeasureFlagsTable(entries, valueById) {
      const columnsByRole = new Map();
      let flagDefs = [];

      (entries || []).forEach((entry) => {
        const role = runtimeMeasureFlagRole(entry);
        if (!role || columnsByRole.has(role)) return;
        columnsByRole.set(role, entry);
        if (!flagDefs.length) {
          flagDefs = normalizeRuntimeMeasureFlags(entry);
        }
      });

      if (!flagDefs.length || columnsByRole.size === 0) return null;

      const orderedColumns = ['active', 'condition']
        .map((role) => ({ role, entry: columnsByRole.get(role) || null }))
        .filter((column) => !!column.entry);
      if (!orderedColumns.length) return null;

      const table = document.createElement('table');
      table.className = 'status-flag-table';

      const thead = document.createElement('thead');
      const headRow = document.createElement('tr');
      const nameHead = document.createElement('th');
      nameHead.scope = 'col';
      nameHead.textContent = 'Alarme';
      headRow.appendChild(nameHead);
      orderedColumns.forEach((column) => {
        const th = document.createElement('th');
        th.scope = 'col';
        th.textContent = runtimeMeasureFlagColumnLabel(column.entry);
        headRow.appendChild(th);
      });
      thead.appendChild(headRow);
      table.appendChild(thead);

      const tbody = document.createElement('tbody');
      flagDefs.forEach((flag) => {
        const row = document.createElement('tr');
        const labelCell = document.createElement('th');
        labelCell.scope = 'row';
        labelCell.textContent = flag.label;
        row.appendChild(labelCell);

        orderedColumns.forEach((column) => {
          const runtimeValue = valueById.get(Number(column.entry.id));
          const maskValue = runtimeMeasureMaskValue(runtimeValue);
          const cell = document.createElement('td');
          const enabled = maskValue === null ? null : ((maskValue & flag.mask) !== 0);
          cell.appendChild(buildRuntimeMeasureFlagCell(enabled, flag.label, runtimeMeasureFlagColumnLabel(column.entry)));
          row.appendChild(cell);
        });

        tbody.appendChild(row);
      });
      table.appendChild(tbody);
      return table;
    }

    function buildPoolMeasureCards(entries, values, options) {
      const fragment = document.createDocumentFragment();
      const opts = options && typeof options === 'object' ? options : {};
      const dashboardLabelByRuntimeUiId = buildDashboardSlotLabelByRuntimeUiId(opts.dashboardSlots);
      const valueById = new Map();
      (values || []).forEach((item) => {
        const id = Number(item && item.id);
        if (Number.isFinite(id)) valueById.set(id, item);
      });

      const groups = [];
      const groupsByName = new Map();
      (entries || []).forEach((entry) => {
        const domainKey = String(entry.domain || 'runtime');
        const groupKey = String(entry.group || '').trim();
        const cardKey = domainKey + '::' + groupKey;
        const cardTitle = formatRuntimeGroupCardTitle(domainKey, groupKey);
        let group = groupsByName.get(cardKey);
        if (!group) {
          group = { name: cardTitle, domainKey, groupKey, entries: [] };
          groupsByName.set(cardKey, group);
          groups.push(group);
        }
        group.entries.push(entry);
      });

      groups.forEach((group) => {
        const card = document.createElement('div');
        card.className =
          'status-card status-card-runtime'
          + ' status-card-runtime-domain-' + runtimeMeasureCssSlug(group.domainKey)
          + ' status-card-runtime-group-' + runtimeMeasureCssSlug(group.groupKey);
        const isPoolModeGroup =
          String(group.domainKey || '').trim().toLowerCase() === 'pool' &&
          String(group.groupKey || '').trim().localeCompare('Mode', 'fr', { sensitivity: 'base' }) === 0;
        const isPoolDashboardGroup =
          String(group.domainKey || '').trim().toLowerCase() === 'pool' &&
          String(group.groupKey || '').trim().localeCompare('Dashboard', 'fr', { sensitivity: 'base' }) === 0;
        const groupDisplayOptions = {
          displayLabelResolver: (entry) => {
            if (isPoolDashboardGroup) {
              const runtimeUiId = Number(entry && (entry.runtimeId ?? entry.runtime_ui_id ?? entry.id));
              const override = dashboardLabelByRuntimeUiId.get(runtimeUiId);
              if (override) return override;
            }
            return runtimeMeasureDisplayLabel(entry);
          },
          booleanTexts: isPoolModeGroup
            ? {
              activeText: 'Marche',
              inactiveText: 'Arrêt'
            }
            : null
        };

        const heading = document.createElement('h3');
        heading.textContent = group.name;
        card.appendChild(heading);
        const badgeNodes = [];
        const circGaugeNodes = [];
        const horizGaugeRows = [];
        const booleanNodes = [];
        const flagEntries = [];
        const valueRows = [];

        group.entries.forEach((entry) => {
          const runtimeValue = valueById.get(Number(entry.id));
          const display = runtimeMeasureDisplayKind(entry);
          if (display === 'badge') {
            badgeNodes.push(buildRuntimeMeasureBadgeNode(entry, runtimeValue));
            return;
          }
          if (display === 'circ-gauge') {
            const gaugeNode = buildRuntimeMeasureCircGaugeNode(entry, runtimeValue, groupDisplayOptions);
            if (gaugeNode) {
              circGaugeNodes.push(gaugeNode);
              return;
            }
          }
          if (display === 'horiz-gauge') {
            horizGaugeRows.push([
              runtimeMeasureResolvedLabel(entry, groupDisplayOptions),
              buildRuntimeMeasureHorizGaugeNode(entry, runtimeValue)
            ]);
            return;
          }
          if (display === 'boolean') {
            booleanNodes.push(buildRuntimeMeasureBooleanNode(entry, runtimeValue, groupDisplayOptions));
            return;
          }
          if (display === 'flags') {
            flagEntries.push(entry);
            return;
          }
          valueRows.push([
            runtimeMeasureResolvedLabel(entry, groupDisplayOptions),
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

        if (flagEntries.length) {
          if (isRuntimeAlarmGroup(group)) {
            const alarmGrid = buildRuntimeAlarmGrid(flagEntries, valueById);
            if (alarmGrid) {
              card.appendChild(alarmGrid);
            } else {
              const flagTable = buildRuntimeMeasureFlagsTable(flagEntries, valueById);
              if (flagTable) card.appendChild(flagTable);
            }
          } else {
            const flagTable = buildRuntimeMeasureFlagsTable(flagEntries, valueById);
            if (flagTable) card.appendChild(flagTable);
          }
        }

        if (horizGaugeRows.length || valueRows.length) {
          const kv = document.createElement('div');
          kv.className = 'status-kv';
          horizGaugeRows.forEach((row) => appendFlowStatusRow(kv, row[0], row[1]));
          valueRows.forEach((row) => appendFlowStatusRow(kv, row[0], row[1]));
          card.appendChild(kv);
        }

        fragment.appendChild(card);
      });
      return fragment;
    }

    function renderPoolMeasureDomainButtons() {
      if (!poolMeasuresDomains) return;
      poolMeasuresDomains.innerHTML = '';
      runtimeMeasureDomainKeys.forEach((domainKey) => {
        const state = poolMeasureDomainState[domainKey];
        const animation = takePoolMeasureDomainAnimation(domainKey);
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'measure-domain-chip'
          + (state.active ? ' active' : '')
          + (state.loading ? ' is-loading' : '')
          + (animation ? ' is-pulsing' : '')
          + (animation && animation.activating ? ' is-activating' : '');
        button.setAttribute('aria-pressed', state.active ? 'true' : 'false');
        button.setAttribute('aria-label', (state.active ? 'Masquer ' : 'Afficher ') + formatRuntimeDomainLabel(domainKey));
        if (animation) {
          button.style.setProperty('--measure-ripple-x', animation.x);
          button.style.setProperty('--measure-ripple-y', animation.y);
        }

        const check = document.createElement('span');
        check.className = 'measure-domain-chip-check';
        check.setAttribute('aria-hidden', 'true');
        check.textContent = '✓';
        button.appendChild(check);

        const label = document.createElement('span');
        label.className = 'measure-domain-chip-label';
        label.textContent = formatRuntimeDomainLabel(domainKey);
        button.appendChild(label);

        button.addEventListener('pointerdown', () => {
          button.classList.add('is-pressing');
        });
        ['pointerup', 'pointerleave', 'pointercancel', 'blur'].forEach((eventName) => {
          button.addEventListener(eventName, () => {
            button.classList.remove('is-pressing');
          });
        });
        button.addEventListener('click', async (event) => {
          primePoolMeasureDomainAnimation(domainKey, event, !state.active);
          await togglePoolMeasureDomain(domainKey);
        });
        poolMeasuresDomains.appendChild(button);
      });
    }

    function primePoolMeasureDomainAnimation(domainKey, event, activating) {
      const cleanDomain = normalizeRuntimeMeasureDomainKey(domainKey);
      if (!cleanDomain) return;
      let x = '50%';
      let y = '50%';
      const target = event && event.currentTarget instanceof HTMLElement ? event.currentTarget : null;
      if (target) {
        const rect = target.getBoundingClientRect();
        const clientX = typeof event.clientX === 'number' ? event.clientX : rect.left + (rect.width / 2);
        const clientY = typeof event.clientY === 'number' ? event.clientY : rect.top + (rect.height / 2);
        const ratioX = Math.max(0, Math.min(100, ((clientX - rect.left) / Math.max(rect.width, 1)) * 100));
        const ratioY = Math.max(0, Math.min(100, ((clientY - rect.top) / Math.max(rect.height, 1)) * 100));
        x = ratioX.toFixed(1) + '%';
        y = ratioY.toFixed(1) + '%';
      }
      poolMeasureDomainAnimations[cleanDomain] = {
        until: Date.now() + 720,
        activating: !!activating,
        rendered: false,
        x,
        y
      };
    }

    function takePoolMeasureDomainAnimation(domainKey) {
      const cleanDomain = normalizeRuntimeMeasureDomainKey(domainKey);
      if (!cleanDomain) return null;
      const animation = poolMeasureDomainAnimations[cleanDomain];
      if (!animation) return null;
      if (animation.until <= Date.now()) {
        delete poolMeasureDomainAnimations[cleanDomain];
        return null;
      }
      if (animation.rendered) return null;
      animation.rendered = true;
      return animation;
    }

    function poolMeasureDomainHasRenderableData(domainKey, state) {
      const cleanDomain = normalizeRuntimeMeasureDomainKey(domainKey);
      const domainState = state && typeof state === 'object' ? state : null;
      if (!cleanDomain || !domainState) return false;
      if (Array.isArray(domainState.entries) && domainState.entries.length > 0) return true;
      return cleanDomain === 'pool' && Array.isArray(domainState.dashboardSlots) && domainState.dashboardSlots.length > 0;
    }

    function renderPoolMeasuresGrid() {
      if (!poolMeasuresGrid) return;
      poolMeasuresGrid.innerHTML = '';

      const activeDomains = activePoolMeasureDomainKeys();
      if (!activeDomains.length) {
        const empty = document.createElement('div');
        empty.className = 'measure-domain-empty';
        empty.textContent = 'Activez un badge pour charger un domaine.';
        poolMeasuresGrid.appendChild(empty);
        return;
      }

      let renderedCardCount = 0;
      activeDomains.forEach((domainKey) => {
        const state = poolMeasureDomainState[domainKey];
        const hasDashboardSlots = cleanDomainName => cleanDomainName === 'pool' && Array.isArray(state.dashboardSlots) && state.dashboardSlots.length > 0;
        const hasRenderableData = poolMeasureDomainHasRenderableData(domainKey, state);
        if (state.loading && !hasRenderableData) {
          const card = document.createElement('div');
          card.className = 'status-card';
          const heading = document.createElement('h3');
          heading.textContent = formatRuntimeDomainLabel(domainKey);
          const summary = document.createElement('p');
          summary.className = 'status-card-summary';
          summary.textContent = 'Chargement en cours...';
          card.appendChild(heading);
          card.appendChild(summary);
          poolMeasuresGrid.appendChild(card);
          renderedCardCount += 1;
          return;
        }
        if (state.error && !hasRenderableData) {
          const card = document.createElement('div');
          card.className = 'status-card';
          const heading = document.createElement('h3');
          heading.textContent = formatRuntimeDomainLabel(domainKey);
          const summary = document.createElement('p');
          summary.className = 'status-card-summary';
          summary.textContent = state.error;
          card.appendChild(heading);
          card.appendChild(summary);
          poolMeasuresGrid.appendChild(card);
          renderedCardCount += 1;
          return;
        }
        if (!state.entries.length && !hasDashboardSlots(domainKey)) {
          const card = document.createElement('div');
          card.className = 'status-card';
          const heading = document.createElement('h3');
          heading.textContent = formatRuntimeDomainLabel(domainKey);
          const summary = document.createElement('p');
          summary.className = 'status-card-summary';
          summary.textContent = 'Aucune valeur runtime exposee pour ce domaine.';
          card.appendChild(heading);
          card.appendChild(summary);
          poolMeasuresGrid.appendChild(card);
          renderedCardCount += 1;
          return;
        }
        if (hasDashboardSlots(domainKey)) {
          const dashboardCard = buildPoolDashboardSlotsCard(state.dashboardSlots);
          if (dashboardCard) {
            poolMeasuresGrid.appendChild(dashboardCard);
            renderedCardCount += 1;
          }
        }
        const cards = buildPoolMeasureCards(state.entries, state.values, { dashboardSlots: state.dashboardSlots });
        renderedCardCount += cards.childNodes.length;
        poolMeasuresGrid.appendChild(cards);
      });

      if (renderedCardCount === 0) {
        const empty = document.createElement('div');
        empty.className = 'measure-domain-empty';
        empty.textContent = 'Aucune valeur runtime disponible pour les domaines actifs.';
        poolMeasuresGrid.appendChild(empty);
      }
    }

    function refreshPoolMeasuresStatus() {
      const activeDomains = activePoolMeasureDomainKeys();
      const domainLabel = (count) => count > 1 ? 'Domaines' : 'Domaine';
      const valueLabel = (count) => count > 1 ? 'Valeurs' : 'Valeur';
      if (!activeDomains.length) {
        poolMeasuresStatus.textContent = 'Domaine: 0 | Valeur: 0';
        return;
      }

      let loadingCount = 0;
      let errorCount = 0;
      let valueCount = 0;
      activeDomains.forEach((domainKey) => {
        const state = poolMeasureDomainState[domainKey];
        if (state.loading) loadingCount += 1;
        if (state.error) errorCount += 1;
        valueCount += state.entries.length;
        if (domainKey === 'pool' && Array.isArray(state.dashboardSlots)) {
          valueCount += state.dashboardSlots.length;
        }
      });

      if (loadingCount > 0) {
        poolMeasuresStatus.textContent =
          'Chargement: ' + loadingCount + ' domaine' + (loadingCount > 1 ? 's' : '');
        return;
      }
      if (errorCount > 0) {
        poolMeasuresStatus.textContent =
          'Erreur(s): ' + errorCount + ' domaine' + (errorCount > 1 ? 's' : '');
        return;
      }
      poolMeasuresStatus.textContent =
        domainLabel(activeDomains.length) + ': ' + activeDomains.length + ' | ' +
        valueLabel(valueCount) + ': ' + valueCount;
    }

    function refreshPoolMeasuresView() {
      renderPoolMeasureDomainButtons();
      renderPoolMeasuresGrid();
      refreshPoolMeasuresStatus();
    }

    async function loadPoolMeasureDomain(domainKey, forceRefresh) {
      const cleanDomain = normalizeRuntimeMeasureDomainKey(domainKey);
      if (!cleanDomain) return;
      const state = poolMeasureDomainState[cleanDomain];
      if (!state.active) return;
      const hadRenderableData = poolMeasureDomainHasRenderableData(cleanDomain, state);
      const requestSeq = state.requestSeq + 1;
      state.requestSeq = requestSeq;
      state.loading = true;
      state.error = '';
      if (hadRenderableData) {
        renderPoolMeasureDomainButtons();
        refreshPoolMeasuresStatus();
      } else {
        refreshPoolMeasuresView();
      }

      try {
        const allEntries = await runtimeMeasureEntriesForDomain(cleanDomain, !!forceRefresh);
        const entries = cleanDomain === 'pool'
          ? allEntries.filter((entry) => !isPoolDashboardSondesEntry(entry))
          : allEntries;
        const ids = entries.map((entry) => Number(entry.id)).filter((id) => Number.isFinite(id));
        const values = ids.length ? await fetchRuntimeValues(ids) : [];
        const dashboardSlots = cleanDomain === 'pool'
          ? await fetchPoolDashboardSlots().catch(() => [])
          : [];
        if (state.requestSeq !== requestSeq) return;
        state.entries = entries;
        state.values = values;
        state.dashboardSlots = dashboardSlots;
        state.error = '';
      } catch (err) {
        if (state.requestSeq !== requestSeq) return;
        if (!hadRenderableData) {
          state.entries = [];
          state.values = [];
          state.dashboardSlots = [];
        }
        state.error = 'Chargement ' + formatRuntimeDomainLabel(cleanDomain) + ' echoue: ' + err;
      } finally {
        if (state.requestSeq === requestSeq) {
          state.loading = false;
        }
        refreshPoolMeasuresView();
      }
    }

    async function refreshActivePoolMeasureDomains(forceRefresh) {
      const activeDomains = activePoolMeasureDomainKeys();
      if (!activeDomains.length) {
        refreshPoolMeasuresView();
        return;
      }
      for (const domainKey of activeDomains) {
        if (!poolMeasureDomainState[domainKey].active) continue;
        await loadPoolMeasureDomain(domainKey, !!forceRefresh);
      }
    }

    async function togglePoolMeasureDomain(domainKey) {
      const cleanDomain = normalizeRuntimeMeasureDomainKey(domainKey);
      if (!cleanDomain) return;
      const state = poolMeasureDomainState[cleanDomain];
      if (state.active) {
        state.active = false;
        state.loading = false;
        state.error = '';
        state.dashboardSlots = [];
        state.requestSeq += 1;
        refreshPoolMeasuresView();
        return;
      }
      state.active = true;
      await loadPoolMeasureDomain(cleanDomain, false);
    }

    async function refreshPoolMeasures(forceRefresh) {
      await refreshActivePoolMeasureDomains(!!forceRefresh);
    }

    async function onPoolMeasuresPageShown() {
      refreshPoolMeasuresView();
      startPoolMeasuresTimer();
      if (activePoolMeasureDomainKeys().length) {
        try {
          await refreshActivePoolMeasureDomains(false);
        } catch (err) {
          showPoolMeasuresError(err);
        }
      }
    }

    async function onUpgradePageShown() {
      renderUpgradeJourney(readUpgradeUiSession() || { phase: 'idle', target: '', detail: 'Aucune opération en cours.' });
      resumeUpgradeReconnectFlow();
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

    function cfgPathHasPrefix(pathValue, prefix) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      const cleanPrefix = nettoyerNomFlowCfg(prefix);
      if (!cleanPrefix) return true;
      return cleanPath === cleanPrefix || cleanPath.startsWith(cleanPrefix + '/');
    }

    function cfgDocPathCandidates(pathValue) {
      const cleanDisplay = nettoyerNomFlowCfg(pathValue);
      const candidates = [];
      const pushCandidate = (candidate) => {
        const cleanCandidate = nettoyerNomFlowCfg(candidate);
        if (!cleanCandidate) return;
        if (candidates.indexOf(cleanCandidate) >= 0) return;
        candidates.push(cleanCandidate);
      };

      let mappedStore = null;
      for (const alias of cfgTreeAliases) {
        if (!cfgPathHasPrefix(cleanDisplay, alias.display)) continue;
        if (!mappedStore || alias.display.length > mappedStore.display.length) {
          mappedStore = alias;
        }
      }

      if (mappedStore) {
        pushCandidate(mappedStore.store + cleanDisplay.slice(mappedStore.display.length));
      }

      pushCandidate(cleanDisplay);
      return candidates;
    }

    function cfgStorePathFromDisplayPath(pathValue) {
      const cleanDisplay = nettoyerNomFlowCfg(pathValue);
      if (!cleanDisplay) return '';
      for (const branch of cfgTreeVirtualBranches) {
        if (branch.display === cleanDisplay) return null;
      }
      const candidates = cfgDocPathCandidates(cleanDisplay);
      return candidates.length > 0 ? candidates[0] : cleanDisplay;
    }

    function cfgDisplayPathFromStorePath(pathValue) {
      const cleanStore = nettoyerNomFlowCfg(pathValue);
      if (!cleanStore) return '';
      let bestAlias = null;
      for (const alias of cfgTreeAliases) {
        if (!cfgPathHasPrefix(cleanStore, alias.store)) continue;
        if (!bestAlias || alias.store.length > bestAlias.store.length) {
          bestAlias = alias;
        }
      }
      if (!bestAlias) return cleanStore;
      return bestAlias.display + cleanStore.slice(bestAlias.store.length);
    }

    function cfgVirtualChildrenForDisplayPath(pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      for (const branch of cfgTreeVirtualBranches) {
        if (branch.display === cleanPath) {
          return branch.children.slice().sort((a, b) => a.localeCompare(b));
        }
      }
      return null;
    }

    function cfgChildTokenForDisplayPath(parentPath, childPath) {
      const cleanParent = nettoyerNomFlowCfg(parentPath);
      const cleanChild = nettoyerNomFlowCfg(childPath);
      if (!cleanChild) return '';
      if (!cleanParent) {
        const rootSegments = cleanChild.split('/');
        return rootSegments.length > 0 ? rootSegments[0] : '';
      }
      if (!cfgPathHasPrefix(cleanChild, cleanParent) || cleanChild === cleanParent) {
        return '';
      }
      const suffix = cleanChild.slice(cleanParent.length + 1);
      const slashIndex = suffix.indexOf('/');
      return slashIndex >= 0 ? suffix.slice(0, slashIndex) : suffix;
    }

    function cfgPathLabel(pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      if (!cleanPath) return 'Racine';
      const meta = configPathMeta(cleanPath);
      if (meta && typeof meta.label === 'string' && meta.label.length > 0) {
        return meta.label;
      }
      const segs = cleanPath.split('/');
      return segs[segs.length - 1] || cleanPath;
    }

    function flowCfgTitreDepuisChemin(pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      if (!cleanPath) return 'Racine';
      const segs = cleanPath.split('/');
      let prefix = '';
      return segs.map((seg) => {
        prefix = prefix ? (prefix + '/' + seg) : seg;
        return cfgPathLabel(prefix);
      }).join(' / ');
    }

    function cfgCacheKey(prefix) {
      const p = nettoyerNomFlowCfg(prefix);
      return p.length > 0 ? p : '__root__';
    }

    function cfgChildrenCacheForSource(source) {
      return source === 'supervisor' ? supCfgChildrenCache : flowCfgChildrenCache;
    }

    function cfgFilteredChildren(source, prefix) {
      const p = nettoyerNomFlowCfg(prefix);
      const node = cfgChildrenCacheForSource(source)[cfgCacheKey(p)];
      if (!node || !Array.isArray(node.children)) return [];
      return node.children
        .filter((name) => !isConfigPathHidden(p ? (p + '/' + name) : name))
        .slice();
    }

    function cfgExpandAncestors(source, pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      if (!cleanPath) return;
      const expandedSet = source === 'supervisor' ? supCfgExpandedNodes : flowCfgExpandedNodes;
      const segs = cleanPath.split('/');
      let prefix = '';
      for (let i = 0; i < segs.length; ++i) {
        prefix = prefix ? (prefix + '/' + segs[i]) : segs[i];
        expandedSet.add(prefix);
      }
    }

    function cfgNodeForPath(source, pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      return cfgChildrenCacheForSource(source)[cfgCacheKey(cleanPath)] || null;
    }

    async function chargerCfgChildren(source, prefix, forceReload) {
      const p = nettoyerNomFlowCfg(prefix);
      const cache = cfgChildrenCacheForSource(source);
      const key = cfgCacheKey(p);
      if (!forceReload && cache[key]) {
        return cache[key];
      }

      const virtualChildren = cfgVirtualChildrenForDisplayPath(p);
      if (virtualChildren) {
        const node = {
          prefix: p,
          hasExact: false,
          children: virtualChildren.filter((name) => !isConfigPathHidden(p ? (p + '/' + name) : name))
        };
        cache[key] = node;
        return node;
      }

      const storePrefix = cfgStorePathFromDisplayPath(p);
      const baseUrl = source === 'supervisor' ? '/api/supervisorcfg/children' : '/api/flowcfg/children';
      const url = storePrefix && storePrefix.length > 0
        ? (baseUrl + '?prefix=' + encodeURIComponent(storePrefix))
        : baseUrl;
      const fetchFn = source === 'supervisor' ? fetch : fetchFlowRemoteQueued;
      const res = await fetchFn(url, { cache: 'no-store' });
      const data = await res.json().catch(() => null);
      if (!res.ok || !data || data.ok !== true || !Array.isArray(data.children)) {
        const fallback = source === 'supervisor'
          ? 'liste enfants supervisor indisponible'
          : 'liste enfants indisponible';
        throw new Error(extractApiErrorMessage(data, fallback));
      }

      const children = data.children
        .filter((name) => typeof name === 'string' && name.length > 0)
        .map((name) => nettoyerNomFlowCfg(name))
        .filter((name) => name.length > 0)
        .map((child) => {
          const storeChildPath = storePrefix ? (storePrefix + '/' + child) : child;
          return cfgDisplayPathFromStorePath(storeChildPath);
        })
        .map((displayChildPath) => cfgChildTokenForDisplayPath(p, displayChildPath))
        .filter((name) => name.length > 0)
        .filter((name) => !isConfigPathHidden(p ? (p + '/' + name) : name));

      const node = {
        prefix: p,
        hasExact: !!data.has_exact,
        children: Array.from(new Set(children)).sort((a, b) => a.localeCompare(b))
      };
      cache[key] = node;
      return node;
    }

    async function ensureCfgPathLoaded(source, pathValue, forceReload) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      await chargerCfgChildren(source, '', !!forceReload);
      if (!cleanPath) return cfgNodeForPath(source, '');

      const segs = cleanPath.split('/');
      let prefix = '';
      let node = null;
      for (let i = 0; i < segs.length; ++i) {
        prefix = prefix ? (prefix + '/' + segs[i]) : segs[i];
        node = await chargerCfgChildren(source, prefix, !!forceReload);
      }
      return node;
    }

    function currentCfgTreePath(source) {
      return source === 'supervisor' ? nettoyerNomFlowCfg(supCfgTreePath) : cheminFlowCfgCourant();
    }

    function renderFlowCfgCurrentPath(source, pathValue, node) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      const childCount = cfgFilteredChildren(source, cleanPath).length;
      const level = cleanPath ? cleanPath.split('/').length : 0;
      const hasExact = !!(node && node.hasExact);
      const sourceLabel = source === 'supervisor' ? 'Config Store Supervisor' : 'Config Store Flow.io';

      flowCfgPathLabel.textContent = cleanPath ? (sourceLabel + ' / ' + flowCfgTitreDepuisChemin(cleanPath)) : sourceLabel;
      flowCfgPathLabel.setAttribute('aria-label', cleanPath ? ('Branche ' + cleanPath) : sourceLabel);
      flowCfgApplyBtn.textContent = source === 'supervisor' ? 'Appliquer localement' : 'Appliquer';

      if (!cleanPath) {
        flowCfgPathMeta.textContent = childCount > 0
          ? (childCount + ' branche(s) disponible(s) dans ' + sourceLabel + '.')
          : ('Aucune branche disponible dans ' + sourceLabel + '.');
        return;
      }

      const details = [];
      details.push('Niveau ' + level);
      if (hasExact) {
        details.push('variables configurables');
      }
      if (childCount > 0) {
        details.push(childCount + ' sous-branche(s)');
      }
      if (details.length === 0) {
        details.push('branche vide');
      }
      flowCfgPathMeta.textContent = details.join(' | ');
    }

    function buildFlowCfgTreeItem(source, pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      const label = cfgPathLabel(cleanPath);
      const cachedNode = cfgNodeForPath(source, cleanPath);
      const children = cfgFilteredChildren(source, cleanPath);
      const isExpanded = source === 'supervisor' ? supCfgExpandedNodes.has(cleanPath) : flowCfgExpandedNodes.has(cleanPath);
      const isSelected = source === cfgTreeSelectedSource && cleanPath === currentCfgTreePath(source);
      const hasKnownChildren = children.length > 0;
      const canExpand = !cachedNode || hasKnownChildren;

      const item = document.createElement('li');
      item.className = 'cfg-tree-item';
      item.setAttribute('role', 'treeitem');
      item.setAttribute('aria-expanded', canExpand ? String(isExpanded) : 'false');

      const row = document.createElement('div');
      row.className = 'cfg-tree-row' + (canExpand ? '' : ' is-leaf');

      const toggle = document.createElement('button');
      toggle.type = 'button';
      toggle.className = 'cfg-tree-toggle' + (isExpanded ? ' is-expanded' : '') + (canExpand ? '' : ' is-leaf');
      toggle.setAttribute('aria-label', canExpand ? ('Afficher ' + label) : (label + ' sans sous-branche'));
      if (canExpand) {
        const glyph = document.createElement('span');
        glyph.className = 'cfg-tree-toggle-glyph' + (isExpanded ? ' is-minus' : ' is-plus');
        glyph.textContent = isExpanded ? '-' : '+';
        toggle.appendChild(glyph);
      }
      toggle.disabled = !canExpand;
      if (canExpand) {
        toggle.addEventListener('click', async (event) => {
          event.stopPropagation();
          await toggleFlowCfgBranch(source, cleanPath);
        });
      }
      row.appendChild(toggle);

      const nodeBtn = document.createElement('button');
      nodeBtn.type = 'button';
      nodeBtn.className = 'cfg-tree-node'
        + (isSelected ? ' is-selected' : '')
        + (cachedNode && cachedNode.hasExact ? ' is-exact' : '');
      nodeBtn.setAttribute('aria-current', isSelected ? 'true' : 'false');
      nodeBtn.addEventListener('click', async () => {
        if (canExpand && isExpanded) {
          await toggleFlowCfgBranch(source, cleanPath);
          return;
        }
        await selectFlowCfgPath(source, cleanPath, false);
      });

      const nodeLabel = document.createElement('span');
      nodeLabel.className = 'cfg-tree-node-label';
      nodeLabel.textContent = label;
      nodeBtn.appendChild(nodeLabel);
      row.appendChild(nodeBtn);
      item.appendChild(row);

      if (isExpanded && hasKnownChildren) {
        const group = document.createElement('ul');
        group.className = 'cfg-tree-group is-nested';
        group.setAttribute('role', 'group');
        children.forEach((child) => {
          const childPath = cleanPath ? (cleanPath + '/' + child) : child;
          group.appendChild(buildFlowCfgTreeItem(source, childPath));
        });
        item.appendChild(group);
      }

      return item;
    }

    function buildCfgTreeRootItem(source, label, expanded, children) {
      const item = document.createElement('li');
      item.className = 'cfg-tree-item cfg-tree-item-root';
      item.setAttribute('role', 'treeitem');
      item.setAttribute('aria-expanded', children.length > 0 ? String(expanded) : 'false');

      const row = document.createElement('div');
      row.className = 'cfg-tree-row cfg-tree-root-row';

      const toggle = document.createElement('button');
      toggle.type = 'button';
      toggle.className = 'cfg-tree-toggle' + (expanded ? ' is-expanded' : '') + (children.length > 0 ? '' : ' is-leaf');
      if (children.length > 0) {
        const glyph = document.createElement('span');
        glyph.className = 'cfg-tree-toggle-glyph' + (expanded ? ' is-minus' : ' is-plus');
        glyph.textContent = expanded ? '-' : '+';
        toggle.appendChild(glyph);
      }
      toggle.disabled = children.length === 0;
      toggle.setAttribute('aria-label', expanded ? ('Replier ' + label) : ('Afficher ' + label));
      toggle.addEventListener('click', async (event) => {
        event.stopPropagation();
        if (source === 'flow') flowCfgRootExpanded = !flowCfgRootExpanded;
        else supCfgRootExpanded = !supCfgRootExpanded;
        renderFlowCfgTree();
      });
      row.appendChild(toggle);

      const labelBtn = document.createElement('button');
      labelBtn.type = 'button';
      labelBtn.className = 'cfg-tree-root-label' + ((cfgTreeSelectedSource === source && !currentCfgTreePath(source)) ? ' is-selected' : '');
      labelBtn.textContent = label;
      labelBtn.addEventListener('click', async () => {
        await selectFlowCfgPath(source, '', false);
      });
      row.appendChild(labelBtn);
      item.appendChild(row);

      if (expanded && children.length > 0) {
        const group = document.createElement('ul');
        group.className = 'cfg-tree-group cfg-tree-group-root';
        group.setAttribute('role', 'group');
        children.forEach((child) => {
          group.appendChild(buildFlowCfgTreeItem(source, child));
        });
        item.appendChild(group);
      }

      return item;
    }

    function renderFlowCfgTree() {
      const savedScrollTop = flowCfgTree.scrollTop;
      flowCfgTree.innerHTML = '';
      const flowChildren = cfgFilteredChildren('flow', '');
      const supervisorChildren = cfgFilteredChildren('supervisor', '');

      const roots = document.createElement('ul');
      roots.className = 'cfg-tree-group';
      roots.setAttribute('role', 'tree');
      roots.appendChild(buildCfgTreeRootItem('flow', 'Config Store Flow.io', flowCfgRootExpanded, flowChildren));
      roots.appendChild(buildCfgTreeRootItem('supervisor', 'Config Store Supervisor', supCfgRootExpanded, supervisorChildren));
      flowCfgTree.appendChild(roots);
      flowCfgTree.scrollTop = savedScrollTop;
    }

    function renderFlowCfgTreeSkeleton() {
      const savedScrollTop = flowCfgTree.scrollTop;
      flowCfgTree.innerHTML = '';
      const skeleton = document.createElement('div');
      skeleton.className = 'cfg-tree-skeleton';
      [100, 88, 92, 78, 84].forEach((width, index) => {
        const line = document.createElement('div');
        line.className = 'skeleton-line cfg-tree-skeleton-line' + (index > 1 ? ' is-indented' : '');
        line.style.width = width + '%';
        skeleton.appendChild(line);
      });
      flowCfgTree.appendChild(skeleton);
      flowCfgTree.scrollTop = savedScrollTop;
    }

    function restoreFlowCfgTreeScroll(scrollTop) {
      if (!flowCfgTree || !Number.isFinite(scrollTop)) return;
      flowCfgTree.scrollTop = scrollTop;
      requestAnimationFrame(() => {
        if (!flowCfgTree) return;
        flowCfgTree.scrollTop = scrollTop;
      });
    }

    async function toggleFlowCfgBranch(source, pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      const expandedSet = source === 'supervisor' ? supCfgExpandedNodes : flowCfgExpandedNodes;
      if (!cleanPath) return;
      if (expandedSet.has(cleanPath)) {
        expandedSet.delete(cleanPath);
        renderFlowCfgTree();
        return;
      }
      try {
        flowCfgStatus.textContent = 'Chargement des sous-branches...';
        await chargerCfgChildren(source, cleanPath, false);
        expandedSet.add(cleanPath);
        renderFlowCfgTree();
        flowCfgStatus.textContent = 'Sous-branches chargees.';
      } catch (err) {
        flowCfgStatus.textContent = 'Chargement des sous-branches echoue: ' + err;
      }
    }

    async function selectFlowCfgPath(source, pathValue, forceReload) {
      const preservedTreeScrollTop = flowCfgTree ? flowCfgTree.scrollTop : 0;
      beginFlowCfgLoading('Chargement de la configuration distante...', { tree: false, detail: true });
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      try {
        let node = null;
        const storePath = cfgStorePathFromDisplayPath(cleanPath);
        cfgTreeSelectedSource = source === 'supervisor' ? 'supervisor' : 'flow';
        if (cfgTreeSelectedSource === 'supervisor') {
          node = await ensureCfgPathLoaded('supervisor', cleanPath, !!forceReload);
          supCfgTreePath = cleanPath;
          supCfgRootExpanded = true;
          cfgExpandAncestors('supervisor', cleanPath);
          if (cleanPath && cfgFilteredChildren('supervisor', cleanPath).length > 0) {
            supCfgExpandedNodes.add(cleanPath);
          }
        } else {
          node = await ensureCfgPathLoaded('flow', cleanPath, !!forceReload);
          flowCfgPath = cleanPath ? cleanPath.split('/') : [];
          flowCfgRootExpanded = true;
          cfgExpandAncestors('flow', cleanPath);
          if (cleanPath && cfgFilteredChildren('flow', cleanPath).length > 0) {
            flowCfgExpandedNodes.add(cleanPath);
          }
        }

        renderFlowCfgCurrentPath(cfgTreeSelectedSource, cleanPath, node);
        renderFlowCfgTree();
        restoreFlowCfgTreeScroll(preservedTreeScrollTop);

        if (!cleanPath) {
          resetPrimaryCfgEditor(cfgFilteredChildren(cfgTreeSelectedSource, '').length > 0
            ? 'Sélectionnez une branche dans l\'arborescence.'
            : 'Aucune branche disponible.');
          return;
        }

        if (node && node.hasExact) {
          if (cfgTreeSelectedSource === 'supervisor') {
            await chargerPrimarySupervisorCfgModule(storePath || cleanPath);
          } else {
            await chargerFlowCfgModule(storePath || cleanPath);
          }
          return;
        }

        const childCount = cfgFilteredChildren(cfgTreeSelectedSource, cleanPath).length;
        if (childCount > 0) {
          resetPrimaryCfgEditor('Branche ouverte. Sélectionnez une sous-branche ou un noeud configurable.');
        } else {
          resetPrimaryCfgEditor('Aucune variable configurable dans cette branche.');
        }
      } catch (err) {
        renderFlowCfgCurrentPath(cfgTreeSelectedSource, cleanPath, null);
        renderFlowCfgTree();
        restoreFlowCfgTreeScroll(preservedTreeScrollTop);
        resetPrimaryCfgEditor('Chargement branche échoué: ' + err);
      } finally {
        endFlowCfgLoading({ tree: false, detail: true });
      }
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

    function beginFlowCfgLoading(statusText, options) {
      const opts = options || {};
      const loadTree = opts.tree !== false;
      const loadDetail = opts.detail !== false;

      if (loadTree) {
        flowCfgTreeLoadingDepth += 1;
        if (flowCfgTreeLoadingDepth === 1) {
          if (flowCfgTreePane) flowCfgTreePane.classList.add('is-loading');
          renderFlowCfgTreeSkeleton();
        }
      }
      if (loadDetail) {
        flowCfgDetailLoadingDepth += 1;
        if (flowCfgDetailLoadingDepth === 1) {
          if (flowCfgDetailPane) flowCfgDetailPane.classList.add('is-loading');
          renderFlowCfgFieldsSkeleton();
          flowCfgApplyBtn.disabled = true;
        }
      }
      if (loadTree || loadDetail) {
        flowCfgRefreshBtn.disabled = true;
      }
      if (statusText) {
        flowCfgStatus.textContent = statusText;
      }
    }

    function endFlowCfgLoading(options) {
      const opts = options || {};
      const loadTree = opts.tree !== false;
      const loadDetail = opts.detail !== false;

      if (loadTree && flowCfgTreeLoadingDepth > 0) {
        flowCfgTreeLoadingDepth -= 1;
        if (flowCfgTreeLoadingDepth === 0) {
          if (flowCfgTreePane) flowCfgTreePane.classList.remove('is-loading');
        }
      }
      if (loadDetail && flowCfgDetailLoadingDepth > 0) {
        flowCfgDetailLoadingDepth -= 1;
        if (flowCfgDetailLoadingDepth === 0) {
          if (flowCfgDetailPane) flowCfgDetailPane.classList.remove('is-loading');
        }
      }
      if (flowCfgTreeLoadingDepth === 0 && flowCfgDetailLoadingDepth === 0) {
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
        const res = await fetch(assetUrl('/webinterface/cfgdocs.fr.json'));
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
          const modsRes = await fetch(assetUrl('/webinterface/cfgmods.fr.json'));
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
        chargerCfgTreeMetaDepuisDocs();
      } catch (err) {
        cfgDocSources = [];
        cfgTreeAliases = [];
        cfgTreeVirtualBranches = [];
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

    function chargerCfgTreeMetaDepuisDocs() {
      const aliases = [];
      const virtualBranches = [];
      const seenAliasKeys = new Set();
      const seenBranchKeys = new Set();

      for (const src of cfgDocSources) {
        const normalized = normalizeDocSource(src);
        if (!normalized || !normalized.meta) continue;

        const aliasEntries = Array.isArray(normalized.meta.cfg_tree_aliases)
          ? normalized.meta.cfg_tree_aliases
          : [];
        aliasEntries.forEach((entry) => {
          const display = nettoyerNomFlowCfg(entry && entry.display);
          const store = nettoyerNomFlowCfg(entry && entry.store);
          if (!display || !store) return;
          const key = display + '->' + store;
          if (seenAliasKeys.has(key)) return;
          seenAliasKeys.add(key);
          aliases.push({ display: display, store: store });
        });

        const branchEntries = Array.isArray(normalized.meta.cfg_tree_virtual_branches)
          ? normalized.meta.cfg_tree_virtual_branches
          : [];
        branchEntries.forEach((entry) => {
          const display = nettoyerNomFlowCfg(entry && entry.display);
          if (!display || seenBranchKeys.has(display)) return;
          const children = Array.isArray(entry && entry.children)
            ? entry.children.map((child) => nettoyerNomFlowCfg(child)).filter((child) => child.length > 0)
            : [];
          seenBranchKeys.add(display);
          virtualBranches.push({ display: display, children: children });
        });
      }

      cfgTreeAliases = aliases;
      cfgTreeVirtualBranches = virtualBranches;
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

    function closeColorPickerPopover() {
      if (!activeColorPickerPopover) return;
      const state = activeColorPickerPopover;
      activeColorPickerPopover = null;
      if (state.outsideHandler) {
        document.removeEventListener('mousedown', state.outsideHandler, true);
      }
      if (state.keyHandler) {
        document.removeEventListener('keydown', state.keyHandler, true);
      }
      if (state.repositionHandler) {
        window.removeEventListener('resize', state.repositionHandler, true);
        window.removeEventListener('scroll', state.repositionHandler, true);
      }
      if (state.popover && state.popover.parentNode) {
        state.popover.parentNode.removeChild(state.popover);
      }
    }

    function enumOptionColor(enumOptions, value) {
      if (!Array.isArray(enumOptions)) return '';
      const currentValue = String(value ?? '');
      for (const opt of enumOptions) {
        if (!opt || typeof opt !== 'object') continue;
        if (String(opt.value) !== currentValue) continue;
        return (typeof opt.color === 'string') ? opt.color.trim() : '';
      }
      return '';
    }

    function positionColorPickerPopover(popover, anchorEl) {
      if (!popover || !anchorEl) return;
      const anchorRect = anchorEl.getBoundingClientRect();
      const popRect = popover.getBoundingClientRect();
      const margin = 12;
      let left = anchorRect.left + (anchorRect.width / 2) - (popRect.width / 2);
      let top = anchorRect.bottom + 10;
      left = Math.max(margin, Math.min(left, window.innerWidth - popRect.width - margin));
      if (top + popRect.height > window.innerHeight - margin) {
        top = Math.max(margin, anchorRect.top - popRect.height - 10);
      }
      popover.style.left = Math.round(left) + 'px';
      popover.style.top = Math.round(top) + 'px';
    }

    function updateColorTriggerVisual(trigger, enumOptions, value) {
      if (!trigger) return;
      const color = enumOptionColor(enumOptions, value) || '#FFFFFF';
      trigger.style.background = color;
      trigger.dataset.color = color;
      trigger.setAttribute('aria-label', 'Couleur ' + color);
      trigger.title = color;
    }

    function openColorPickerPopover(trigger, inputEl, enumOptions) {
      if (!trigger || !inputEl || !Array.isArray(enumOptions) || !enumOptions.length) return;
      closeColorPickerPopover();

      const popover = document.createElement('div');
      popover.className = 'color-picker-popover';
      popover.setAttribute('role', 'dialog');
      popover.setAttribute('aria-modal', 'false');

      const grid = document.createElement('div');
      grid.className = 'color-picker-grid';
      const currentValue = String(inputEl.value ?? '');
      enumOptions.forEach((opt) => {
        if (!opt || typeof opt !== 'object') return;
        const color = (typeof opt.color === 'string') ? opt.color.trim() : '';
        if (!color) return;
        const swatch = document.createElement('button');
        swatch.type = 'button';
        swatch.className = 'color-picker-swatch';
        swatch.style.background = color;
        swatch.dataset.color = color;
        swatch.setAttribute('aria-label', color);
        swatch.title = color;
        if (String(opt.value) === currentValue) {
          swatch.classList.add('is-selected');
        }
        swatch.addEventListener('click', () => {
          inputEl.value = String(opt.value);
          updateColorTriggerVisual(trigger, enumOptions, inputEl.value);
          inputEl.dispatchEvent(new Event('input', { bubbles: true }));
          inputEl.dispatchEvent(new Event('change', { bubbles: true }));
          closeColorPickerPopover();
        });
        grid.appendChild(swatch);
      });
      popover.appendChild(grid);
      document.body.appendChild(popover);
      positionColorPickerPopover(popover, trigger);

      const outsideHandler = (event) => {
        const target = event && event.target;
        if (popover.contains(target) || trigger.contains(target)) return;
        closeColorPickerPopover();
      };
      const keyHandler = (event) => {
        if (event && event.key === 'Escape') {
          closeColorPickerPopover();
        }
      };
      const repositionHandler = () => {
        if (!activeColorPickerPopover || activeColorPickerPopover.popover !== popover) return;
        positionColorPickerPopover(popover, trigger);
      };

      document.addEventListener('mousedown', outsideHandler, true);
      document.addEventListener('keydown', keyHandler, true);
      window.addEventListener('resize', repositionHandler, true);
      window.addEventListener('scroll', repositionHandler, true);
      activeColorPickerPopover = { popover, trigger, outsideHandler, keyHandler, repositionHandler };
    }

    function createColorPickerControl(doc, key, value, enumOptions) {
      const input = document.createElement('input');
      input.type = 'hidden';
      input.className = 'control-color-input';
      input.dataset.key = key;
      input.dataset.kind = configNumericKind(doc, value);
      input.dataset.label = (doc && typeof doc.label === 'string' && doc.label.length > 0) ? doc.label : key;
      input.value = String(value ?? '');
      storeConfigFieldInitialValue(input, value);

      const trigger = document.createElement('button');
      trigger.type = 'button';
      trigger.className = 'control-color-trigger';
      trigger.setAttribute('aria-haspopup', 'dialog');
      updateColorTriggerVisual(trigger, enumOptions, input.value);
      trigger.addEventListener('click', (event) => {
        event.preventDefault();
        event.stopPropagation();
        if (activeColorPickerPopover && activeColorPickerPopover.trigger === trigger) {
          closeColorPickerPopover();
          return;
        }
        openColorPickerPopover(trigger, input, enumOptions);
      });

      return { input, trigger };
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

    function parseConfigNumericValueDetailed(rawValue, kind, displayFormat) {
      if (displayFormat === 'hex') {
        if (typeof rawValue === 'number' && Number.isFinite(rawValue)) {
          return { ok: true, value: Math.max(0, Math.trunc(rawValue)) };
        }
        const raw = String(rawValue ?? '').trim();
        const normalized = raw.length > 0 ? raw : '0';
        if (!/^(?:0[xX])?[0-9A-Fa-f]+$/.test(normalized)) {
          return {
            ok: false,
            value: 0,
            error: 'utilisez une valeur hexadécimale valide, par exemple 0x77'
          };
        }
        const parsed = Number.parseInt(normalized, 16);
        if (!Number.isFinite(parsed)) {
          return {
            ok: false,
            value: 0,
            error: 'utilisez une valeur hexadécimale valide, par exemple 0x77'
          };
        }
        return { ok: true, value: parsed };
      }
      if (typeof rawValue === 'number' && Number.isFinite(rawValue)) {
        return { ok: true, value: kind === 'float' ? rawValue : Math.trunc(rawValue) };
      }
      const raw = String(rawValue ?? '').trim();
      if (kind === 'float') {
        const parsed = Number.parseFloat(raw);
        return { ok: true, value: Number.isFinite(parsed) ? parsed : 0 };
      }
      const parsed = Number.parseInt(raw, 10);
      return { ok: true, value: Number.isFinite(parsed) ? parsed : 0 };
    }

    function parseConfigNumericValue(rawValue, kind, displayFormat) {
      const parsed = parseConfigNumericValueDetailed(rawValue, kind, displayFormat);
      return parsed.value;
    }

    function setConfigFieldValidationState(inputEl, ok, message) {
      if (!inputEl) return ok;
      const row = inputEl.closest('.control-row');
      const validationMessage = ok ? '' : String(message || 'Valeur invalide');
      if (typeof inputEl.setCustomValidity === 'function') {
        inputEl.setCustomValidity(validationMessage);
      }
      if (ok) {
        inputEl.removeAttribute('aria-invalid');
        inputEl.removeAttribute('title');
      } else {
        inputEl.setAttribute('aria-invalid', 'true');
        inputEl.setAttribute('title', validationMessage);
      }
      if (row) {
        row.classList.toggle('is-invalid', !ok);
      }
      return ok;
    }

    function validateConfigFieldValue(inputEl, options) {
      const opts = options || {};
      if (!inputEl) return true;
      const kind = String(inputEl.dataset.kind || '').trim();
      const displayFormat = String(inputEl.dataset.format || '').trim();
      if ((kind !== 'int' && kind !== 'float') || displayFormat !== 'hex') {
        return setConfigFieldValidationState(inputEl, true, '');
      }
      const parsed = parseConfigNumericValueDetailed(inputEl.value, kind, displayFormat);
      const ok = setConfigFieldValidationState(inputEl, !!parsed.ok, parsed.error || '');
      if (!ok && !opts.silent && typeof inputEl.reportValidity === 'function') {
        inputEl.reportValidity();
      }
      return ok;
    }

    function readConfigFieldValueStrict(inputEl) {
      if (!inputEl) return null;
      const kind = String(inputEl.dataset.kind || '').trim();
      const displayFormat = String(inputEl.dataset.format || '').trim();
      if ((kind === 'int' || kind === 'float') && displayFormat === 'hex') {
        const parsed = parseConfigNumericValueDetailed(inputEl.value, kind, displayFormat);
        if (!parsed.ok) {
          validateConfigFieldValue(inputEl);
          const label = String(inputEl.dataset.label || inputEl.dataset.key || 'champ').trim();
          throw new Error(label + ' : ' + parsed.error);
        }
        return parsed.value;
      }
      return readConfigFieldValue(inputEl);
    }

    function updatePrimaryCfgApplyState() {
      if (!flowCfgApplyBtn) return;
      if (flowCfgApplyBtn.hidden) {
        flowCfgApplyBtn.disabled = true;
        flowCfgApplyBtn.removeAttribute('title');
        return;
      }
      const fields = flowCfgFields ? Array.from(flowCfgFields.querySelectorAll('[data-key]')) : [];
      let hasDirty = false;
      let hasInvalid = false;
      fields.forEach((el) => {
        if (!el || typeof el !== 'object') return;
        if (!validateConfigFieldValue(el, { silent: true })) {
          hasInvalid = true;
        }
        if (configFieldIsDirty(el)) {
          hasDirty = true;
        }
      });
      flowCfgApplyBtn.disabled = !hasDirty || hasInvalid;
      if (hasInvalid) {
        flowCfgApplyBtn.title = 'Corrigez les champs invalides avant application';
      } else if (!hasDirty) {
        flowCfgApplyBtn.title = 'Aucun changement a appliquer';
      } else {
        flowCfgApplyBtn.title = 'Appliquer les changements';
      }
    }

    function configNumericKind(doc, value) {
      const typeName = String((doc && doc.type) || '').trim().toLowerCase();
      if (typeName === 'float' || typeName === 'double') {
        return 'float';
      }
      if (typeName === 'int32' || typeName === 'uint16' || typeName === 'uint8') {
        return 'int';
      }
      if (typeof value === 'number') {
        return Number.isInteger(value) ? 'int' : 'float';
      }
      return 'string';
    }

    function configFieldNormalizedInitialValue(doc, value) {
      const numericKind = configNumericKind(doc, value);
      if (numericKind === 'int' || numericKind === 'float') {
        const displayFormat = (doc && typeof doc.display_format === 'string') ? doc.display_format : '';
        return parseConfigNumericValue(value, numericKind, displayFormat);
      }
      return value;
    }

    function configDocFor(moduleName, key, extraSources) {
      const k = String(key || '').trim();
      const candidates = cfgDocPathCandidates(moduleName);
      if (candidates.length === 0 || !k) return null;

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
        const wildcard = docs['*/' + k];
        if (wildcard && typeof wildcard === 'object') {
          merged = Object.assign(merged || {}, wildcard);
        }
        candidates.forEach((candidate) => {
          const exact = docs[candidate + '/' + k];
          if (exact && typeof exact === 'object') {
            merged = Object.assign(merged || {}, exact);
          }
        });
      }
      return enrichResolvedDoc(merged, sources);
    }

    function configPathMeta(pathValue) {
      const candidates = cfgDocPathCandidates(pathValue);
      if (candidates.length === 0) return null;
      const sources = [];
      for (const src of cfgDocSources) {
        const normalized = normalizeDocSource(src);
        if (!normalized) continue;
        sources.push(normalized);
      }
      let merged = null;
      for (const normalized of sources) {
        candidates.forEach((candidate) => {
          const exact = normalized.docs[candidate];
          if (exact && typeof exact === 'object') {
            merged = Object.assign(merged || {}, exact);
          }
        });
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

    function resetPrimaryCfgEditor(message) {
      flowCfgFields.innerHTML = '';
      flowCfgApplyBtn.hidden = false;
      flowCfgApplyBtn.disabled = true;
      if (message) {
        flowCfgStatus.textContent = message;
      }
    }

    function resetFlowCfgEditor(message) {
      flowCfgCurrentModule = '';
      flowCfgCurrentData = {};
      resetPrimaryCfgEditor(message);
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
      const valid = validateConfigFieldValue(inputEl, { silent: true });
      const dirty = configFieldIsDirty(inputEl);
      applyBtn.disabled = !dirty || !valid;
      applyBtn.classList.toggle('is-dirty', dirty && valid);
      applyBtn.classList.remove('is-pending');
      applyBtn.title = !valid
        ? 'Corrigez ce champ avant application'
        : (dirty ? 'Appliquer ce changement' : 'Aucun changement a appliquer');
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
        [key]: readConfigFieldValueStrict(inputEl)
      };
      return JSON.stringify(patch);
    }

    function renderConfigFields(containerEl, moduleName, dataObj, options) {
      const opts = options || {};
      closeColorPickerPopover();
      containerEl.innerHTML = '';
      const data = (dataObj && typeof dataObj === 'object') ? dataObj : {};
      const perFieldApply = !!opts.perFieldApply;
      const controlsPrimaryPane = !!opts.controlsPrimaryPane;
      const onApplyField = typeof opts.onApplyField === 'function' ? opts.onApplyField : null;
      if (controlsPrimaryPane) {
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
        if (controlsPrimaryPane && !perFieldApply) {
          updatePrimaryCfgApplyState();
        }
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
        } else if (enumOptions && enumOptions.length > 0 && enumOptions.some((opt) => opt && typeof opt.color === 'string' && opt.color.trim().length > 0)) {
          const colorControl = createColorPickerControl(doc, key, value, enumOptions);
          inputEl = colorControl.input;
          valueWrap.classList.add('control-value-wrap-color');
          valueWrap.appendChild(colorControl.input);
          valueWrap.appendChild(colorControl.trigger);
        } else if (enumOptions && enumOptions.length > 0) {
          const select = document.createElement('select');
          select.className = 'control-input';
          select.dataset.key = key;
          select.dataset.kind = configNumericKind(doc, value);
          if (doc && typeof doc.display_format === 'string') {
            select.dataset.format = doc.display_format;
          }
          const currentValue = String(value);
          let hasSelectedOption = false;
          enumOptions.forEach((opt) => {
            if (!opt || typeof opt !== 'object') return;
            const optionEl = document.createElement('option');
            optionEl.value = String(opt.value);
            optionEl.textContent = (typeof opt.label === 'string' && opt.label.length > 0)
              ? opt.label
              : String(opt.value);
            if (typeof opt.color === 'string' && opt.color.trim().length > 0) {
              optionEl.dataset.color = opt.color.trim();
            }
            if (optionEl.value === currentValue) {
              optionEl.selected = true;
              hasSelectedOption = true;
            }
            select.appendChild(optionEl);
          });
          if (!hasSelectedOption && currentValue.length > 0) {
            const placeholder = document.createElement('option');
            placeholder.value = currentValue;
            placeholder.textContent = 'Valeur non definie';
            placeholder.selected = true;
            select.insertBefore(placeholder, select.firstChild);
          }
          storeConfigFieldInitialValue(select, value);
          inputEl = select;
          valueWrap.appendChild(select);
        } else if (configNumericKind(doc, value) !== 'string') {
          const input = document.createElement('input');
          input.className = 'control-input';
          const displayFormat = (doc && typeof doc.display_format === 'string') ? doc.display_format : '';
          const numericKind = configNumericKind(doc, value);
          input.type = displayFormat === 'hex' ? 'text' : 'number';
          input.value = formatConfigValueForDisplay(
            configFieldNormalizedInitialValue(doc, value),
            displayFormat
          );
          if (displayFormat !== 'hex') {
            input.step = (numericKind === 'float') ? '0.001' : '1';
          }
          input.dataset.key = key;
          input.dataset.kind = numericKind;
          input.dataset.label = label.textContent || key;
          if (displayFormat) {
            input.dataset.format = displayFormat;
          }
          storeConfigFieldInitialValue(input, configFieldNormalizedInitialValue(doc, value));
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
          input.dataset.label = label.textContent || key;
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
            if (onApplyField) {
              await onApplyField(inputEl, applyBtn);
            }
          });

          const syncApplyState = () => updateControlFieldApplyState(inputEl, applyBtn);
          inputEl.addEventListener('input', syncApplyState);
          inputEl.addEventListener('change', syncApplyState);
          updateControlFieldApplyState(inputEl, applyBtn);
          valueWrap.appendChild(applyBtn);
        } else if (controlsPrimaryPane && inputEl) {
          const syncPrimaryState = () => {
            validateConfigFieldValue(inputEl, { silent: true });
            updatePrimaryCfgApplyState();
          };
          inputEl.addEventListener('input', syncPrimaryState);
          inputEl.addEventListener('change', syncPrimaryState);
          validateConfigFieldValue(inputEl, { silent: true });
        }

        row.appendChild(valueWrap);
        containerEl.appendChild(row);
      }

      if (controlsPrimaryPane && !perFieldApply) {
        updatePrimaryCfgApplyState();
      }
    }

    function renderFlowCfgFields(dataObj) {
      renderConfigFields(flowCfgFields, flowCfgCurrentModule, dataObj, {
        controlsPrimaryPane: true,
        perFieldApply: flowCfgApplyPerFieldEnabled(flowCfgCurrentModule),
        onApplyField: appliquerFlowCfgField
      });
    }

    function renderPrimarySupervisorCfgFields(dataObj) {
      renderConfigFields(flowCfgFields, supCfgCurrentModule, dataObj, {
        controlsPrimaryPane: true,
        perFieldApply: false
      });
    }

    function buildPatchJsonFromFields(fieldsContainer, moduleName) {
      if (!moduleName) throw new Error('branche non sélectionnée');
      const patch = {};
      const modulePatch = {};
      const fields = fieldsContainer.querySelectorAll('[data-key]');
      fields.forEach((el) => {
        const key = el.dataset.key;
        const kind = el.dataset.kind;
        if (!key || !kind) return;
        if (kind === 'bool') {
          modulePatch[key] = !!el.checked;
          return;
        }
        if (kind === 'int') {
          modulePatch[key] = readConfigFieldValueStrict(el);
          return;
        }
        if (kind === 'float') {
          modulePatch[key] = readConfigFieldValueStrict(el);
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

    function buildPrimaryCfgPatchJson() {
      if (cfgTreeSelectedSource === 'supervisor') {
        return buildPatchJsonFromFields(flowCfgFields, supCfgCurrentModule);
      }
      return buildPatchJsonFromFields(flowCfgFields, flowCfgCurrentModule);
    }

    async function chargerFlowCfgModule(moduleName) {
      beginFlowCfgLoading('Chargement de la branche distante...', { tree: false, detail: true });
      const m = nettoyerNomFlowCfg(moduleName);
      try {
        if (!m) {
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
        renderFlowCfgFields(flowCfgCurrentData);
        updatePrimaryCfgApplyState();
        flowCfgStatus.textContent = data.truncated
          ? 'Branche chargée (tronquée, buffer distant atteint).'
          : 'Branche chargée.';
      } catch (err) {
        resetFlowCfgEditor('Chargement branche échoué: ' + err);
      } finally {
        endFlowCfgLoading({ tree: false, detail: true });
      }
    }

    async function chargerPrimarySupervisorCfgModule(moduleName) {
      beginFlowCfgLoading('Chargement de la branche supervisor...', { tree: false, detail: true });
      const m = nettoyerNomFlowCfg(moduleName);
      try {
        if (!m) {
          supCfgCurrentModule = '';
          supCfgCurrentData = {};
          resetPrimaryCfgEditor('Aucune branche supervisor sélectionnée.');
          return;
        }
        const res = await fetch('/api/supervisorcfg/module?name=' + encodeURIComponent(m), { cache: 'no-store' });
        const data = await res.json();
        if (!res.ok || !data || data.ok !== true || typeof data.data !== 'object') {
          throw new Error('lecture module supervisor impossible');
        }
        supCfgCurrentModule = m;
        supCfgCurrentData = data.data;
        renderPrimarySupervisorCfgFields(supCfgCurrentData);
        updatePrimaryCfgApplyState();
        flowCfgStatus.textContent = data.truncated
          ? 'Branche supervisor chargée (tronquée, buffer atteint).'
          : 'Branche supervisor chargée.';
      } catch (err) {
        supCfgCurrentModule = '';
        supCfgCurrentData = {};
        resetPrimaryCfgEditor('Chargement branche supervisor échoué: ' + err);
      } finally {
        endFlowCfgLoading({ tree: false, detail: true });
      }
    }

    function markCfgSourceUnavailable(source) {
      const emptyRootNode = {
        prefix: '',
        hasExact: false,
        children: []
      };
      if (source === 'supervisor') {
        supCfgChildrenCache = { [cfgCacheKey('')]: emptyRootNode };
        supCfgExpandedNodes = new Set();
        supCfgTreePath = '';
        supCfgCurrentModule = '';
        supCfgCurrentData = {};
        return;
      }
      flowCfgChildrenCache = { [cfgCacheKey('')]: emptyRootNode };
      flowCfgExpandedNodes = new Set();
      flowCfgPath = [];
      flowCfgCurrentModule = '';
      flowCfgCurrentData = {};
    }

    function formatCfgLoadStatus(result, finalMessage) {
      if (result && !result.flowLoaded && result.supervisorLoaded) {
        return finalMessage
          ? 'Flow.io indisponible pour le moment. Configuration Supervisor disponible. Nouvelle tentative automatique...'
          : 'Configuration Supervisor disponible. Nouvelle tentative pour Flow.io.';
      }
      if (result && result.flowLoaded && !result.supervisorLoaded) {
        return finalMessage
          ? 'Configuration Flow.io disponible. Configuration Supervisor indisponible. Nouvelle tentative automatique...'
          : 'Configuration Flow.io disponible. Nouvelle tentative pour Supervisor.';
      }
      return finalMessage
        ? 'Flow.io indisponible pour le moment. Nouvelle tentative automatique...'
        : 'Flow.io se prépare... nouvelle tentative.';
    }

    async function chargerFlowCfgModules(forceReload) {
      const force = !!forceReload;
      if (force) {
        flowCfgChildrenCache = {};
        flowCfgExpandedNodes = new Set();
        supCfgChildrenCache = {};
        supCfgExpandedNodes = new Set();
      }

      const rootLoads = await Promise.allSettled([
        ensureCfgPathLoaded('flow', '', force),
        ensureCfgPathLoaded('supervisor', '', force)
      ]);
      const flowLoaded = rootLoads[0].status === 'fulfilled';
      const supervisorLoaded = rootLoads[1].status === 'fulfilled';

      if (!flowLoaded) {
        markCfgSourceUnavailable('flow');
      }
      if (!supervisorLoaded) {
        markCfgSourceUnavailable('supervisor');
      }

      const currentSource = cfgTreeSelectedSource === 'supervisor' ? 'supervisor' : 'flow';
      let nextSource = currentSource;
      if (currentSource === 'flow' && !flowLoaded && supervisorLoaded) {
        nextSource = 'supervisor';
      } else if (currentSource === 'supervisor' && !supervisorLoaded && flowLoaded) {
        nextSource = 'flow';
      }

      const nextPath = nextSource === currentSource
        ? nettoyerNomFlowCfg(currentCfgTreePath(currentSource))
        : '';
      const selectableSource = nextSource === 'flow'
        ? (flowLoaded ? 'flow' : (supervisorLoaded ? 'supervisor' : ''))
        : (supervisorLoaded ? 'supervisor' : (flowLoaded ? 'flow' : ''));

      if (selectableSource) {
        await selectFlowCfgPath(selectableSource, selectableSource === nextSource ? nextPath : '', force);
      } else {
        cfgTreeSelectedSource = 'flow';
        renderFlowCfgCurrentPath('flow', '', null);
        renderFlowCfgTree();
        resetPrimaryCfgEditor('Aucune branche disponible.');
      }

      return {
        ok: flowLoaded && supervisorLoaded,
        flowLoaded,
        supervisorLoaded
      };
    }

    async function ensureFlowCfgLoaded(forceReload) {
      const force = !!forceReload;
      if (force) {
        flowCfgFlowOnlyFailureStreak = 0;
      }
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
        let loadResult = { ok: false, flowLoaded: false, supervisorLoaded: false };
        for (let attempt = 0; attempt < retryDelaysMs.length; ++attempt) {
          if (retryDelaysMs[attempt] > 0) {
            await waitMs(retryDelaysMs[attempt]);
          }
          loadResult = await chargerFlowCfgModules(force || attempt > 0);
          if (loadResult.ok) {
            flowCfgLoadedOnce = true;
            flowCfgFlowOnlyFailureStreak = 0;
            stopFlowCfgRetry();
            return;
          }
          if (loadResult.supervisorLoaded && !loadResult.flowLoaded) {
            break;
          }
          if (attempt + 1 < retryDelaysMs.length) {
            flowCfgStatus.textContent = formatCfgLoadStatus(loadResult, false);
          }
        }

        if (isPageActive('page-control')) {
          if (loadResult.supervisorLoaded && !loadResult.flowLoaded) {
            flowCfgFlowOnlyFailureStreak += 1;
            const retryDelayMs = Math.min(60000, 7000 + ((flowCfgFlowOnlyFailureStreak - 1) * 5000));
            if (flowCfgFlowOnlyFailureStreak >= 6) {
              stopFlowCfgRetry();
              flowCfgStatus.textContent =
                'Flow.io indisponible (lien I2C). Configuration Supervisor disponible. ' +
                'Auto-retry en pause, utilisez Rafraîchir.';
            } else {
              flowCfgStatus.textContent =
                'Flow.io indisponible pour le moment. Configuration Supervisor disponible. ' +
                'Nouvelle tentative dans ' + Math.max(1, Math.round(retryDelayMs / 1000)) + ' s.';
              scheduleFlowCfgRetry(retryDelayMs);
            }
            return;
          }
          flowCfgFlowOnlyFailureStreak = 0;
          flowCfgStatus.textContent = formatCfgLoadStatus(loadResult, true);
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
        return 'Flow.io a refusé la configuration (' + (where || 'flowcfg') + ').';
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
        flowCfgStatus.textContent = 'Configuration appliquée sur Flow.io.';
        await chargerFlowCfgModule(flowCfgCurrentModule);
      } catch (err) {
        flowCfgStatus.textContent = 'Application cfg échouée: ' + err;
      }
    }

    async function appliquerPrimaryCfg() {
      if (cfgTreeSelectedSource === 'supervisor') {
        try {
          const patch = buildPrimaryCfgPatchJson();
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
          flowCfgStatus.textContent = 'Configuration supervisor appliquée.';
          await chargerPrimarySupervisorCfgModule(supCfgCurrentModule);
        } catch (err) {
          flowCfgStatus.textContent = 'Application cfg supervisor échouée: ' + err;
        }
        return;
      }
      await appliquerFlowCfg();
    }

    function setFlowCfgBackupStatus(message, tone) {
      if (!flowCfgBackupStatus) return;
      flowCfgBackupStatus.textContent = String(message || '').trim() || 'Sauvegarde configuration prête.';
      flowCfgBackupStatus.classList.remove('is-ok', 'is-error', 'is-busy');
      if (tone === 'ok') flowCfgBackupStatus.classList.add('is-ok');
      if (tone === 'error') flowCfgBackupStatus.classList.add('is-error');
      if (tone === 'busy') flowCfgBackupStatus.classList.add('is-busy');
    }

    function setFlowCfgBackupBusy(busy) {
      flowCfgBackupBusy = !!busy;
      if (flowCfgExportBtn) flowCfgExportBtn.disabled = flowCfgBackupBusy;
      if (flowCfgImportBtn) flowCfgImportBtn.disabled = flowCfgBackupBusy;
      if (flowCfgImportFileInput) flowCfgImportFileInput.disabled = flowCfgBackupBusy;
    }

    function flowCfgBackupStoreLabel(storeName) {
      return storeName === 'supervisor' ? 'Supervisor' : 'Flow.io';
    }

    function flowCfgBackupStoreFetchImpl(storeName) {
      return storeName === 'supervisor' ? fetch : fetchFlowRemoteQueued;
    }

    function flowCfgBackupStoreBasePath(storeName) {
      return storeName === 'supervisor' ? '/api/supervisorcfg' : '/api/flowcfg';
    }

    function flowCfgBackupIsoDateForFile(dateLike) {
      const d = dateLike instanceof Date ? dateLike : new Date();
      const pad = (value) => String(value).padStart(2, '0');
      return d.getUTCFullYear()
        + pad(d.getUTCMonth() + 1)
        + pad(d.getUTCDate())
        + '-'
        + pad(d.getUTCHours())
        + pad(d.getUTCMinutes())
        + pad(d.getUTCSeconds());
    }

    function flowCfgBackupDownloadText(filename, textContent) {
      const blob = new Blob([textContent], { type: 'application/json;charset=utf-8' });
      const url = URL.createObjectURL(blob);
      const anchor = document.createElement('a');
      anchor.href = url;
      anchor.download = filename;
      anchor.rel = 'noopener';
      document.body.appendChild(anchor);
      anchor.click();
      setTimeout(() => {
        document.body.removeChild(anchor);
        URL.revokeObjectURL(url);
      }, 0);
    }

    function flowCfgBackupShouldRedactField(key, value) {
      const normalizedKey = String(key || '').trim().toLowerCase();
      if (!normalizedKey) return false;
      if (/pass|token|secret|api[_-]?key/.test(normalizedKey)) return true;
      if (typeof value === 'string' && value.trim() === '***') return true;
      return false;
    }

    function flowCfgBackupRedactModuleData(storeName, moduleName, moduleData) {
      const result = {};
      const redactedFields = [];
      const source = (moduleData && typeof moduleData === 'object' && !Array.isArray(moduleData)) ? moduleData : {};
      Object.keys(source).sort().forEach((key) => {
        const value = source[key];
        if (flowCfgBackupShouldRedactField(key, value)) {
          result[key] = flowCfgBackupRedactedToken;
          redactedFields.push({
            module: moduleName,
            key: key,
            reason: 'secret',
            store: storeName
          });
          return;
        }
        result[key] = value;
      });
      return { data: result, redactedFields };
    }

    async function flowCfgBackupFetchModules(storeName) {
      const basePath = flowCfgBackupStoreBasePath(storeName);
      const fetchImpl = flowCfgBackupStoreFetchImpl(storeName);
      const data = await fetchOkJson(
        basePath + '/modules',
        { cache: 'no-store' },
        'liste modules ' + flowCfgBackupStoreLabel(storeName) + ' indisponible',
        fetchImpl
      );
      if (!Array.isArray(data.modules)) {
        throw new Error('liste modules ' + flowCfgBackupStoreLabel(storeName) + ' invalide');
      }
      return data.modules
        .filter((moduleName) => typeof moduleName === 'string' && moduleName.trim().length > 0)
        .map((moduleName) => moduleName.trim())
        .sort((left, right) => left.localeCompare(right));
    }

    async function flowCfgBackupFetchModule(storeName, moduleName) {
      const basePath = flowCfgBackupStoreBasePath(storeName);
      const fetchImpl = flowCfgBackupStoreFetchImpl(storeName);
      const data = await fetchOkJson(
        basePath + '/module?name=' + encodeURIComponent(moduleName),
        { cache: 'no-store' },
        'lecture module ' + moduleName + ' (' + flowCfgBackupStoreLabel(storeName) + ') impossible',
        fetchImpl
      );
      if (!data || typeof data.data !== 'object' || Array.isArray(data.data)) {
        throw new Error('module ' + moduleName + ' invalide (' + flowCfgBackupStoreLabel(storeName) + ')');
      }
      return {
        data: data.data,
        truncated: !!data.truncated
      };
    }

    function flowCfgBackupValidatePrimitiveValue(value, moduleName, key) {
      if (typeof value === 'string' || typeof value === 'number' || typeof value === 'boolean') return;
      throw new Error('Valeur invalide pour ' + moduleName + '.' + key + ' (type non supporté).');
    }

    function flowCfgBackupNormalizeStoreSection(rawStore, storeName) {
      const store = rawStore && typeof rawStore === 'object' ? rawStore : {};
      const rawModules = (store.modules && typeof store.modules === 'object' && !Array.isArray(store.modules))
        ? store.modules
        : {};
      const modules = {};
      Object.keys(rawModules).forEach((moduleName) => {
        const cleanModuleName = String(moduleName || '').trim();
        if (!cleanModuleName) return;
        const rawModuleData = rawModules[moduleName];
        if (!rawModuleData || typeof rawModuleData !== 'object' || Array.isArray(rawModuleData)) {
          throw new Error('Module invalide dans backup: ' + cleanModuleName + ' (' + flowCfgBackupStoreLabel(storeName) + ').');
        }
        const normalizedData = {};
        Object.keys(rawModuleData).forEach((key) => {
          const cleanKey = String(key || '').trim();
          if (!cleanKey) return;
          const value = rawModuleData[key];
          flowCfgBackupValidatePrimitiveValue(value, cleanModuleName, cleanKey);
          normalizedData[cleanKey] = value;
        });
        modules[cleanModuleName] = normalizedData;
      });

      const truncatedModules = Array.isArray(store.truncated_modules)
        ? store.truncated_modules
            .filter((moduleName) => typeof moduleName === 'string' && moduleName.trim().length > 0)
            .map((moduleName) => moduleName.trim())
        : [];

      const redactedFields = Array.isArray(store.redacted_fields)
        ? store.redacted_fields
            .filter((entry) => entry && typeof entry === 'object')
            .map((entry) => ({
              module: String(entry.module || '').trim(),
              key: String(entry.key || '').trim()
            }))
            .filter((entry) => entry.module.length > 0 && entry.key.length > 0)
        : [];

      return {
        modules,
        truncated_modules: truncatedModules,
        redacted_fields: redactedFields
      };
    }

    function flowCfgBackupValidateDocument(parsedDoc) {
      if (!parsedDoc || typeof parsedDoc !== 'object') {
        throw new Error('Backup invalide (objet JSON attendu).');
      }
      if (String(parsedDoc.format || '').trim() !== flowCfgBackupFormat) {
        throw new Error('Backup invalide (format non reconnu).');
      }
      if (Number(parsedDoc.version) !== flowCfgBackupVersion) {
        throw new Error('Backup invalide (version non supportée).');
      }
      const stores = (parsedDoc.stores && typeof parsedDoc.stores === 'object') ? parsedDoc.stores : null;
      if (!stores) {
        throw new Error('Backup invalide (stores absent).');
      }
      return {
        format: flowCfgBackupFormat,
        version: flowCfgBackupVersion,
        stores: {
          supervisor: flowCfgBackupNormalizeStoreSection(stores.supervisor, 'supervisor'),
          flow: flowCfgBackupNormalizeStoreSection(stores.flow, 'flow')
        }
      };
    }

    function flowCfgBackupBuildRedactedFieldSet(redactedFields) {
      const set = new Set();
      (Array.isArray(redactedFields) ? redactedFields : []).forEach((entry) => {
        const moduleName = String(entry && entry.module ? entry.module : '').trim();
        const key = String(entry && entry.key ? entry.key : '').trim();
        if (!moduleName || !key) return;
        set.add(moduleName + '\u0000' + key);
      });
      return set;
    }

    function flowCfgBackupBuildModulePatch(moduleName, moduleData, redactedFieldSet) {
      const patch = {};
      const source = (moduleData && typeof moduleData === 'object' && !Array.isArray(moduleData)) ? moduleData : {};
      Object.keys(source).sort().forEach((key) => {
        const value = source[key];
        if (value === flowCfgBackupRedactedToken) return;
        if (redactedFieldSet && redactedFieldSet.has(moduleName + '\u0000' + key)) return;
        patch[key] = value;
      });
      return patch;
    }

    function flowCfgBackupSplitModulePatch(moduleName, modulePatch, maxBytes) {
      const keys = Object.keys(modulePatch || {}).sort();
      if (!keys.length) return [];

      const chunks = [];
      let current = {};
      keys.forEach((key) => {
        const next = Object.assign({}, current, { [key]: modulePatch[key] });
        const nextPatch = { [moduleName]: next };
        if (utf8ByteLength(JSON.stringify(nextPatch)) <= maxBytes) {
          current = next;
          return;
        }

        if (Object.keys(current).length === 0) {
          throw new Error('Champ trop volumineux pour import: ' + moduleName + '.' + key);
        }

        chunks.push({ [moduleName]: current });
        current = { [key]: modulePatch[key] };
        const singlePatch = { [moduleName]: current };
        if (utf8ByteLength(JSON.stringify(singlePatch)) > maxBytes) {
          throw new Error('Champ trop volumineux pour import: ' + moduleName + '.' + key);
        }
      });

      if (Object.keys(current).length > 0) {
        chunks.push({ [moduleName]: current });
      }
      return chunks;
    }

    async function flowCfgBackupApplyPatch(storeName, patchJson) {
      const basePath = flowCfgBackupStoreBasePath(storeName);
      const fetchImpl = flowCfgBackupStoreFetchImpl(storeName);
      const response = await fetchJsonResponse(
        basePath + '/apply',
        createFormPostOptions({ patch: patchJson }),
        fetchImpl
      );
      if (!response.res.ok || !response.data || response.data.ok !== true) {
        if (storeName === 'flow') {
          throw new Error(formatFlowCfgApplyError(response.data));
        }
        throw new Error(extractApiErrorMessage(response.data, 'apply refusé'));
      }
    }

    async function exportFlowCfgBackup() {
      if (flowCfgBackupBusy) return;
      setFlowCfgBackupBusy(true);
      const startedAt = Date.now();
      try {
        setFlowCfgBackupStatus('Préparation de l\'export ConfigStore...', 'busy');
        const createdAt = new Date();
        const backupDoc = {
          format: flowCfgBackupFormat,
          version: flowCfgBackupVersion,
          created_at_utc: createdAt.toISOString(),
          meta: {
            supervisor_fw: supervisorFirmwareVersion || '-',
            flow_reachable: false
          },
          stores: {
            supervisor: {
              modules: {},
              truncated_modules: [],
              redacted_fields: []
            },
            flow: {
              modules: {},
              truncated_modules: [],
              redacted_fields: []
            }
          }
        };

        const stores = ['supervisor', 'flow'];
        for (const storeName of stores) {
          const storeLabel = flowCfgBackupStoreLabel(storeName);
          setFlowCfgBackupStatus('Lecture des modules ' + storeLabel + '...', 'busy');
          const modules = await flowCfgBackupFetchModules(storeName);
          for (let i = 0; i < modules.length; i += 1) {
            const moduleName = modules[i];
            setFlowCfgBackupStatus(
              'Export ' + storeLabel + ' : ' + moduleName + ' (' + (i + 1) + '/' + modules.length + ')...',
              'busy'
            );
            const modulePayload = await flowCfgBackupFetchModule(storeName, moduleName);
            if (modulePayload.truncated) {
              backupDoc.stores[storeName].truncated_modules.push(moduleName);
            }
            const redacted = flowCfgBackupRedactModuleData(storeName, moduleName, modulePayload.data);
            backupDoc.stores[storeName].modules[moduleName] = redacted.data;
            redacted.redactedFields.forEach((entry) => {
              backupDoc.stores[storeName].redacted_fields.push({
                module: entry.module,
                key: entry.key
              });
            });
          }
          backupDoc.stores[storeName].module_count = modules.length;
          if (storeName === 'flow') {
            backupDoc.meta.flow_reachable = true;
          }
        }

        const truncatedErrors = []
          .concat((backupDoc.stores.supervisor.truncated_modules || []).map((moduleName) => 'Supervisor/' + moduleName))
          .concat((backupDoc.stores.flow.truncated_modules || []).map((moduleName) => 'Flow.io/' + moduleName));
        if (truncatedErrors.length > 0) {
          throw new Error(
            'export interrompu: modules tronqués (' + truncatedErrors.join(', ') + ').'
          );
        }

        const serialized = JSON.stringify(backupDoc, null, 2);
        const fileName = 'flowio-configstore-backup-' + flowCfgBackupIsoDateForFile(createdAt) + '.json';
        flowCfgBackupDownloadText(fileName, serialized);
        const durationMs = Date.now() - startedAt;
        setFlowCfgBackupStatus(
          'Export terminé (' + fileName + ', ' + Math.max(1, Math.round(durationMs / 1000)) + ' s).',
          'ok'
        );
      } catch (err) {
        setFlowCfgBackupStatus('Export échoué: ' + err, 'error');
      } finally {
        setFlowCfgBackupBusy(false);
      }
    }

    async function importFlowCfgBackupFromText(rawText, fileName) {
      if (flowCfgBackupBusy) return;
      setFlowCfgBackupBusy(true);
      const startedAt = Date.now();
      try {
        let parsedDoc = null;
        try {
          parsedDoc = JSON.parse(String(rawText || ''));
        } catch (err) {
          throw new Error('JSON invalide.');
        }
        const backupDoc = flowCfgBackupValidateDocument(parsedDoc);
        if (!confirm('Confirmer l\'import du backup "' + (fileName || 'inconnu') + '" ?')) {
          setFlowCfgBackupStatus('Import annulé.', '');
          return;
        }

        const report = {
          supervisor: { modules_applied: 0, modules_skipped: 0, patches_applied: 0 },
          flow: { modules_applied: 0, modules_skipped: 0, patches_applied: 0 }
        };

        const stores = ['supervisor', 'flow'];
        for (const storeName of stores) {
          const storeData = backupDoc.stores[storeName];
          const storeLabel = flowCfgBackupStoreLabel(storeName);
          const moduleNames = Object.keys(storeData.modules || {}).sort((left, right) => left.localeCompare(right));
          const truncatedSet = new Set(storeData.truncated_modules || []);
          const redactedSet = flowCfgBackupBuildRedactedFieldSet(storeData.redacted_fields);

          for (let moduleIndex = 0; moduleIndex < moduleNames.length; moduleIndex += 1) {
            const moduleName = moduleNames[moduleIndex];
            if (truncatedSet.has(moduleName)) {
              report[storeName].modules_skipped += 1;
              continue;
            }

            const modulePatch = flowCfgBackupBuildModulePatch(
              moduleName,
              storeData.modules[moduleName],
              redactedSet
            );
            if (Object.keys(modulePatch).length === 0) {
              report[storeName].modules_skipped += 1;
              continue;
            }

            const chunkPatches = flowCfgBackupSplitModulePatch(
              moduleName,
              modulePatch,
              flowCfgBackupPatchTargetBytes
            );

            for (let chunkIndex = 0; chunkIndex < chunkPatches.length; chunkIndex += 1) {
              setFlowCfgBackupStatus(
                'Import ' + storeLabel + ' : ' + moduleName
                + ' (' + (moduleIndex + 1) + '/' + moduleNames.length + ', patch ' + (chunkIndex + 1)
                + '/' + chunkPatches.length + ')...',
                'busy'
              );
              await flowCfgBackupApplyPatch(storeName, JSON.stringify(chunkPatches[chunkIndex]));
              report[storeName].patches_applied += 1;
            }

            report[storeName].modules_applied += 1;
          }
        }

        await ensureFlowCfgLoaded(true).catch(() => {});
        const durationMs = Date.now() - startedAt;
        setFlowCfgBackupStatus(
          'Import terminé (' + Math.max(1, Math.round(durationMs / 1000)) + ' s). '
            + 'Supervisor: ' + report.supervisor.modules_applied + ' module(s), '
            + report.supervisor.patches_applied + ' patch(s). '
            + 'Flow.io: ' + report.flow.modules_applied + ' module(s), '
            + report.flow.patches_applied + ' patch(s).',
          'ok'
        );
      } catch (err) {
        setFlowCfgBackupStatus('Import échoué: ' + err, 'error');
      } finally {
        setFlowCfgBackupBusy(false);
        if (flowCfgImportFileInput) {
          flowCfgImportFileInput.value = '';
        }
      }
    }

    async function importFlowCfgBackupFromFile(file) {
      if (!file) return;
      const text = await file.text();
      await importFlowCfgBackupFromText(text, file.name || 'backup.json');
    }

    async function callSystemAction(target, action) {
      let endpoint = '/api/system/reboot';
      if (target === 'flow' && action === 'reboot') endpoint = '/api/flow/system/reboot';
      else if (target === 'nextion' && action === 'reboot') endpoint = '/api/system/nextion/reboot';
      else if (target === 'flow' && action === 'factory_reset') endpoint = '/api/flow/system/factory-reset';
      else if (target === 'supervisor' && action === 'factory_reset') endpoint = '/api/system/factory-reset';
      await fetchOkJson(endpoint, { method: 'POST' }, 'échec action', target === 'flow' ? fetchFlowRemoteQueued : fetch);
      if (target === 'flow' && action === 'factory_reset') {
        systemStatusText.textContent = 'Reset Flow.io en cours';
      } else if (target === 'flow' && action === 'reboot') {
        systemStatusText.textContent = 'Redémarrage Flow.io';
      } else if (target === 'nextion' && action === 'reboot') {
        systemStatusText.textContent = 'Redémarrage Nextion';
      } else if (target === 'supervisor' && action === 'factory_reset') {
        systemStatusText.textContent = 'Reset superviseur en cours';
      } else {
        systemStatusText.textContent = 'Redémarrage superviseur';
      }
    }

    function clearPendingSystemAction(button) {
      if (!button) return;
      const pending = pendingSystemActionCountdowns.get(button);
      if (pending && pending.timer) {
        clearTimeout(pending.timer);
      }
      pendingSystemActionCountdowns.delete(button);
      button.disabled = false;
      button.textContent = 'Redémarrer';
    }

    function startDelayedSystemAction(button, countdownLabel, actionRunner, failurePrefix) {
      if (!button || typeof actionRunner !== 'function') return;
      clearPendingSystemAction(button);

      let remaining = rebootActionDelaySeconds;
      button.disabled = true;

      const tick = () => {
        button.textContent = remaining + ' s';
        systemStatusText.textContent = countdownLabel + ' dans ' + remaining + ' s';

        if (remaining <= 1) {
          pendingSystemActionCountdowns.delete(button);
          runAsyncTaskSafely(async () => {
            button.textContent = '...';
            try {
              await actionRunner();
            } catch (err) {
              clearPendingSystemAction(button);
              systemStatusText.textContent = failurePrefix;
              return;
            }
            button.textContent = 'Redémarrer';
            button.disabled = false;
          });
          return;
        }

        remaining -= 1;
        const timer = setTimeout(tick, 1000);
        pendingSystemActionCountdowns.set(button, { timer });
      };

      tick();
    }

    function initUpgradeBindings() {
      upgradeConfigFieldDefs.forEach((def) => {
        if (!def || !def.input || !def.button) return;
        updateUpgradeConfigFieldApplyState(def);
        def.input.addEventListener('input', () => updateUpgradeConfigFieldApplyState(def));
        def.input.addEventListener('change', () => updateUpgradeConfigFieldApplyState(def));
        bindClickAction(def.button, async () => {
          try {
            await applyUpgradeConfigField(def);
          } catch (err) {
            setUpgradeMessage('Échec de l\'enregistrement : ' + err);
          }
        });
      });
      bindClickAction(upSupervisorBtn, () => startUpgrade('supervisor'));
      bindClickAction(upFlowBtn, () => startUpgrade('flowio'));
      bindClickAction(upNextionBtn, () => startUpgrade('nextion'));
      bindClickAction(upSpiffsBtn, () => startUpgrade('spiffs'));
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
      bindClickAction(rebootSupervisorBtn, () => {
        startDelayedSystemAction(
          rebootSupervisorBtn,
          'Reboot superviseur',
          () => callSystemAction('supervisor', 'reboot'),
          'Reboot superviseur échoué'
        );
      });
      bindClickAction(rebootFlowBtn, () => {
        startDelayedSystemAction(
          rebootFlowBtn,
          'Reboot Flow.io',
          () => callSystemAction('flow', 'reboot'),
          'Reboot Flow.io échoué'
        );
      });
      bindClickAction(rebootNextionBtn, () => {
        startDelayedSystemAction(
          rebootNextionBtn,
          'Reboot Nextion',
          () => callSystemAction('nextion', 'reboot'),
          'Reboot Nextion échoué'
        );
      });
      bindClickAction(flowFactoryResetBtn, async () => {
        if (!confirm('Confirmer la réinitialisation usine de Flow.io ? Cette action efface la configuration distante.')) return;
        try {
          await callSystemAction('flow', 'factory_reset');
        } catch (err) {
          systemStatusText.textContent = 'Reset Flow.io échoué';
        }
      });
    }

    function initConfigBindings() {
      bindClickAction(flowCfgRefreshBtn, () => ensureFlowCfgLoaded(true));
      bindClickAction(flowCfgApplyBtn, () => appliquerPrimaryCfg());
      bindClickAction(flowCfgExportBtn, () => exportFlowCfgBackup());
      bindClickAction(flowCfgImportBtn, () => {
        if (!flowCfgImportFileInput || flowCfgBackupBusy) return;
        flowCfgImportFileInput.value = '';
        flowCfgImportFileInput.click();
      });
      if (flowCfgImportFileInput) {
        flowCfgImportFileInput.addEventListener('change', () => {
          const file = flowCfgImportFileInput.files && flowCfgImportFileInput.files[0]
            ? flowCfgImportFileInput.files[0]
            : null;
          if (!file) return;
          runAsyncTaskSafely(() => importFlowCfgBackupFromFile(file));
        });
      }
    }

    function initGlobalUiBindings() {
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

    renderUpgradeJourney(readUpgradeUiSession() || { phase: 'idle', target: '', detail: 'Aucune opération en cours.' });
    resumeUpgradeReconnectFlow();
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
