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

const sendWebAppData = (payload) => {
  if (!tg) {
    setStatus("Команда недоступна вне Telegram.");
    return;
  }
  tg.sendData(JSON.stringify(payload));
};

const sendSettings = () => {
  const [width, height] = resolutionSelect.value.split("x").map(Number);
  const payload = {
    action: "settings",
    compression: compressionToggle.checked,
    width,
    height,
    quality: Number(qualityRange.value),
  };
  sendWebAppData(payload);
  setStatus("Настройки отправлены.");
};

if (!tg) {
  warningEl.hidden = false;
  setStatus("Откройте Web App через Telegram.");
} else {
  tg.ready();
  tg.expand();
}

qualityValue.textContent = qualityRange.value;

qualityRange.addEventListener("input", () => {
  qualityValue.textContent = qualityRange.value;
});

qualityRange.addEventListener("change", sendSettings);
resolutionSelect.addEventListener("change", sendSettings);
compressionToggle.addEventListener("change", sendSettings);

screenshotBtn.addEventListener("click", () => {
  if (!tg) {
    setStatus("Команда недоступна вне Telegram.");
    return;
  }
  setStatus("Команда отправлена, ожидайте скриншот в чате.");
  sendWebAppData({ action: "screenshot" });
});
