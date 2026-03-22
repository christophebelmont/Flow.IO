    const drawer = document.getElementById('drawer');
    const overlay = document.getElementById('overlay');
    const menuToggles = Array.from(document.querySelectorAll('[data-menu-toggle]'));
    const menuItems = Array.from(document.querySelectorAll('.menu-item'));
    const flowCfgMenuIcon = document.querySelector('.menu-item[data-page="page-control"] .ico');
    const pages = Array.from(document.querySelectorAll('.page'));
    const appMeta = document.querySelector('.app-meta');
    let webAssetVersion = '';
    let supervisorFirmwareVersion = '-';
    let hideMenuSvg = false;
    let unifyStatusCardIcons = false;
    let supervisorUptimeMs = 0;
    let supervisorHeap = {};

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
      }
      return '-';
    }

    supervisorFirmwareVersion = resolveSupervisorFirmwareVersion();
    let flowStatusLiveTimer = null;

    function versionedWebAssetUrl(path) {
      if (!webAssetVersion) return path;
      return path + '?v=' + encodeURIComponent(webAssetVersion);
    }

    async function loadWebMeta() {
      try {
        const res = await fetch('/api/web/meta', { cache: 'no-store' });
        const data = await res.json().catch(() => null);
        if (!res.ok || !data || data.ok !== true) return;

        if (typeof data.web_asset_version === 'string') {
          webAssetVersion = data.web_asset_version.trim();
        }
        applyMenuIconPreference(!!data.hide_menu_svg);
        applyStatusIconPreference(!!data.unify_status_card_icons);
        if (typeof data.firmware_version === 'string') {
          const trimmed = data.firmware_version.trim();
          if (trimmed) {
            supervisorFirmwareVersion = trimmed;
            if (appMeta) {
              appMeta.textContent = 'Supervisor ' + trimmed;
            }
          }
        }
        supervisorUptimeMs = Number(data.upms) || 0;
        supervisorHeap = (data.heap && typeof data.heap === 'object') ? data.heap : {};

        const activePage = document.querySelector('.page.active');
        if (activePage && activePage.id === 'page-status') {
          refreshFlowStatus().catch(() => {});
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
      if (upgradeStatusPollTimer) return;
      upgradeStatusPollTimer = setInterval(() => {
        refreshUpgradeStatus().catch(() => {});
      }, 4000);
    }

    function stopUpgradeStatusPolling() {
      if (!upgradeStatusPollTimer) return;
      clearInterval(upgradeStatusPollTimer);
      upgradeStatusPollTimer = null;
    }

    function stopFlowStatusLiveTimer() {
      if (!flowStatusLiveTimer) return;
      clearInterval(flowStatusLiveTimer);
      flowStatusLiveTimer = null;
    }

    let flowRemoteFetchQueue = Promise.resolve();

    function fetchFlowRemoteQueued(url, options) {
      const queued = flowRemoteFetchQueue
        .catch(() => {})
        .then(() => fetch(url, options));
      flowRemoteFetchQueue = queued.catch(() => {});
      return queued;
    }

    function showPage(pageId) {
      pages.forEach((el) => el.classList.toggle('active', el.id === pageId));
      menuItems.forEach((el) => el.classList.toggle('active', el.dataset.page === pageId));
      terminalActive = pageId === 'page-terminal';
      if (terminalActive) {
        connectLogSocket();
      } else {
        closeLogSocket();
        setWsStatusText('inactif');
      }
      if (pageId === 'page-status') {
        refreshFlowStatus().catch(() => {});
      } else {
        stopFlowStatusLiveTimer();
      }
      if (pageId === 'page-system') {
        onConfigPageShown().catch(() => {});
        onUpgradePageShown().catch(() => {});
      }
      if (pageId === 'page-control') {
        onControlPageShown().catch(() => {});
      }
      if (pageId === 'page-local-config') {
        onLocalConfigPageShown().catch(() => {});
      }
      if (pageId !== 'page-system') {
        stopUpgradeStatusPolling();
      }
      closeMobileDrawer();
    }

    menuItems.forEach((item) => item.addEventListener('click', () => showPage(item.dataset.page)));

    menuToggles.forEach((btn) => btn.addEventListener('click', () => {
      if (isMobileLayout()) {
        setMobileDrawerOpen(!drawer.classList.contains('mobile-open'));
      } else {
        if (hideMenuSvg) return;
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
    const cfgdocsPath = document.getElementById('cfgdocsPath');
    const saveCfgBtn = document.getElementById('saveCfg');
    const upSupervisorBtn = document.getElementById('upSupervisor');
    const upFlowBtn = document.getElementById('upFlow');
    const upNextionBtn = document.getElementById('upNextion');
    const upCfgdocsBtn = document.getElementById('upCfgdocs');
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
    let wifiScanPollTimer = null;
    let upgradeStatusPollTimer = null;
    let flowCfgCurrentModule = '';
    let flowCfgCurrentData = {};
    let flowCfgChildrenCache = {};
    let flowCfgPath = [];
    let cfgDocSources = [];
    let flowCfgDocsLoaded = false;
    let flowCfgLoadingDepth = 0;
    let upgradeCfgLoadedOnce = false;
    let wifiConfigLoadedOnce = false;
    let flowCfgLoadedOnce = false;
    let supCfgLoadedOnce = false;
    let supCfgCurrentModule = '';
    let supCfgCurrentData = {};
    let wifiScanAutoRequested = false;
    let flowStatusReqSeq = 0;
    const fieldApplyCheckIcon =
      '<svg viewBox="0 0 664 663" aria-hidden="true"><path fill="none" d="M646.293 331.888L17.7538 17.6187L155.245 331.888M646.293 331.888L17.753 646.157L155.245 331.888M646.293 331.888L318.735 330.228L155.245 331.888"></path><path stroke-linejoin="round" stroke-linecap="round" stroke-width="33.67" stroke="currentColor" d="M646.293 331.888L17.7538 17.6187L155.245 331.888M646.293 331.888L17.753 646.157L155.245 331.888M646.293 331.888L318.735 330.228L155.245 331.888"></path></svg>';
    const flowStatusDomainTtlMs = 20000;
    const flowStatusDomainKeys = ['system', 'wifi', 'mqtt', 'pool', 'i2c'];
    const flowStatusDomainCache = {
      system: { data: null, fetchedAt: 0 },
      wifi: { data: null, fetchedAt: 0 },
      mqtt: { data: null, fetchedAt: 0 },
      pool: { data: null, fetchedAt: 0 },
      i2c: { data: null, fetchedAt: 0 }
    };

    const wsProto = location.protocol === 'https:' ? 'wss' : 'ws';
    let logSource = 'flow';
    let logSocket = null;

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

    const iconeOeilOuvert =
      '<svg viewBox="0 0 24 24" aria-hidden="true"><path fill="currentColor" d="M12 5C6.7 5 2.73 8.11 1.16 11.25a1.7 1.7 0 0 0 0 1.5C2.73 15.89 6.7 19 12 19s9.27-3.11 10.84-6.25a1.7 1.7 0 0 0 0-1.5C21.27 8.11 17.3 5 12 5Zm0 12c-4.46 0-7.88-2.66-9.29-5 .64-1.06 1.74-2.21 3.17-3.11A11.7 11.7 0 0 1 12 7c4.46 0 7.88 2.66 9.29 5-.64 1.06-1.74 2.21-3.17 3.11A11.7 11.7 0 0 1 12 17Zm0-6.4A2.4 2.4 0 1 0 14.4 13 2.4 2.4 0 0 0 12 10.6Z"/></svg>';
    const iconeOeilBarre =
      '<svg viewBox="0 0 24 24" aria-hidden="true"><path fill="currentColor" d="M3.28 2.22 2.22 3.28l2.64 2.64c-1.54 1.14-2.84 2.65-3.7 4.34a1.7 1.7 0 0 0 0 1.5C2.73 14.89 6.7 18 12 18c2.2 0 4.18-.53 5.93-1.38l2.79 2.79 1.06-1.06ZM12 16.2c-4.46 0-7.88-2.66-9.29-5 .73-1.21 2.09-2.57 3.9-3.53l1.66 1.66a3.8 3.8 0 0 0 5.4 5.4l1.75 1.75c-1.02.44-2.16.72-3.42.72Zm.04-9.4 4.35 4.35c0-.05.01-.1.01-.15a4.4 4.4 0 0 0-4.4-4.4h-.01a.8.8 0 0 1 .05.2Zm9.8 4.45C20.27 8.11 16.3 5 11 5c-1.8 0-3.46.35-4.97.95l1.47 1.47A11.5 11.5 0 0 1 12 6.8c4.46 0 7.88 2.66 9.29 5-.43.71-1.03 1.47-1.79 2.16l1.28 1.28c.86-.78 1.56-1.65 2.06-2.63a1.7 1.7 0 0 0 0-1.36Z"/></svg>';

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
        const res = await fetch('/api/fwupdate/config', { cache: 'no-store' });
        const data = await res.json();
        if (data && data.ok) {
          updateHost.value = data.update_host || '';
          flowPath.value = data.flowio_path || '';
          supervisorPath.value = data.supervisor_path || '';
          nextionPath.value = data.nextion_path || '';
          cfgdocsPath.value = data.cfgdocs_path || '';
        }
      } catch (err) {
        setUpgradeMessage('Échec du chargement de la configuration : ' + err);
      }
    }

    async function saveUpgradeConfig() {
      const body = new URLSearchParams();
      body.set('update_host', updateHost.value.trim());
      body.set('flowio_path', flowPath.value.trim());
      body.set('supervisor_path', supervisorPath.value.trim());
      body.set('nextion_path', nextionPath.value.trim());
      body.set('cfgdocs_path', cfgdocsPath.value.trim());
      const res = await fetch('/api/fwupdate/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
        body: body.toString()
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.ok) throw new Error('échec enregistrement');
      setUpgradeMessage('Configuration enregistrée.');
    }

    async function refreshUpgradeStatus() {
      try {
        const res = await fetch('/api/fwupdate/status', { cache: 'no-store' });
        const data = await res.json();
        if (data && data.ok) updateUpgradeView(data);
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
        else if (target === 'cfgdocs') endpoint = '/fwupdate/cfgdocs';
        const res = await fetch(endpoint, { method: 'POST' });
        const data = await res.json().catch(() => ({}));
        if (!res.ok || !data.ok) throw new Error('échec démarrage');
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
      if (!flowCfgDocsLoaded) {
        await chargerFlowCfgDocs();
      }
      if (!flowCfgLoadedOnce) {
        flowCfgLoadedOnce = true;
        await chargerFlowCfgModules(false);
        return;
      }
      await chargerFlowCfgModules(false);
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
      span.innerHTML = ok
        ? '<svg viewBox="0 0 16 16" aria-hidden="true"><circle cx="8" cy="8" r="6"></circle><path d="M5 8.2 7 10.2 11 6.2"></path></svg>'
        : '<svg viewBox="0 0 16 16" aria-hidden="true"><circle cx="8" cy="8" r="6"></circle><path d="M5.5 5.5 10.5 10.5"></path><path d="M10.5 5.5 5.5 10.5"></path></svg>';
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

    function createFlowLiveValue(kind, baseMs, refMs) {
      const span = document.createElement('span');
      span.dataset.flowLive = kind;
      span.dataset.flowBaseMs = String(Math.max(0, Number(baseMs) || 0));
      span.dataset.flowRefMs = String(Math.max(0, Number(refMs) || Date.now()));
      return span;
    }

    function updateFlowLiveValue(el, nowMs) {
      if (!el) return;
      const kind = el.dataset.flowLive || '';
      const baseMs = Number(el.dataset.flowBaseMs) || 0;
      const refMs = Number(el.dataset.flowRefMs) || nowMs;
      const totalMs = baseMs + Math.max(0, nowMs - refMs);
      if (kind === 'uptime') {
        el.textContent = fmtFlowUptime(totalMs);
        return;
      }
      if (kind === 'elapsed') {
        el.textContent = fmtFlowRelativeAge(totalMs);
      }
    }

    function refreshFlowStatusLiveValues() {
      const pageStatus = document.getElementById('page-status');
      if (!pageStatus || !pageStatus.classList.contains('active')) return;
      const nowMs = Date.now();
      flowStatusGrid.querySelectorAll('[data-flow-live]').forEach((el) => updateFlowLiveValue(el, nowMs));
    }

    function ensureFlowStatusLiveTimer() {
      if (flowStatusLiveTimer) return;
      flowStatusLiveTimer = setInterval(refreshFlowStatusLiveValues, 1000);
    }

    function buildFlowStatusCardIcon(iconKey, ok, label) {
      const unifiedIconSvg =
        '<svg viewBox="0 0 512 512" aria-hidden="true"><path d="M288 32c0-17.7-14.3-32-32-32s-32 14.3-32 32V256c0 17.7 14.3 32 32 32s32-14.3 32-32V32zM143.5 120.6c13.6-11.3 15.4-31.5 4.1-45.1s-31.5-15.4-45.1-4.1C49.7 115.4 16 181.8 16 256c0 132.5 107.5 240 240 240s240-107.5 240-240c0-74.2-33.8-140.6-86.6-184.6c-13.6-11.3-33.8-9.4-45.1 4.1s-9.4 33.8 4.1 45.1c38.9 32.3 63.5 81 63.5 135.4c0 97.2-78.8 176-176 176s-176-78.8-176-176c0-54.4 24.7-103.1 63.5-135.4z"/></svg>';
      const iconSvg = {
        wifi: '<svg viewBox="0 0 24 24" aria-hidden="true"><path fill="currentColor" d="M2.28 8.84a15.3 15.3 0 0 1 19.44 0l-1.42 1.42a13.3 13.3 0 0 0-16.6 0l-1.42-1.42zm3.54 3.54a10.3 10.3 0 0 1 12.36 0l-1.42 1.42a8.3 8.3 0 0 0-9.52 0l-1.42-1.42zm3.54 3.54a5.3 5.3 0 0 1 5.28 0L12 18.56l-2.64-2.64z"/></svg>',
        supervisor: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M7 7h10v2H7Zm0 4h10v2H7Zm0 4h6v2H7Z"/><path d="M5 3h14a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H8l-5 0V5a2 2 0 0 1 2-2Zm0 2v14h14V5Z"/></svg>',
        system: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M9 3h6v2h2.5A1.5 1.5 0 0 1 19 6.5V9h2v6h-2v2.5A1.5 1.5 0 0 1 17.5 19H15v2H9v-2H6.5A1.5 1.5 0 0 1 5 17.5V15H3V9h2V6.5A1.5 1.5 0 0 1 6.5 5H9Zm-2 4v10h10V7Zm2 2h6v6H9Z"/></svg>',
        mqtt: '<svg viewBox="0 0 24 24" aria-hidden="true"><ellipse cx="6.594" cy="7.156" fill="#fff" stroke="currentColor" rx="2.844" ry="2.781"/><ellipse cx="19.094" cy="8.594" fill="#fff" stroke="currentColor" rx="2.844" ry="2.906"/><ellipse cx="11.687" cy="18.344" fill="#fff" stroke="currentColor" rx="3" ry="2.906"/><path fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" d="m9.625 7.375 6.625.875M7.312 10.062l2.875 5.625M13.375 15.937l4.125-4.875"/></svg>',
        pool: '<svg viewBox="0 0 24 24" aria-hidden="true"><path fill="currentColor" d="M1.125 10.875c1.5 0 2.25-.75 3-1.5s1.5-1.5 3-1.5s2.25.75 3 1.5s1.5 1.5 3 1.5s2.25-.75 3-1.5s1.5-1.5 3-1.5s2.25.75 3 1.5v2.5c-.75-.75-1.5-1.5-3-1.5s-2.25.75-3 1.5s-1.5 1.5-3 1.5s-2.25-.75-3-1.5s-1.5-1.5-3-1.5s-2.25.75-3 1.5s-1.5 1.5-3 1.5v-2.5zm0 4c1.5 0 2.25-.75 3-1.5s1.5-1.5 3-1.5s2.25.75 3 1.5s1.5 1.5 3 1.5s2.25-.75 3-1.5s1.5-1.5 3-1.5s2.25.75 3 1.5v2.5c-.75-.75-1.5-1.5-3-1.5s-2.25.75-3 1.5s-1.5 1.5-3 1.5s-2.25-.75-3-1.5s-1.5-1.5-3-1.5s-2.25.75-3 1.5s-1.5 1.5-3 1.5v-2.5z"/></svg>',
        pump: '<svg viewBox="0 0 24 24" aria-hidden="true"><path fill="currentColor" d="M18 1H6a5 5 0 0 0 0 10h12a5 5 0 0 0 0-10zm0 8a3 3 0 1 1 3-3 3 3 0 0 1-3 3zm0 4H6a5 5 0 0 0 0 10h12a5 5 0 0 0 0-10zm-.2 9H6a4 4 0 0 1 0-8h11.8a4 4 0 1 1 0 8zM6 15a3 3 0 1 0 3 3 3 3 0 0 0-3-3zm0 5a2 2 0 1 1 2-2 2.003 2.003 0 0 1-2 2z"/></svg>'
      };
      const span = document.createElement('span');
      span.className = ok ? 'status-card-icon is-true' : 'status-card-icon is-false';
      span.setAttribute('role', 'img');
      span.setAttribute('aria-label', label || (ok ? 'OK' : 'NOK'));
      span.title = label || (ok ? 'OK' : 'NOK');
      span.innerHTML = unifyStatusCardIcons ? unifiedIconSvg : (iconSvg[iconKey] || iconSvg.system);
      return span;
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

    function buildMqttStatsStrip(items) {
      const wrapper = document.createElement('div');
      wrapper.className = 'status-mqtt-strip';
      (items || []).forEach((item) => {
        const cell = document.createElement('div');
        cell.className = 'status-mqtt-metric';

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
        const res = await fetchFlowRemoteQueued(
          '/api/flow/status/domain?d=' + encodeURIComponent(domainKey),
          { cache: 'no-store' }
        );
        const data = await res.json().catch(() => null);
        if (!res.ok || !data || data.ok !== true) {
          throw new Error('statut ' + domainKey + ' indisponible');
        }
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

      const kv = document.createElement('div');
      kv.className = 'status-kv';
      (config.rows || []).forEach((row) => appendFlowStatusRow(kv, row[0], row[1]));
      card.appendChild(kv);
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
            { label: 'Anomalies', value: mqttIssueCount },
            { label: 'Ignores', value: (Number(mqttRxDrop) || 0) + (Number(mqttOversizeDrop) || 0) },
            { label: 'Contenu', value: mqttParseFail },
            { label: 'Traitement', value: mqttHandlerFail }
          ])
        ]
      });
      appendFlowStatusCard({
        title: 'Mesures Bassin',
        icon: 'pool',
        ok: poolMetricsReady,
        iconLabel: poolMetricsReady ? 'Mesures bassin disponibles' : 'Mesures bassin indisponibles',
        rows: [
          ['Eau', fmtFlowFixed(waterTemp, 1, '°C')],
          ['Air', fmtFlowFixed(airTemp, 1, '°C')],
          ['ORP', fmtFlowFixed(orpValue, 0, 'mV')],
          ['pH', fmtFlowFixed(phValue, 2, '')]
        ]
      });
      appendFlowStatusCard({
        title: 'Etats Bassin',
        icon: 'pump',
        ok: poolStatesReady,
        iconLabel: poolStatesReady ? 'Etats bassin disponibles' : 'Etats bassin indisponibles',
        rows: [
          ['Mode auto', (typeof autoModeOn === 'boolean') ? buildFlowStatusReadonlySwitch(autoModeOn) : '-'],
          ['Filtration', (typeof filtrationOn === 'boolean') ? buildFlowStatusReadonlySwitch(filtrationOn) : '-'],
          ['Hivernage', (typeof winterModeOn === 'boolean') ? buildFlowStatusReadonlySwitch(winterModeOn) : '-']
        ]
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
          flowStatusChip.textContent = 'statut affiché depuis le cache local';
          return;
        }
        flowStatusRaw.hidden = true;
        flowStatusRaw.classList.remove('is-skeleton');
        flowStatusChip.textContent = 'erreur lecture statut';
        flowStatusGrid.innerHTML = '';
        flowStatusRaw.innerHTML = '';
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
      if (wifiScanPollTimer) {
        clearTimeout(wifiScanPollTimer);
        wifiScanPollTimer = null;
      }
    }

    function scheduleWifiScanPolling() {
      stopWifiScanPolling();
      wifiScanPollTimer = setTimeout(() => {
        refreshWifiScanStatus(false);
      }, 1200);
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
      const body = new URLSearchParams();
      body.set('force', force ? '1' : '0');
      const res = await fetch('/api/wifi/scan', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
        body: body.toString()
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.ok) throw new Error('échec démarrage scan');
      return data;
    }

    async function refreshWifiScanStatus(triggerScan) {
      try {
        if (triggerScan) {
          await requestWifiScan(true);
        }
        const res = await fetch('/api/wifi/scan', { cache: 'no-store' });
        const data = await res.json();
        if (!res.ok || !data || data.ok !== true) throw new Error('échec lecture état');

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
        const res = await fetch('/api/wifi/config', { cache: 'no-store' });
        const data = await res.json();
        if (data && data.ok) {
          wifiEnabled.checked = toBool(data.enabled);
          wifiSsid.value = data.ssid || '';
          wifiPass.value = data.pass || '';
          wifiConfigStatus.textContent = 'Configuration WiFi chargée.';
        }
      } catch (err) {
        wifiConfigStatus.textContent = 'Chargement WiFi échoué: ' + err;
      }
    }

    async function saveWifiConfig() {
      const body = new URLSearchParams();
      body.set('enabled', wifiEnabled.checked ? '1' : '0');
      body.set('ssid', wifiSsid.value.trim());
      body.set('pass', wifiPass.value);

      const res = await fetch('/api/wifi/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
        body: body.toString()
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.ok) throw new Error('échec application');
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
      if (flowCfgMenuIcon && typeof flowCfgMenuIcon.innerHTML === 'string' && flowCfgMenuIcon.innerHTML.trim().length > 0) {
        return flowCfgMenuIcon.innerHTML;
      }
      return '<svg viewBox="0 0 24 24" aria-hidden="true"><path fill="currentColor" d="M13.221 22.753c-1.046.164-2.13.247-3.222.247-5.304 0-9-1.845-9-3.5v-3.018C2.602 17.984 5.984 19 9.999 19c.528 0 1.042-.022 1.548-.057.275-.018.484-.256.465-.532s-.239-.485-.533-.466c-.483.033-.975.055-1.48.055-5.304 0-9-1.845-9-3.5v-3.018C2.602 12.984 5.984 14 9.999 14c1.516 0 2.979-.146 4.349-.436a.5.5 0 0 0-.207-.979A20 20 0 0 1 9.999 13c-5.304 0-9-1.845-9-3.5V6.482C2.602 7.984 5.984 9 9.999 9s7.397-1.016 9-2.518V9.5c0 .646-.533 1.188-.979 1.53a.501.501 0 0 0 .304.897.5.5 0 0 0 .303-.103c.141-.107.252-.223.372-.336v.174a.5.5 0 0 0 1 0V4.5c0-2.523-4.393-4.5-10-4.5s-10 1.977-10 4.5v15c0 2.523 4.393 4.5 10 4.5 1.144 0 2.28-.087 3.376-.259.273-.043.459-.299.417-.571s-.297-.454-.571-.417M9.999 1c5.304 0 9 1.845 9 3.5s-3.696 3.5-9 3.5-9-1.845-9-3.5 3.696-3.5 9-3.5M23.69 16.843a.5.5 0 0 0-.669-.3c-.24.098-.501.02-.624-.194s-.062-.482.145-.638a.497.497 0 0 0 .075-.73c-.817-.927-1.867-1.541-3.039-1.774a.5.5 0 0 0-.596.44c-.026.258-.234.452-.482.452s-.456-.194-.482-.452a.504.504 0 0 0-.596-.44 5.44 5.44 0 0 0-3.039 1.774.5.5 0 0 0 .075.73.487.487 0 0 1 .145.638.49.49 0 0 1-.624.194.497.497 0 0 0-.669.3c-.209.616-.311 1.191-.311 1.756s.102 1.139.311 1.755a.5.5 0 0 0 .669.3c.24-.099.5-.018.624.194a.49.49 0 0 1-.145.638.497.497 0 0 0-.075.73c.816.928 1.867 1.542 3.039 1.774a.5.5 0 0 0 .595-.44c.026-.258.234-.452.482-.452s.456.194.482.452a.504.504 0 0 0 .597.441 5.43 5.43 0 0 0 3.039-1.774.5.5 0 0 0-.075-.73.49.49 0 0 1-.145-.638.494.494 0 0 1 .624-.194.5.5 0 0 0 .669-.3c.209-.616.311-1.19.311-1.756s-.102-1.14-.311-1.756m-.824 2.773c-.553-.036-1.058.252-1.336.734s-.251 1.066.031 1.522c-.507.48-1.101.83-1.748 1.029a1.478 1.478 0 0 0-2.627-.001 4.4 4.4 0 0 1-1.748-1.029c.282-.456.309-1.04.03-1.522s-.792-.768-1.336-.734c-.089-.354-.133-.688-.133-1.016s.044-.662.133-1.016a1.44 1.44 0 0 0 1.336-.734 1.48 1.48 0 0 0-.03-1.522c.507-.48 1.1-.829 1.747-1.029a1.478 1.478 0 0 0 2.628 0c.647.2 1.24.549 1.747 1.029a1.49 1.49 0 0 0-.03 1.522c.278.483.804.77 1.336.734.089.354.133.689.133 1.016s-.044.662-.133 1.017m-4.367-3.517c-1.378 0-2.5 1.121-2.5 2.5s1.122 2.5 2.5 2.5 2.5-1.121 2.5-2.5-1.122-2.5-2.5-2.5m0 4c-.827 0-1.5-.673-1.5-1.5s.673-1.5 1.5-1.5 1.5.673 1.5 1.5-.673 1.5-1.5 1.5"/></svg>';
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
        toggleBtn.innerHTML =
          '<svg class="control-crumb-arrows" viewBox="0 0 12 16" aria-hidden="true">' +
          '<path fill="currentColor" d="M6 2.2 9.4 6H2.6L6 2.2Z"/>' +
          '<path fill="currentColor" d="M6 13.8 2.6 10h6.8L6 13.8Z"/>' +
          '</svg>';
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

    async function chargerFlowCfgDocs() {
      flowCfgDocsLoaded = true;
      try {
        const res = await fetch(versionedWebAssetUrl('/webinterface/cfgdocs.fr.json'), { cache: 'no-store' });
        const data = await res.json();
        if (!res.ok || !data || typeof data !== 'object') {
          throw new Error('invalid docs payload');
        }
        const docs = (data.docs && typeof data.docs === 'object') ? data.docs : {};
        const meta = (data._meta && typeof data._meta === 'object') ? data._meta : {};
        cfgDocSources = [{ docs: docs, meta: meta }];
        try {
          const modsRes = await fetch(versionedWebAssetUrl('/webinterface/cfgmods.fr.json'), { cache: 'no-store' });
          const modsData = await modsRes.json();
          if (modsRes.ok && modsData && typeof modsData === 'object') {
            const modsDocs = (modsData.docs && typeof modsData.docs === 'object') ? modsData.docs : {};
            const modsMeta = (modsData._meta && typeof modsData._meta === 'object') ? modsData._meta : {};
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
      const meta = (source.meta && typeof source.meta === 'object') ? source.meta : {};
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
      } catch (err) {
        flowCfgStatus.textContent = 'Chargement des branches échoué: ' + err;
      } finally {
        endFlowCfgLoading();
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
      const res = (target === 'flow')
        ? await fetchFlowRemoteQueued(endpoint, { method: 'POST' })
        : await fetch(endpoint, { method: 'POST' });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.ok) throw new Error('échec action');
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

    saveCfgBtn.addEventListener('click', async () => {
      try {
        await saveUpgradeConfig();
      } catch (err) {
        setUpgradeMessage('Échec de l\'enregistrement : ' + err);
      }
    });
    flowStatusRefreshBtn.addEventListener('click', async () => {
      await refreshFlowStatus(true);
    });
    upSupervisorBtn.addEventListener('click', () => startUpgrade('supervisor'));
    upFlowBtn.addEventListener('click', () => startUpgrade('flowio'));
    upNextionBtn.addEventListener('click', () => startUpgrade('nextion'));
    upCfgdocsBtn.addEventListener('click', () => startUpgrade('cfgdocs'));
    refreshStateBtn.addEventListener('click', refreshUpgradeStatus);
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
    scanWifiBtn.addEventListener('click', () => {
      refreshWifiScanStatus(true);
    });
    applyWifiCfgBtn.addEventListener('click', async () => {
      try {
        await saveWifiConfig();
      } catch (err) {
        wifiConfigStatus.textContent = 'Application WiFi échouée: ' + err;
      }
    });
    rebootSupervisorBtn.addEventListener('click', async () => {
      try {
        await callSystemAction('supervisor', 'reboot');
      } catch (err) {
        systemStatusText.textContent = 'Redémarrage du Superviseur échoué : ' + err;
      }
    });
    rebootFlowBtn.addEventListener('click', async () => {
      try {
        await callSystemAction('flow', 'reboot');
      } catch (err) {
        systemStatusText.textContent = 'Redémarrage de Flow.IO échoué : ' + err;
      }
    });
    flowFactoryResetBtn.addEventListener('click', async () => {
      if (!confirm('Confirmer la réinitialisation usine de Flow.IO ? Cette action efface la configuration distante.')) return;
      try {
        await callSystemAction('flow', 'factory_reset');
      } catch (err) {
        systemStatusText.textContent = 'Réinitialisation usine de Flow.IO échouée : ' + err;
      }
    });
    flowCfgRefreshBtn.addEventListener('click', async () => {
      await chargerFlowCfgModules(true);
    });
    flowCfgApplyBtn.addEventListener('click', async () => {
      await appliquerFlowCfg();
    });
    supCfgRefreshBtn.addEventListener('click', async () => {
      await chargerSupervisorCfgModules(true);
    });
    supCfgModuleSelect.addEventListener('change', async () => {
      const selected = nettoyerNomFlowCfg(supCfgModuleSelect.value);
      await chargerSupervisorCfgModule(selected);
    });
    supCfgApplyBtn.addEventListener('click', async () => {
      await appliquerSupervisorCfg();
    });

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
      const active = document.querySelector('.page.active');
      const onUpgradePage = !!active && active.id === 'page-system';
      const onTerminalPage = !!active && active.id === 'page-terminal';
      if (document.hidden || !onUpgradePage) {
        stopUpgradeStatusPolling();
      } else {
        startUpgradeStatusPolling();
      }
      if (document.hidden || !onTerminalPage) {
        closeLogSocket();
        setWsStatusText('inactif');
      } else if (terminalActive) {
        connectLogSocket();
      }
    });

    setUpgradeProgress(0);
    {
      const activePage = document.querySelector('.page.active');
      if (activePage && activePage.id) {
        showPage(activePage.id);
      } else {
        showPage('page-status');
      }
    }
    loadWebMeta().catch(() => {});
