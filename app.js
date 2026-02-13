const statusEl = document.getElementById("status");
const warningEl = document.getElementById("webapp-warning");
const screenshotBtn = document.getElementById("screenshot-btn");
const compressionToggle = document.getElementById("compression-toggle");
const resolutionSelect = document.getElementById("resolution-select");
const qualityRange = document.getElementById("quality-range");
const qualityValue = document.getElementById("quality-value");

const tg = window.Telegram?.WebApp;

const setStatus = (text) => {
  statusEl.textContent = text;
};

const loadSettings = () => {
  const raw = localStorage.getItem("pccontroller.settings");
  if (!raw) {
    return {
      compression: false,
      resolution: "1280x720",
      quality: 85,
    };
  }
  try {
    const parsed = JSON.parse(raw);
    return {
      compression: Boolean(parsed.compression),
      resolution: parsed.resolution || "1280x720",
      quality: Number(parsed.quality) || 85,
    };
  } catch {
    return {
      compression: false,
      resolution: "1280x720",
      quality: 85,
    };
  }
};

const saveSettings = (settings) => {
  localStorage.setItem("pccontroller.settings", JSON.stringify(settings));
};

const sendWebAppData = (payload) => {
  if (!tg) {
    setStatus("Команда недоступна вне Telegram.");
    return;
  }
  tg.sendData(JSON.stringify(payload));
};

const buildSettingsPayload = () => {
  const [width, height] = resolutionSelect.value.split("x").map(Number);
  return {
    compression: compressionToggle.checked,
    width,
    height,
    quality: Number(qualityRange.value),
  };
};

const buildQueryPayload = (action) => {
  const settings = buildSettingsPayload();
  const params = new URLSearchParams({
    action,
    compression: settings.compression ? "1" : "0",
    width: String(settings.width),
    height: String(settings.height),
    quality: String(settings.quality),
  });
  return params.toString();
};

const initialSettings = loadSettings();
compressionToggle.checked = initialSettings.compression;
resolutionSelect.value = initialSettings.resolution;
qualityRange.value = String(initialSettings.quality);
qualityValue.textContent = qualityRange.value;

if (!tg) {
  warningEl.hidden = false;
  setStatus("Откройте Web App через Telegram.");
} else {
  tg.ready();
  tg.expand();
}

qualityRange.addEventListener("input", () => {
  qualityValue.textContent = qualityRange.value;
});

const handleSettingsChange = () => {
  const settings = {
    compression: compressionToggle.checked,
    resolution: resolutionSelect.value,
    quality: Number(qualityRange.value),
  };
  saveSettings(settings);
  setStatus("Настройки сохранены локально.");
};

qualityRange.addEventListener("change", handleSettingsChange);
resolutionSelect.addEventListener("change", handleSettingsChange);
compressionToggle.addEventListener("change", handleSettingsChange);

screenshotBtn.addEventListener("click", () => {
  if (!tg) {
    setStatus("Команда недоступна вне Telegram.");
    return;
  }
  setStatus("Команда отправлена, ожидайте скриншот в чате.");
  if (!tg) {
    return;
  }
  tg.sendData(buildQueryPayload("screenshot"));
});
