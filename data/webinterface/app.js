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
      if (pageId === 'page-config') {
        onConfigPageShown().catch(() => {});
      }
      if (pageId === 'page-control') {
        onControlPageShown().catch(() => {});
      }
      closeMobileDrawer();
    }

    async function ouvrirPageParDefautSelonModeReseau() {
      try {
        const res = await fetch('/api/network/mode', { cache: 'no-store' });
        const data = await res.json();
        if (res.ok && data && data.ok === true && data.mode === 'ap') {
          showPage('page-config');
        }
      } catch (err) {
        // Keep default page when network mode endpoint is unavailable.
      }
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
    const toggleAutoscrollInput = document.getElementById('toggleAutoscroll');
    let autoScrollEnabled = true;

    const updateHost = document.getElementById('updateHost');
    const flowPath = document.getElementById('flowPath');
    const nextionPath = document.getElementById('nextionPath');
    const supervisorPath = document.getElementById('supervisorPath');
    const saveCfgBtn = document.getElementById('saveCfg');
    const upSupervisorBtn = document.getElementById('upSupervisor');
    const upFlowBtn = document.getElementById('upFlow');
    const upNextionBtn = document.getElementById('upNextion');
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
    let wifiScanPollTimer = null;
    let flowCfgCurrentModule = '';
    let flowCfgCurrentData = {};
    let flowCfgChildrenCache = {};
    let flowCfgPath = [];
    let wifiConfigLoadedOnce = false;
    let flowCfgLoadedOnce = false;
    let wifiScanAutoRequested = false;

    const wsProto = location.protocol === 'https:' ? 'wss' : 'ws';
    const ws = new WebSocket(wsProto + '://' + location.host + '/wsserial');

    ws.onopen = () => wsStatus.textContent = 'connecté';
    ws.onclose = () => wsStatus.textContent = 'déconnecté';
    ws.onerror = () => wsStatus.textContent = 'erreur';

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
      if (ws.readyState === WebSocket.OPEN) ws.send(txt);
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
    refreshAutoscrollUi();
    flowCfgApplyBtn.disabled = true;

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
      upStatusChip.textContent = stateLabel;
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
      if (!flowCfgLoadedOnce) {
        flowCfgLoadedOnce = true;
        await chargerFlowCfgModules(false);
        return;
      }
      await chargerFlowCfgModules(false);
    }

    function boolFlowStatus(v) {
      return v ? 'oui' : 'non';
    }

    function fmtFlowStatusVal(v) {
      if (v === null || typeof v === 'undefined') return '-';
      if (typeof v === 'boolean') return boolFlowStatus(v);
      return String(v);
    }

    function fmtFlowUptime(ms) {
      if (!Number.isFinite(ms) || ms < 0) return '-';
      const sec = Math.floor(ms / 1000);
      const h = Math.floor(sec / 3600);
      const m = Math.floor((sec % 3600) / 60);
      const s = sec % 60;
      if (h > 0) return h + 'h ' + m + 'm ' + s + 's';
      if (m > 0) return m + 'm ' + s + 's';
      return s + 's';
    }

    function appendFlowStatusCard(title, rows) {
      const card = document.createElement('div');
      card.className = 'status-card';
      const heading = document.createElement('h3');
      heading.textContent = title;
      card.appendChild(heading);

      const kv = document.createElement('div');
      kv.className = 'status-kv';
      rows.forEach((row) => {
        const line = document.createElement('div');
        const key = document.createElement('span');
        key.textContent = row[0];
        const value = document.createElement('b');
        value.textContent = fmtFlowStatusVal(row[1]);
        line.appendChild(key);
        line.appendChild(value);
        kv.appendChild(line);
      });
      card.appendChild(kv);
      flowStatusGrid.appendChild(card);
    }

    function renderFlowStatus(data) {
      const wifi = (data && typeof data.wifi === 'object') ? data.wifi : {};
      const mqtt = (data && typeof data.mqtt === 'object') ? data.mqtt : {};
      const heap = (data && data.heap && typeof data.heap === 'object') ? data.heap : {};
      const i2c = (data && data.i2c && typeof data.i2c === 'object') ? data.i2c : {};

      flowStatusGrid.innerHTML = '';
      appendFlowStatusCard('Réseau', [
        ['WiFi connecté', !!wifi.ready],
        ['IP', wifi.ip || '-'],
        ['RSSI (dBm)', wifi.has_rssi ? wifi.rssi_dbm : '-'],
        ['MQTT connecté', !!mqtt.ready]
      ]);
      appendFlowStatusCard('I2C Supervisor', [
        ['Lien actif', !!i2c.supervisor_link_ok],
        ['Supervisor vu', !!i2c.supervisor_seen],
        ['Req total', i2c.request_count],
        ['Dernière req (ms)', i2c.last_request_ago_ms]
      ]);
      appendFlowStatusCard('Système Flow.IO', [
        ['Firmware', data.firmware || '-'],
        ['Uptime', fmtFlowUptime(data.uptime_ms)],
        ['Heap libre', heap.free],
        ['Heap min', heap.min]
      ]);
      appendFlowStatusCard('Diagnostic MQTT', [
        ['RX drop', mqtt.rx_drop],
        ['Parse fail', mqtt.parse_fail],
        ['Handler fail', mqtt.handler_fail],
        ['Oversize drop', mqtt.oversize_drop]
      ]);

      flowStatusChip.textContent = i2c.supervisor_link_ok ? 'Flow.IO en ligne (I2C OK)' : 'Flow.IO partiel / lien I2C faible';
      flowStatusRaw.textContent = JSON.stringify(data, null, 2);
    }

    async function refreshFlowStatus() {
      try {
        const res = await fetch('/api/flow/status', { cache: 'no-store' });
        const data = await res.json();
        if (!res.ok || !data || data.ok !== true) {
          throw new Error('statut indisponible');
        }
        renderFlowStatus(data);
      } catch (err) {
        flowStatusChip.textContent = 'erreur lecture statut';
        flowStatusGrid.innerHTML = '';
        flowStatusRaw.textContent = 'Erreur: ' + err;
      }
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

    function flowCfgCachedChildren(prefix) {
      const p = nettoyerNomFlowCfg(prefix);
      const key = flowCfgCacheKey(p);
      const node = flowCfgChildrenCache[key];
      if (!node || !Array.isArray(node.children)) return [];
      return node.children.slice();
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
      rootBtn.innerHTML =
        '<svg viewBox="0 0 24 24" aria-hidden="true"><path fill="currentColor" d="M19.14 12.94c.04-.31.06-.63.06-.94s-.02-.63-.06-.94l2.03-1.58a.5.5 0 0 0 .12-.64l-1.92-3.32a.5.5 0 0 0-.6-.22l-2.39.96a7.3 7.3 0 0 0-1.63-.94l-.36-2.54a.5.5 0 0 0-.5-.42h-3.84a.5.5 0 0 0-.5.42l-.36 2.54c-.58.23-1.13.54-1.63.94l-2.39-.96a.5.5 0 0 0-.6.22L2.71 8.84a.5.5 0 0 0 .12.64l2.03 1.58c-.04.31-.06.63-.06.94s.02.63.06.94l-2.03 1.58a.5.5 0 0 0-.12.64l1.92 3.32a.5.5 0 0 0 .6.22l2.39-.96c.5.4 1.05.71 1.63.94l.36 2.54a.5.5 0 0 0 .5.42h3.84a.5.5 0 0 0 .5-.42l.36-2.54c.58-.23 1.13-.54 1.63-.94l2.39.96a.5.5 0 0 0 .6-.22l1.92-3.32a.5.5 0 0 0-.12-.64l-2.03-1.58ZM12 15.5A3.5 3.5 0 1 1 12 8.5a3.5 3.5 0 0 1 0 7Z"/></svg>';
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
      const children = (node && Array.isArray(node.children)) ? node.children : [];
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

    async function chargerFlowCfgChildren(prefix, forceReload) {
      const p = nettoyerNomFlowCfg(prefix);
      const key = flowCfgCacheKey(p);
      if (!forceReload && flowCfgChildrenCache[key]) {
        return flowCfgChildrenCache[key];
      }

      const url = p.length > 0
        ? ('/api/flowcfg/children?prefix=' + encodeURIComponent(p))
        : '/api/flowcfg/children';
      const res = await fetch(url, { cache: 'no-store' });
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
      flowCfgApplyBtn.disabled = true;
      if (message) {
        flowCfgStatus.textContent = message;
      }
    }

    function renderFlowCfgFields(dataObj) {
      flowCfgFields.innerHTML = '';
      const data = (dataObj && typeof dataObj === 'object') ? dataObj : {};
      const keys = Object.keys(data).sort();
      if (keys.length === 0) {
        const row = document.createElement('div');
        row.className = 'control-row';
        const label = document.createElement('span');
        label.className = 'control-label';
        label.textContent = 'Aucun champ configurable dans cette branche.';
        row.appendChild(label);
        flowCfgFields.appendChild(row);
        return;
      }

      for (const key of keys) {
        const value = data[key];
        const row = document.createElement('div');
        row.className = 'control-row';

        const label = document.createElement('span');
        label.className = 'control-label';
        label.textContent = key;
        row.appendChild(label);

        if (typeof value === 'boolean') {
          const sw = document.createElement('span');
          sw.className = 'md3-switch';
          const input = document.createElement('input');
          input.type = 'checkbox';
          input.checked = value;
          input.dataset.key = key;
          input.dataset.kind = 'bool';
          const track = document.createElement('span');
          track.className = 'md3-track';
          const thumb = document.createElement('span');
          thumb.className = 'md3-thumb';
          sw.appendChild(input);
          sw.appendChild(track);
          sw.appendChild(thumb);
          row.appendChild(sw);
        } else if (typeof value === 'number') {
          const input = document.createElement('input');
          input.className = 'control-input';
          input.type = 'number';
          input.value = String(value);
          input.step = Number.isInteger(value) ? '1' : '0.001';
          input.dataset.key = key;
          input.dataset.kind = Number.isInteger(value) ? 'int' : 'float';
          row.appendChild(input);
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
          row.appendChild(input);
        }

        flowCfgFields.appendChild(row);
      }
    }

    function buildFlowCfgPatchJson() {
      if (!flowCfgCurrentModule) throw new Error('branche non sélectionnée');
      const patch = {};
      const modulePatch = {};
      const fields = flowCfgFields.querySelectorAll('[data-key]');
      fields.forEach((el) => {
        const key = el.dataset.key;
        const kind = el.dataset.kind;
        if (!key || !kind) return;
        if (kind === 'bool') {
          modulePatch[key] = !!el.checked;
          return;
        }
        if (kind === 'int') {
          const v = Number.parseInt(el.value, 10);
          modulePatch[key] = Number.isFinite(v) ? v : 0;
          return;
        }
        if (kind === 'float') {
          const v = Number.parseFloat(el.value);
          modulePatch[key] = Number.isFinite(v) ? v : 0;
          return;
        }
        const masked = el.dataset.masked === '1';
        const raw = String(el.value ?? '');
        if (masked && raw.length === 0) return;
        modulePatch[key] = raw;
      });
      patch[flowCfgCurrentModule] = modulePatch;
      return JSON.stringify(patch);
    }

    async function chargerFlowCfgModule(moduleName) {
      const m = nettoyerNomFlowCfg(moduleName);
      if (!m) {
        renderFlowCfgTitle(cheminFlowCfgCourant());
        resetFlowCfgEditor('Aucune branche sélectionnée.');
        return;
      }
      try {
        const res = await fetch('/api/flowcfg/module?name=' + encodeURIComponent(m), { cache: 'no-store' });
        const data = await res.json();
        if (!res.ok || !data || data.ok !== true || typeof data.data !== 'object') {
          throw new Error('lecture module impossible');
        }
        flowCfgCurrentModule = m;
        flowCfgCurrentData = data.data;
        renderFlowCfgTitle(m);
        renderFlowCfgFields(flowCfgCurrentData);
        flowCfgApplyBtn.disabled = false;
        flowCfgStatus.textContent = data.truncated
          ? 'Branche chargée (tronquée, buffer distant atteint).'
          : 'Branche chargée.';
      } catch (err) {
        renderFlowCfgTitle(cheminFlowCfgCourant());
        resetFlowCfgEditor('Chargement branche échoué: ' + err);
      }
    }

    async function renderFlowCfgNavigator(forceReloadCurrent) {
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
    }

    async function chargerFlowCfgModules(forceReload) {
      const force = !!forceReload;
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
      }
    }

    async function appliquerFlowCfg() {
      try {
        const patch = buildFlowCfgPatchJson();
        const body = new URLSearchParams();
        body.set('patch', patch);
        const res = await fetch('/api/flowcfg/apply', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
          body: body.toString()
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok || !data || data.ok !== true) {
          throw new Error('apply refusé');
        }
        flowCfgStatus.textContent = 'Configuration appliquée sur Flow.IO.';
        await chargerFlowCfgModule(flowCfgCurrentModule);
      } catch (err) {
        flowCfgStatus.textContent = 'Application cfg échouée: ' + err;
      }
    }

    async function callSystemAction(target, action) {
      let endpoint = '/api/system/reboot';
      if (target === 'flow' && action === 'reboot') endpoint = '/api/flow/system/reboot';
      else if (target === 'flow' && action === 'factory_reset') endpoint = '/api/flow/system/factory-reset';
      else if (target === 'supervisor' && action === 'factory_reset') endpoint = '/api/system/factory-reset';
      const res = await fetch(endpoint, { method: 'POST' });
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
      await refreshFlowStatus();
    });
    upSupervisorBtn.addEventListener('click', () => startUpgrade('supervisor'));
    upFlowBtn.addEventListener('click', () => startUpgrade('flowio'));
    upNextionBtn.addEventListener('click', () => startUpgrade('nextion'));
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

    loadUpgradeConfig();
    setUpgradeProgress(0);
    refreshFlowStatus().catch(() => {});
    ouvrirPageParDefautSelonModeReseau();
    refreshUpgradeStatus();
    setInterval(refreshUpgradeStatus, 2000);
